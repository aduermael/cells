// C++ SyncClient Adapter
// Provides a JS interface that wraps the C++ SyncClient for use with existing UI components.
// This adapter replaces the JS-based CollabManager, SignalingClient, WebRTCManager, and PresenceManager.
//
// Architecture: Uses polling (10-20 Hz) to fetch sync state and presence from C++.
// Polling is intentional - C++ sync runs in WASM worker, and pushing events would require
// additional infrastructure. The polling rate is fast enough for smooth UI updates.

import { generateRandomName, getColorForPeer } from "./presence";
import type {
  PeerId,
  RoomId,
  UserColor,
  SyncStateType,
  SyncStats,
  PeerPresence,
  CppSyncState,
  EnableSyncResult,
  Position,
} from "./types";

// ============================================================================
// Types
// ============================================================================

/** Event callback signature */
type EventCallback = (...args: unknown[]) => void;

/** CellsClient interface for sync operations */
interface SyncClient {
  enableSync(url: string, roomId: string, peerId: string): Promise<EnableSyncResult>;
  disableSync(): Promise<void>;
  getSyncState(): Promise<CppSyncState>;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  getRemotePresences(): Promise<{ peers?: Record<string, any> }>;
  processSyncOutgoing(): Promise<void>;
  processSyncPresence(): Promise<void>;
  broadcastSyncOperations(): Promise<void>;
  setSyncLocalName(name: string): Promise<void>;
  setSyncCurrentSheet(sheetId: string): Promise<void>;
  setSyncCursor(col: number, row: number): Promise<void>;
  clearSyncCursor(): Promise<void>;
  setSyncSelection(startCol: number, startRow: number, endCol: number, endRow: number): Promise<void>;
  clearSyncSelection(): Promise<void>;
  setSyncMousePosition(x: number, y: number): Promise<void>;
  clearSyncMousePosition(): Promise<void>;
  setSyncEditing(col: number, row: number, text: string): Promise<void>;
  clearSyncEditing(): Promise<void>;
}

/** Options for CppSyncAdapter constructor */
interface CppSyncAdapterOptions {
  client: SyncClient;
}

/** Debug export data structure */
interface DebugData {
  timestamp: string;
  peerId: PeerId | null;
  roomId: RoomId | null;
  state: SyncStateType;
  stats: SyncStats;
  peerLatencies: Record<string, number | null>;
  remotePeers: Record<string, { name: string; color: UserColor }>;
}

// ============================================================================
// Constants
// ============================================================================

/**
 * Sync states - matches C++ SyncClientState enum
 */
export const SyncState = {
  OFFLINE: "offline" as const,
  CONNECTING: "connecting" as const,
  SYNCING: "syncing" as const,
  ONLINE: "online" as const,
  ERROR: "error" as const,
};

// Also export as CollabState for backwards compatibility with collab-ui.js
export const CollabState = SyncState;

// ============================================================================
// EventEmitter
// ============================================================================

/**
 * Simple event emitter
 */
class EventEmitter {
  private _listeners: Record<string, EventCallback[]>;

  constructor() {
    this._listeners = {};
  }

  on(event: string, callback: EventCallback): void {
    if (!this._listeners[event]) {
      this._listeners[event] = [];
    }
    this._listeners[event]!.push(callback);
  }

  off(event: string, callback: EventCallback): void {
    if (!this._listeners[event]) return;
    this._listeners[event] = this._listeners[event]!.filter((cb) => cb !== callback);
  }

  emit(event: string, ...args: unknown[]): void {
    if (!this._listeners[event]) return;
    this._listeners[event]!.forEach((callback) => {
      try {
        callback(...args);
      } catch (err) {
        console.error(`Error in event listener for ${event}:`, err);
      }
    });
  }
}

// ============================================================================
// CppSyncAdapter
// ============================================================================

/**
 * C++ SyncClient Adapter
 * Provides a CollabManager-compatible interface that uses the C++ SyncClient.
 */
export class CppSyncAdapter {
  private _client: SyncClient;
  private _emitter: EventEmitter;

