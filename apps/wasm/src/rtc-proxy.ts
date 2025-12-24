// WebRTC Proxy - Runs on main thread, proxies RTCPeerConnection calls from worker
//
// The WASM module runs in a Web Worker, but RTCPeerConnection is only available
// on the main thread. This proxy receives requests from the worker and manages
// the actual WebRTC connections.

// ============================================================================
// Types
// ============================================================================

/** ICE server configuration */
interface IceServer {
  urls: string | string[];
  username?: string;
  credential?: string;
}

/** Peer connection entry in registry */
interface PeerConnectionEntry {
  pc: RTCPeerConnection;
}

/** Data channel entry in registry */
interface DataChannelEntry {
  channel: RTCDataChannel;
  registryId: number;
}

/** Callback type for sending messages to worker */
type SendToWorkerCallback = (type: string, payload: Record<string, unknown>) => void;

// ============================================================================
// Message payload types (from worker)
// ============================================================================

interface CreatePeerConnectionPayload {
  iceServers?: IceServer[];
  registryId: number;
}

interface RegistryIdPayload {
  registryId: number;
}

interface SetDescriptionPayload {
  registryId: number;
  sdpType: RTCSdpType;
  sdp: string;
}

interface AddIceCandidatePayload {
  registryId: number;
  candidate: string;
  sdpMid: string | null;
  sdpMLineIndex: number | null;
}

interface CreateDataChannelPayload {
  registryId: number;
  label: string;
  ordered: boolean;
  maxRetransmits: number;
  maxPacketLifeTime: number;
}

interface DcSendPayload {
  dcHandle: number;
  isText: boolean;
  data: string | number[];
}

interface DcHandlePayload {
  dcHandle: number;
}

interface DcSetThresholdPayload {
  dcHandle: number;
  threshold: number;
}

// Message type union for handleMessage
export type RTCMessagePayload =
  | CreatePeerConnectionPayload
  | RegistryIdPayload
  | SetDescriptionPayload
  | AddIceCandidatePayload
  | CreateDataChannelPayload
  | DcSendPayload
  | DcHandlePayload
  | DcSetThresholdPayload
  | Record<string, unknown>; // Allow generic payloads for forward compatibility

// ============================================================================
// Connection state mappings (C++ enum values)
// ============================================================================

/** Maps RTCPeerConnectionState to C++ enum value */
function mapConnectionState(state: RTCPeerConnectionState): number {
  switch (state) {
    case "new":
      return 0;
    case "connecting":
      return 1;
    case "connected":
      return 2;
    case "disconnected":
      return 3;
    case "failed":
      return 4;
    case "closed":
      return 5;
    default:
      return 0;
  }
}

/** Maps RTCIceConnectionState to C++ enum value */
function mapIceConnectionState(state: RTCIceConnectionState): number {
  switch (state) {
    case "new":
      return 0;
    case "checking":
      return 1;
    case "connected":
      return 2;
    case "completed":
      return 3;
    case "disconnected":
      return 4;
    case "failed":
      return 5;
    case "closed":
      return 6;
    default:
      return 0;
  }
}

/** Maps RTCIceGatheringState to C++ enum value */
function mapIceGatheringState(state: RTCIceGatheringState): number {
  switch (state) {
    case "new":
      return 0;
    case "gathering":
      return 1;
    case "complete":
      return 2;
    default:
      return 0;
  }
}

/** Maps RTCSignalingState to C++ enum value */
function mapSignalingState(state: RTCSignalingState): number {
  switch (state) {
    case "stable":
      return 0;
    case "have-local-offer":
      return 1;
    case "have-remote-offer":
      return 2;
    case "have-local-pranswer":
      return 3;
    case "have-remote-pranswer":
      return 4;
    case "closed":
      return 5;
    default:
      return 0;
  }
}

// ============================================================================
// RTCProxy Class
// ============================================================================

/**
 * RTCProxy - Manages WebRTC connections on behalf of the worker
 */
export class RTCProxy {
  private _peerConnections: Map<number, PeerConnectionEntry>;
  private _dataChannels: Map<number, DataChannelEntry>;
  private _nextDcHandle: number;
  private _sendToWorker: SendToWorkerCallback | null;

