// Peer Connector Module
// Orchestrates WebRTC connection flow using signaling client and WebRTC manager

import { WebRTCManager, ConnectionState } from './webrtc-manager.js';
import { SignalingClient, SignalingState } from './signaling-client.js';

/**
 * Generate a random peer ID (8-character base62)
 * @returns {string}
 */
export function generatePeerId() {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let result = '';
    const array = new Uint8Array(8);
    crypto.getRandomValues(array);
    for (let i = 0; i < 8; i++) {
        result += chars[array[i] % chars.length];
    }
    return result;
}

/**
 * Connector states
 */
export const ConnectorState = {
    DISCONNECTED: 'disconnected',
    CONNECTING: 'connecting',
    CONNECTED: 'connected',
    RECONNECTING: 'reconnecting'
};

/**
 * Simple event emitter for connector events
 */
class EventEmitter {
    constructor() {
        this._listeners = {};
    }

    on(event, callback) {
        if (!this._listeners[event]) {
            this._listeners[event] = [];
        }
        this._listeners[event].push(callback);
    }

    off(event, callback) {
        if (!this._listeners[event]) return;
        this._listeners[event] = this._listeners[event].filter(cb => cb !== callback);
    }

    emit(event, ...args) {
        if (!this._listeners[event]) return;
        this._listeners[event].forEach(callback => {
            try {
                callback(...args);
            } catch (err) {
                console.error(`Error in connector event listener for ${event}:`, err);
            }
        });
    }
}

/**
 * PeerConnector orchestrates WebRTC connection establishment
 * using WebSocket signaling for coordination
 */
export class PeerConnector {
    /**
     * @param {Object} options - Configuration options
     * @param {string} [options.peerId] - Local peer ID (generated if not provided)
     * @param {string} [options.signalingUrl] - WebSocket signaling server URL
     * @param {RTCIceServer[]} [options.iceServers] - ICE server configuration
     * @param {number} [options.maxPeers] - Maximum peers (default: 10)
     */
    constructor(options = {}) {
        this._peerId = options.peerId || generatePeerId();
        this._roomId = null;
        this._state = ConnectorState.DISCONNECTED;
        this._emitter = new EventEmitter();

        // Create signaling client
        this._signaling = new SignalingClient({
            url: options.signalingUrl
        });

        // Create WebRTC manager
        this._rtc = new WebRTCManager({
            iceServers: options.iceServers,
            maxPeers: options.maxPeers || 10
        });
        this._rtc.setLocalPeerId(this._peerId);

        this._setupSignalingHandlers();
        this._setupRTCHandlers();
    }

    /**
     * Get local peer ID
     * @returns {string}
     */
    get peerId() {
        return this._peerId;
    }

    /**
     * Get current room ID
     * @returns {string|null}
     */
    get roomId() {
        return this._roomId;
    }

    /**
     * Get current connection state
     * @returns {string}
     */
    get state() {
        return this._state;
    }

    /**
     * Get count of connected peers
     * @returns {number}
     */
    get peerCount() {
        return this._rtc.getReadyConnectionCount();
    }

    /**
     * Get all connected peer IDs
     * @returns {string[]}
     */
    get connectedPeers() {
        return this._rtc.getConnectedPeerIds();
    }