  private _state: SyncStateType;
  private _peerId: PeerId | null;
  private _roomId: RoomId | null;
  private _localName: string | null;
  private _localColor: UserColor | null;

  // Statistics
  private _stats: SyncStats;

  // Latency tracking (from C++ PeerInfo)
  private _peerLatencies: Map<PeerId, number | null>;

  // Remote presence cache
  private _remotePeers: Map<PeerId, PeerPresence>;

  // Polling interval for sync state and presence
  private _pollInterval: ReturnType<typeof setInterval> | null;
  private _pollIntervalMs: number;

  // Presence update interval - matches C++ MAX_UPDATES_PER_SEC (5 Hz)
  // Client-side lerping provides smooth cursor display between updates
  private _presenceInterval: ReturnType<typeof setInterval> | null;
  private _presenceIntervalMs: number;

  // Debug mode
  private _debugMode: boolean;

  constructor(options: CppSyncAdapterOptions) {
    if (!options.client) {
      throw new Error("CppSyncAdapter requires client option");
    }

    this._client = options.client;
    this._emitter = new EventEmitter();

    this._state = SyncState.OFFLINE;
    this._peerId = null;
    this._roomId = null;
    this._localName = null;
    this._localColor = null;

    // Statistics
    this._stats = {
      operationsSent: 0,
      operationsReceived: 0,
      operationsApplied: 0,
      operationsDuplicate: 0,
    };

    // Latency tracking (from C++ PeerInfo)
    this._peerLatencies = new Map();

    // Remote presence cache
    this._remotePeers = new Map();

    // Polling interval for sync state and presence
    this._pollInterval = null;
    this._pollIntervalMs = 100; // 10 Hz for sync state

    // Presence update interval - matches C++ MAX_UPDATES_PER_SEC (5 Hz)
    // Client-side lerping provides smooth cursor display between updates
    this._presenceInterval = null;
    this._presenceIntervalMs = 200; // 5 Hz - cursor lerping smooths display

    // Debug mode
    this._debugMode = this._loadDebugMode();
  }

  /**
   * Load debug mode setting from localStorage
   */
  private _loadDebugMode(): boolean {
    try {
      return localStorage.getItem("cells.debugMode") === "true";
    } catch {
      return false;
    }
  }

  /**
   * Enable or disable debug mode
   */
  setDebugMode(enabled: boolean): void {
    this._debugMode = enabled;
    try {
      if (enabled) {
        localStorage.setItem("cells.debugMode", "true");
      } else {
        localStorage.removeItem("cells.debugMode");
      }
    } catch {
      // Ignore storage errors
    }
  }

  get debugMode(): boolean {
    return this._debugMode;
  }

  /**
   * Get current state
   */
  get state(): SyncStateType {
    return this._state;
  }

  /**
   * Get local peer ID
   */
  get peerId(): PeerId | null {
    return this._peerId;
  }

  /**
   * Get current room ID
   */
  get roomId(): RoomId | null {
    return this._roomId;
  }

  /**
   * Get sync statistics
   */
  get stats(): SyncStats {
    return { ...this._stats };
  }

  /**
   * Get connected peer count
   */
  getConnectedPeerCount(): number {
    return this._remotePeers.size;
  }

  /**
   * Get average latency across all peers (ms)
   */
  getAverageLatency(): number | null {
    let total = 0;
    let count = 0;
    for (const latency of this._peerLatencies.values()) {
      if (latency !== null && latency > 0) {
        total += latency;
        count++;
      }
    }
    return count > 0 ? Math.round(total / count) : null;
  }

  /**
   * Get latency for a specific peer
   */
  getPeerLatency(peerId: PeerId): number | null {
    return this._peerLatencies.get(peerId) ?? null;
  }

  /**
   * Check if connection quality is poor
   */
  isConnectionPoor(): boolean {
    const avgLatency = this.getAverageLatency();
    return avgLatency !== null && avgLatency > 500;
  }