  constructor() {
    this._peerConnections = new Map();
    this._dataChannels = new Map();
    this._nextDcHandle = 1;
    this._sendToWorker = null;
  }

  /**
   * Set the callback for sending messages to the worker
   */
  setSendCallback(callback: SendToWorkerCallback): void {
    this._sendToWorker = callback;
  }

  /**
   * Handle a message from the worker
   */
  handleMessage(
    type: string,
    payload: RTCMessagePayload
  ): Record<string, unknown> | void | Promise<void> {
    switch (type) {
      case "rtc_create":
        return this._createPeerConnection(payload as CreatePeerConnectionPayload);
      case "rtc_destroy":
        return this._destroyPeerConnection(payload as RegistryIdPayload);
      case "rtc_close":
        return this._closePeerConnection(payload as RegistryIdPayload);
      case "rtc_create_offer":
        return this._createOffer(payload as RegistryIdPayload);
      case "rtc_create_answer":
        return this._createAnswer(payload as RegistryIdPayload);
      case "rtc_set_local_description":
        return this._setLocalDescription(payload as SetDescriptionPayload);
      case "rtc_set_remote_description":
        return this._setRemoteDescription(payload as SetDescriptionPayload);
      case "rtc_add_ice_candidate":
        return this._addIceCandidate(payload as AddIceCandidatePayload);
      case "rtc_create_data_channel":
        return this._createDataChannel(payload as CreateDataChannelPayload);
      // DataChannel operations
      case "dc_send":
        return this._dcSend(payload as DcSendPayload);
      case "dc_close":
        return this._dcClose(payload as DcHandlePayload);
      case "dc_set_buffered_amount_low_threshold":
        return this._dcSetBufferedAmountLowThreshold(payload as DcSetThresholdPayload);
      default:
        console.warn("RTCProxy: Unknown message type:", type);
        return { error: "Unknown message type" };
    }
  }

  // ========================================================================
  // PeerConnection operations
  // ========================================================================

  private _createPeerConnection({
    iceServers,
    registryId,
  }: CreatePeerConnectionPayload): Record<string, unknown> {
    const config: RTCConfiguration = {};
    if (iceServers) {
      config.iceServers = iceServers;
    }

    try {
      const pc = new RTCPeerConnection(config);
      this._peerConnections.set(registryId, { pc });

      // Setup event handlers
      this._setupPeerConnectionCallbacks(registryId, pc);

      return {};
    } catch (err) {
      console.error("RTCProxy: Failed to create PeerConnection:", err);
      return { error: err instanceof Error ? err.message : String(err) };
    }
  }

  private _setupPeerConnectionCallbacks(registryId: number, pc: RTCPeerConnection): void {
    pc.onconnectionstatechange = (): void => {
      this._send("rtc_on_connection_state", {
        registryId,
        state: mapConnectionState(pc.connectionState),
      });
    };

    pc.oniceconnectionstatechange = (): void => {
      this._send("rtc_on_ice_connection_state", {
        registryId,
        state: mapIceConnectionState(pc.iceConnectionState),
      });
    };

    pc.onicegatheringstatechange = (): void => {
      this._send("rtc_on_ice_gathering_state", {
        registryId,
        state: mapIceGatheringState(pc.iceGatheringState),
      });
    };

    pc.onsignalingstatechange = (): void => {
      this._send("rtc_on_signaling_state", {
        registryId,
        state: mapSignalingState(pc.signalingState),
      });
    };

    pc.onicecandidate = (event: RTCPeerConnectionIceEvent): void => {
      if (event.candidate) {
        this._send("rtc_on_ice_candidate", {
          registryId,
          candidate: event.candidate.candidate,
          sdpMid: event.candidate.sdpMid,
          sdpMLineIndex: event.candidate.sdpMLineIndex,
        });
      } else {
        // End of candidates
        this._send("rtc_on_ice_candidate", {
          registryId,
          candidate: null,
          sdpMid: null,
          sdpMLineIndex: 0,
        });
      }
    };

    pc.ondatachannel = (event: RTCDataChannelEvent): void => {
      const channel = event.channel;
      const dcHandle = this._nextDcHandle++;
      this._dataChannels.set(dcHandle, { channel, registryId });
      this._setupDataChannelCallbacks(dcHandle, channel, registryId);

      this._send("rtc_on_data_channel", {
        registryId,
        dcHandle,
        label: channel.label,
      });
    };

    pc.onnegotiationneeded = (): void => {
      this._send("rtc_on_negotiation_needed", { registryId });
    };
  }