    /**
     * Set up signaling event handlers
     * @private
     */
    _setupSignalingHandlers() {
        // Handle room join confirmation
        this._signaling.on('joined', (roomId, existingPeers) => {
            this._roomId = roomId;
            this._state = ConnectorState.CONNECTED;
            this._emitter.emit('statechange', this._state);
            this._emitter.emit('joined', roomId, existingPeers);

            // Initiate connections to existing peers in the room
            for (const peerId of existingPeers) {
                this._initiateConnection(peerId);
            }
        });

        // Handle new peer joining
        this._signaling.on('peerjoined', (peerId) => {
            this._emitter.emit('peerjoined', peerId);
            // The new peer will initiate connection to us
            // We just wait for their offer
        });

        // Handle peer leaving
        this._signaling.on('peerleft', (peerId) => {
            this._rtc.removePeer(peerId);
            this._emitter.emit('peerleft', peerId);
        });

        // Handle incoming SDP offer
        this._signaling.on('offer', async (fromPeerId, sdp) => {
            try {
                await this._handleOffer(fromPeerId, sdp);
            } catch (err) {
                console.error('Error handling offer from', fromPeerId, err);
            }
        });

        // Handle incoming SDP answer
        this._signaling.on('answer', async (fromPeerId, sdp) => {
            try {
                await this._handleAnswer(fromPeerId, sdp);
            } catch (err) {
                console.error('Error handling answer from', fromPeerId, err);
            }
        });

        // Handle incoming ICE candidate
        this._signaling.on('icecandidate', async (fromPeerId, candidate) => {
            try {
                await this._handleIceCandidate(fromPeerId, candidate);
            } catch (err) {
                console.error('Error handling ICE candidate from', fromPeerId, err);
            }
        });

        // Handle signaling state changes
        this._signaling.on('statechange', (state) => {
            if (state === SignalingState.RECONNECTING) {
                this._state = ConnectorState.RECONNECTING;
                this._emitter.emit('statechange', this._state);
            } else if (state === SignalingState.DISCONNECTED) {
                if (this._state === ConnectorState.CONNECTED) {
                    this._state = ConnectorState.RECONNECTING;
                    this._emitter.emit('statechange', this._state);
                }
            }
        });

        // Handle signaling reconnection
        this._signaling.on('reconnected', () => {
            this._state = ConnectorState.CONNECTED;
            this._emitter.emit('statechange', this._state);
            this._emitter.emit('reconnected');
        });

        // Handle signaling errors
        this._signaling.on('error', (err) => {
            this._emitter.emit('error', err);
        });

        this._signaling.on('servererror', (message) => {
            this._emitter.emit('servererror', message);
        });

        this._signaling.on('maxreconnectsreached', () => {
            this._state = ConnectorState.DISCONNECTED;
            this._emitter.emit('statechange', this._state);
            this._emitter.emit('maxreconnectsreached');
        });
    }

    /**
     * Set up WebRTC manager event handlers
     * @private
     */
    _setupRTCHandlers() {
        // Handle ICE candidates from local peers
        this._rtc.on('icecandidate', (peerId, candidate) => {
            this._signaling.sendIceCandidate(peerId, candidate);
        });

        // Handle peer ready (all channels open)
        this._rtc.on('peerready', (peerId) => {
            this._emitter.emit('peerready', peerId);
        });

        // Handle peer connection
        this._rtc.on('peerconnected', (peerId) => {
            this._emitter.emit('peerconnected', peerId);
        });

        // Handle peer disconnection
        this._rtc.on('peerdisconnected', (peerId) => {
            this._emitter.emit('peerdisconnected', peerId);
        });

        // Handle peer failure
        this._rtc.on('peerfailed', (peerId) => {
            this._emitter.emit('peerfailed', peerId);
        });

        // Handle incoming messages
        this._rtc.on('message', (peerId, channel, data) => {
            this._emitter.emit('message', peerId, channel, data);
        });

        // Handle channel events
        this._rtc.on('channelopen', (peerId, channel) => {
            this._emitter.emit('channelopen', peerId, channel);
        });

        this._rtc.on('channelclose', (peerId, channel) => {
            this._emitter.emit('channelclose', peerId, channel);
        });
    }

    /**
     * Initiate a WebRTC connection to a peer
     * @param {string} peerId - Remote peer ID
     * @private
     */
    async _initiateConnection(peerId) {
        try {
            const peer = this._rtc.createPeerConnection(peerId);
            const offer = await peer.createOffer();
            this._signaling.sendOffer(peerId, offer);
        } catch (err) {
            console.error('Error initiating connection to', peerId, err);
            this._emitter.emit('connectionerror', peerId, err);
        }
    }