  /**
   * Initialize the adapter with a peer ID
   */
  async initialize(peerId: PeerId | null = null): Promise<void> {
    this._peerId = peerId ?? this._loadOrGeneratePeerId();
    this._localName = this._loadOrGenerateName();
    this._localColor = getColorForPeer(this._peerId);

    this._emitter.emit("initialized", this._peerId);
  }

  private _loadOrGeneratePeerId(): PeerId {
    const storageKey = "cells.peerId";
    let peerId: string | null = null;

    try {
      peerId = localStorage.getItem(storageKey);
    } catch {
      // localStorage not available
    }

    if (!peerId || peerId.length !== 8) {
      peerId = this._generateId();
      try {
        localStorage.setItem(storageKey, peerId);
      } catch {
        // localStorage not available
      }
    }

    return peerId;
  }

  private _loadOrGenerateName(): string {
    const storageKey = "cells.displayName";
    let name: string | null = null;

    try {
      name = sessionStorage.getItem(storageKey);
    } catch {
      // sessionStorage not available
    }

    if (!name) {
      name = generateRandomName();
      try {
        sessionStorage.setItem(storageKey, name);
      } catch {
        // sessionStorage not available
      }
    }

    return name;
  }

  private _generateId(): string {
    const chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    let id = "";
    for (let i = 0; i < 8; i++) {
      id += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return id;
  }

  /**
   * Join a collaboration room
   */
  async joinRoom(roomId: RoomId): Promise<void> {
    if (!this._peerId) {
      throw new Error("Must call initialize() before joining a room");
    }

    this._roomId = roomId;
    this._setState(SyncState.CONNECTING);

    try {
      // Derive signaling URL from current page
      const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
      const host = window.location.host;
      const url = `${protocol}//${host}/ws`;

      // Enable sync via C++ SyncClient
      const result = await this._client.enableSync(url, roomId, this._peerId);

      if (result.success) {
        this._peerId = result.peerId ?? this._peerId;

        // Set local presence info
        if (this._localName) {
          await this._client.setSyncLocalName(this._localName);
        }

        // Start polling for state updates
        this._startPolling();

        this._emitter.emit("roomjoined", roomId);
      } else {
        throw new Error(result.error ?? "Failed to enable sync");
      }
    } catch (err) {
      this._setState(SyncState.OFFLINE);
      throw err;
    }
  }

  /**
   * Leave the current room
   */
  async leaveRoom(): Promise<void> {
    this._stopPolling();
    await this._client.disableSync();
    this._roomId = null;
    this._remotePeers.clear();
    this._peerLatencies.clear();
    this._setState(SyncState.OFFLINE);
    this._emitter.emit("roomleft");
  }

  /**
   * Force reconnect to all peers
   */
  async forceReconnect(): Promise<void> {
    if (this._roomId) {
      const roomId = this._roomId;
      await this.leaveRoom();
      // Small delay before reconnecting
      await new Promise((resolve) => setTimeout(resolve, 500));
      await this.joinRoom(roomId);
    }
  }

  /**
   * Reset sync state (for debugging)
   */
  async resetSyncState(): Promise<void> {
    await this.leaveRoom();
    this._emitter.emit("syncreset");
  }

  /**
   * Start polling for sync state and presence updates
   */
  private _startPolling(): void {
    if (this._pollInterval) return;

    this._pollInterval = setInterval(() => {
      void this._pollSyncState();
    }, this._pollIntervalMs);

    // Separate faster interval for presence
    this._presenceInterval = setInterval(() => {
      void this._pollPresence();
    }, this._presenceIntervalMs);

    // Poll immediately
    void this._pollSyncState();
    void this._pollPresence();
  }

  private _stopPolling(): void {
    if (this._pollInterval) {
      clearInterval(this._pollInterval);
      this._pollInterval = null;
    }
    if (this._presenceInterval) {
      clearInterval(this._presenceInterval);
      this._presenceInterval = null;
    }
  }

  private async _pollSyncState(): Promise<void> {
    try {
      const state = await this._client.getSyncState();

      // Update state
      const newState = this._mapState(state.state);
      if (newState !== this._state) {
        this._setState(newState);
      }

      // Update peer latencies
      if (state.peers) {
        const currentPeerIds = new Set<string>();
        for (const peer of state.peers) {
          currentPeerIds.add(peer.id);
          const oldLatency = this._peerLatencies.get(peer.id);
          if (peer.latency !== oldLatency) {
            this._peerLatencies.set(peer.id, peer.latency);
            this._emitter.emit("latencyupdate", peer.id, peer.latency);
          }
        }

        // Remove disconnected peers
        for (const peerId of this._peerLatencies.keys()) {
          if (!currentPeerIds.has(peerId)) {
            this._peerLatencies.delete(peerId);
          }
        }
      }

      // Process outgoing messages
      await this._client.processSyncOutgoing();
    } catch (err) {
      console.error("Error polling sync state:", err);
    }
  }

  private async _pollPresence(): Promise<void> {
    try {
      // Process and broadcast our local presence updates
      await this._client.processSyncPresence();

      const result = await this._client.getRemotePresences();
      const peers = result.peers ?? {};

      // Track which peers are new, updated, or removed
      const currentPeerIds = new Set(Object.keys(peers));
      const previousPeerIds = new Set(this._remotePeers.keys());

      // Check for new peers
      for (const peerId of currentPeerIds) {
        if (!previousPeerIds.has(peerId)) {
          this._emitter.emit("peerarrived", peerId, peers[peerId]);
        }
      }

      // Check for removed peers
      for (const peerId of previousPeerIds) {
        if (!currentPeerIds.has(peerId)) {
          const oldPresence = this._remotePeers.get(peerId);
          this._remotePeers.delete(peerId);
          this._emitter.emit("peerleft", peerId, oldPresence);
        }
      }

      // Update all presence
      for (const [peerId, presence] of Object.entries(peers)) {
        const existingPresence = this._remotePeers.get(peerId);

        // Convert C++ presence format to JS format
        const jsPresence: PeerPresence = {
          peer_id: peerId,
          name: presence.name ?? "Unknown",
          color: presence.color ?? getColorForPeer(peerId),
          sheet_id: presence.sheetId,
          cursor: presence.cursor ?? null,
          selection: presence.selection
            ? {
                start: { col: presence.selection.startCol, row: presence.selection.startRow },
                end: { col: presence.selection.endCol, row: presence.selection.endRow },
              }
            : null,
          mouse: presence.mouse ?? null,
          editing: presence.editing ?? null,
          timestamp: Date.now(),
        };

        this._remotePeers.set(peerId, jsPresence);

        // Emit update if presence changed
        if (!existingPresence || JSON.stringify(existingPresence) !== JSON.stringify(jsPresence)) {
          this._emitter.emit("presenceupdated", peerId, jsPresence);
        }
      }
    } catch (err) {
      console.error("Error polling presence:", err);
    }
  }

  private _mapState(cppState: string): SyncStateType {
    // C++ returns uppercase states (e.g., "ONLINE"), JS expects lowercase
    const state = (cppState ?? "").toLowerCase();
    switch (state) {
      case "connecting":
        return SyncState.CONNECTING;
      case "syncing":
        return SyncState.SYNCING;
      case "online":
      case "connected":
        return SyncState.ONLINE;
      case "error":
        return SyncState.ERROR;
      default:
        return SyncState.OFFLINE;
    }
  }

  private _setState(newState: SyncStateType): void {
    if (this._state !== newState) {
      const oldState = this._state;
      this._state = newState;
      this._emitter.emit("statechange", newState, oldState);
    }
  }

  /**
   * Queue local operations for broadcast
   */
  async queueLocalOperationsBroadcast(): Promise<void> {
    try {
      await this._client.broadcastSyncOperations();
    } catch (err) {
      console.error("Error broadcasting operations:", err);
    }
  }

  // ========================================================================
  // Presence API (for PresenceManager compatibility)
  // ========================================================================

  /**
   * Get local peer ID
   */
  get localPeerId(): PeerId | null {
    return this._peerId;
  }

  /**
   * Get local name
   */
  get localName(): string | null {
    return this._localName;
  }

  /**
   * Get local color
   */
  get localColor(): UserColor | null {
    return this._localColor;
  }

  /**
   * Set local display name
   */
  async setLocalName(name: string): Promise<void> {
    this._localName = name;
    try {
      sessionStorage.setItem("cells.displayName", name);
    } catch {
      // sessionStorage not available
    }
    await this._client.setSyncLocalName(name);
    this._emitter.emit("localnamechanged", name);
  }

  /**
   * Set current sheet (for presence tracking)
   */
  async setCurrentSheet(sheetId: string): Promise<void> {
    await this._client.setSyncCurrentSheet(sheetId);
  }

  /**
   * Set cursor position (zero-based indices)
   */
  async setCursor(col: number, row: number): Promise<void> {
    await this._client.setSyncCursor(col, row);
  }

  /**
   * Clear cursor
   */
  async clearCursor(): Promise<void> {
    await this._client.clearSyncCursor();
  }

  /**
   * Set selection range (zero-based indices)
   */
  async setSelection(start: Position, end: Position): Promise<void> {
    await this._client.setSyncSelection(start.col, start.row, end.col, end.row);
  }

  /**
   * Clear selection
   */
  async clearSelection(): Promise<void> {
    await this._client.clearSyncSelection();
  }

  /**
   * Set mouse position (canvas coordinates)
   */
  async setMousePosition(x: number, y: number): Promise<void> {
    await this._client.setSyncMousePosition(x, y);
  }

  /**
   * Clear mouse position
   */
  async clearMousePosition(): Promise<void> {
    await this._client.clearSyncMousePosition();
  }

  /**
   * Set editing state (ephemeral, shows what user is typing)
   */
  async setEditing(col: number, row: number, text: string): Promise<void> {
    await this._client.setSyncEditing(col, row, text);
  }

  /**
   * Clear editing state
   */
  async clearEditing(): Promise<void> {
    await this._client.clearSyncEditing();
  }

  /**
   * Get all remote peer presence data
   */
  getRemotePeers(): Map<PeerId, PeerPresence> {
    return new Map(this._remotePeers);
  }

  /**
   * Get presence for a specific peer
   */
  getPeerPresence(peerId: PeerId): PeerPresence | undefined {
    return this._remotePeers.get(peerId);
  }

  /**
   * Get peers on a specific sheet
   */
  getPeersOnSheet(sheetId: string): PeerPresence[] {
    const peers: PeerPresence[] = [];
    for (const presence of this._remotePeers.values()) {
      if (presence.sheet_id === sheetId) {
        peers.push(presence);
      }
    }
    return peers;
  }

  /**
   * Get remote peer count
   */
  getRemotePeerCount(): number {
    return this._remotePeers.size;
  }

  // ========================================================================
  // Event emitter interface
  // ========================================================================

  on(event: string, callback: EventCallback): void {
    this._emitter.on(event, callback);
  }

  off(event: string, callback: EventCallback): void {
    this._emitter.off(event, callback);
  }

  /**
   * Clean up
   */
  async destroy(): Promise<void> {
    await this.leaveRoom();
  }

  // ========================================================================
  // Debug methods
  // ========================================================================

  getOperationLog(): unknown[] {
    return []; // Not implemented in C++ adapter
  }

  clearOperationLog(): void {
    // Not implemented in C++ adapter
  }

  exportDebugData(): DebugData {
    return {
      timestamp: new Date().toISOString(),
      peerId: this._peerId,
      roomId: this._roomId,
      state: this._state,
      stats: { ...this._stats },
      peerLatencies: Object.fromEntries(this._peerLatencies),
      remotePeers: Object.fromEntries(
        Array.from(this._remotePeers.entries()).map(([k, v]) => [
          k,
          { name: v.name, color: v.color },
        ])
      ),
    };
  }

  downloadDebugData(): void {
    const data = this.exportDebugData();
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `cpp-sync-debug-${Date.now()}.json`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }
}

/**
 * Factory function to create the appropriate sync adapter
 */
export function createSyncAdapter(options: CppSyncAdapterOptions): CppSyncAdapter {
  // For now, always use the C++ adapter
  return new CppSyncAdapter(options);
}