  private _destroyPeerConnection({ registryId }: RegistryIdPayload): Record<string, unknown> {
    const entry = this._peerConnections.get(registryId);
    if (entry) {
      entry.pc.close();
      this._peerConnections.delete(registryId);
    }
    return {};
  }

  private _closePeerConnection({ registryId }: RegistryIdPayload): Record<string, unknown> {
    const entry = this._peerConnections.get(registryId);
    if (entry) {
      entry.pc.close();
    }
    return {};
  }

  private async _createOffer({ registryId }: RegistryIdPayload): Promise<void> {
    const entry = this._peerConnections.get(registryId);
    if (!entry) {
      this._send("rtc_on_create_sdp", {
        registryId,
        success: false,
        error: "PeerConnection not found",
      });
      return;
    }

    try {
      const offer = await entry.pc.createOffer();
      this._send("rtc_on_create_sdp", {
        registryId,
        success: true,
        sdp: offer.sdp,
        type: "offer",
      });
    } catch (err) {
      this._send("rtc_on_create_sdp", {
        registryId,
        success: false,
        error: err instanceof Error ? err.message : String(err),
      });
    }
  }

  private async _createAnswer({ registryId }: RegistryIdPayload): Promise<void> {
    const entry = this._peerConnections.get(registryId);
    if (!entry) {
      this._send("rtc_on_create_sdp", {
        registryId,
        success: false,
        error: "PeerConnection not found",
      });
      return;
    }

    try {
      const answer = await entry.pc.createAnswer();
      this._send("rtc_on_create_sdp", {
        registryId,
        success: true,
        sdp: answer.sdp,
        type: "answer",
      });
    } catch (err) {
      this._send("rtc_on_create_sdp", {
        registryId,
        success: false,
        error: err instanceof Error ? err.message : String(err),
      });
    }
  }

  private async _setLocalDescription({
    registryId,
    sdpType,
    sdp,
  }: SetDescriptionPayload): Promise<void> {
    const entry = this._peerConnections.get(registryId);
    if (!entry) {
      this._send("rtc_on_set_sdp", {
        registryId,
        success: false,
        error: "PeerConnection not found",
      });
      return;
    }

    try {
      await entry.pc.setLocalDescription({ type: sdpType, sdp });
      this._send("rtc_on_set_sdp", {
        registryId,
        success: true,
      });
    } catch (err) {
      this._send("rtc_on_set_sdp", {
        registryId,
        success: false,
        error: err instanceof Error ? err.message : String(err),
      });
    }
  }

  private async _setRemoteDescription({
    registryId,
    sdpType,
    sdp,
  }: SetDescriptionPayload): Promise<void> {
    const entry = this._peerConnections.get(registryId);
    if (!entry) {
      this._send("rtc_on_set_sdp", {
        registryId,
        success: false,
        error: "PeerConnection not found",
      });
      return;
    }

    try {
      await entry.pc.setRemoteDescription({ type: sdpType, sdp });
      this._send("rtc_on_set_sdp", {
        registryId,
        success: true,
      });
    } catch (err) {
      this._send("rtc_on_set_sdp", {
        registryId,
        success: false,
        error: err instanceof Error ? err.message : String(err),
      });
    }
  }

  private async _addIceCandidate({
    registryId,
    candidate,
    sdpMid,
    sdpMLineIndex,
  }: AddIceCandidatePayload): Promise<void> {
    const entry = this._peerConnections.get(registryId);
    if (!entry) {
      this._send("rtc_on_set_sdp", {
        registryId,
        success: false,
        error: "PeerConnection not found",
      });
      return;
    }

    try {
      await entry.pc.addIceCandidate({
        candidate,
        sdpMid,
        sdpMLineIndex,
      });
      this._send("rtc_on_set_sdp", {
        registryId,
        success: true,
      });
    } catch (err) {
      this._send("rtc_on_set_sdp", {
        registryId,
        success: false,
        error: err instanceof Error ? err.message : String(err),
      });
    }
  }