    /**
     * Handle incoming SDP offer
     * @param {string} fromPeerId - Remote peer ID
     * @param {RTCSessionDescriptionInit} sdp - SDP offer
     * @private
     */
    async _handleOffer(fromPeerId, sdp) {
        const peer = this._rtc.acceptPeerConnection(fromPeerId);
        await peer.setRemoteDescription(sdp);
        const answer = await peer.createAnswer();
        this._signaling.sendAnswer(fromPeerId, answer);
    }

    /**
     * Handle incoming SDP answer
     * @param {string} fromPeerId - Remote peer ID
     * @param {RTCSessionDescriptionInit} sdp - SDP answer
     * @private
     */
    async _handleAnswer(fromPeerId, sdp) {
        const peer = this._rtc.getPeer(fromPeerId);
        if (peer) {
            await peer.setRemoteDescription(sdp);
        }
    }

    /**
     * Handle incoming ICE candidate
     * @param {string} fromPeerId - Remote peer ID
     * @param {RTCIceCandidateInit} candidate - ICE candidate
     * @private
     */
    async _handleIceCandidate(fromPeerId, candidate) {
        const peer = this._rtc.getPeer(fromPeerId);
        if (peer) {
            await peer.addIceCandidate(candidate);
        }
    }

    /**
     * Join a collaboration room
     * @param {string} roomId - Room ID to join
     * @returns {Promise<void>}
     */
    async join(roomId) {
        if (this._state === ConnectorState.CONNECTED && this._roomId === roomId) {
            return; // Already in this room
        }

        this._state = ConnectorState.CONNECTING;
        this._emitter.emit('statechange', this._state);

        try {
            await this._signaling.connect(roomId, this._peerId);
        } catch (err) {
            this._state = ConnectorState.DISCONNECTED;
            this._emitter.emit('statechange', this._state);
            throw err;
        }
    }

    /**
     * Leave the current room
     */
    leave() {
        this._signaling.leave();
        this._rtc.close();
        this._roomId = null;
        this._state = ConnectorState.DISCONNECTED;
        this._emitter.emit('statechange', this._state);
        this._emitter.emit('left');
    }

    /**
     * Disconnect completely
     */
    disconnect() {
        this._signaling.disconnect();
        this._rtc.close();
        this._roomId = null;
        this._state = ConnectorState.DISCONNECTED;
        this._emitter.emit('statechange', this._state);
    }

    /**
     * Send an operation message to a specific peer
     * @param {string} peerId - Target peer ID
     * @param {string} data - Message data
     * @returns {boolean} Whether the send was successful
     */
    sendOperationToPeer(peerId, data) {
        return this._rtc.sendOperationToPeer(peerId, data);
    }

    /**
     * Broadcast an operation message to all connected peers
     * @param {string} data - Message data
     */
    broadcastOperation(data) {
        this._rtc.broadcastOperation(data);
    }

    /**
     * Send a presence message to a specific peer
     * @param {string} peerId - Target peer ID
     * @param {string} data - Message data
     * @returns {boolean} Whether the send was successful
     */
    sendPresenceToPeer(peerId, data) {
        return this._rtc.sendPresenceToPeer(peerId, data);
    }

    /**
     * Broadcast a presence message to all connected peers
     * @param {string} data - Message data
     */
    broadcastPresence(data) {
        this._rtc.broadcastPresence(data);
    }

    /**
     * Check if connected to at least one peer
     * @returns {boolean}
     */
    isConnected() {
        return this._rtc.getReadyConnectionCount() > 0;
    }

    /**
     * Check if signaling server is connected
     * @returns {boolean}
     */
    isSignalingConnected() {
        return this._signaling.isConnected();
    }

    /**
     * Register an event listener
     * @param {string} event - Event name
     * @param {Function} callback - Callback function
     */
    on(event, callback) {
        this._emitter.on(event, callback);
    }

    /**
     * Remove an event listener
     * @param {string} event - Event name
     * @param {Function} callback - Callback function
     */
    off(event, callback) {
        this._emitter.off(event, callback);
    }
}