  private _createDataChannel({
    registryId,
    label,
    ordered,
    maxRetransmits,
    maxPacketLifeTime,
  }: CreateDataChannelPayload): Record<string, unknown> {
    const entry = this._peerConnections.get(registryId);
    if (!entry) {
      console.error("RTCProxy: PeerConnection not found for createDataChannel");
      return { error: "PeerConnection not found" };
    }

    const options: RTCDataChannelInit = { ordered };
    if (maxRetransmits >= 0) options.maxRetransmits = maxRetransmits;
    if (maxPacketLifeTime >= 0) options.maxPacketLifeTime = maxPacketLifeTime;

    try {
      const channel = entry.pc.createDataChannel(label, options);
      const dcHandle = this._nextDcHandle++;
      this._dataChannels.set(dcHandle, { channel, registryId });
      this._setupDataChannelCallbacks(dcHandle, channel, registryId);

      // Notify worker about the created channel via callback
      // (same path as ondatachannel for consistency)
      this._send("rtc_on_data_channel", {
        registryId,
        dcHandle,
        label,
      });

      return {};
    } catch (err) {
      console.error("RTCProxy: Failed to create DataChannel:", err);
      return { error: err instanceof Error ? err.message : String(err) };
    }
  }

  // ========================================================================
  // DataChannel operations
  // ========================================================================

  private _setupDataChannelCallbacks(
    dcHandle: number,
    channel: RTCDataChannel,
    registryId: number
  ): void {
    channel.onopen = (): void => {
      this._send("dc_on_open", { dcHandle, registryId });
    };

    channel.onclose = (): void => {
      this._send("dc_on_close", { dcHandle, registryId });
    };

    channel.onerror = (event: Event): void => {
      const rtcEvent = event as RTCErrorEvent;
      this._send("dc_on_error", {
        dcHandle,
        registryId,
        error: rtcEvent.error?.message || "DataChannel error",
      });
    };

    channel.onmessage = (event: MessageEvent): void => {
      if (typeof event.data === "string") {
        this._send("dc_on_message", {
          dcHandle,
          registryId,
          isText: true,
          data: event.data,
        });
      } else if (event.data instanceof ArrayBuffer) {
        // Convert ArrayBuffer to array for transfer
        this._send("dc_on_message", {
          dcHandle,
          registryId,
          isText: false,
          data: Array.from(new Uint8Array(event.data)),
        });
      }
    };
  }

  private _dcSend({ dcHandle, isText, data }: DcSendPayload): Record<string, unknown> {
    const entry = this._dataChannels.get(dcHandle);
    if (!entry) {
      return { error: "DataChannel not found" };
    }

    try {
      if (isText) {
        entry.channel.send(data as string);
      } else {
        entry.channel.send(new Uint8Array(data as number[]).buffer);
      }
      return {};
    } catch (err) {
      return { error: err instanceof Error ? err.message : String(err) };
    }
  }

  private _dcClose({ dcHandle }: DcHandlePayload): Record<string, unknown> {
    const entry = this._dataChannels.get(dcHandle);
    if (entry) {
      entry.channel.close();
      this._dataChannels.delete(dcHandle);
    }
    return {};
  }

  private _dcSetBufferedAmountLowThreshold({
    dcHandle,
    threshold,
  }: DcSetThresholdPayload): Record<string, unknown> {
    const entry = this._dataChannels.get(dcHandle);
    if (entry) {
      entry.channel.bufferedAmountLowThreshold = threshold;
    }
    return {};
  }

  // ========================================================================
  // Helper methods
  // ========================================================================

  private _send(type: string, payload: Record<string, unknown>): void {
    if (this._sendToWorker) {
      this._sendToWorker(type, payload);
    }
  }

  /**
   * Clean up all connections
   */
  destroy(): void {
    for (const [, entry] of this._peerConnections) {
      entry.pc.close();
    }
    this._peerConnections.clear();
    this._dataChannels.clear();
  }
}
