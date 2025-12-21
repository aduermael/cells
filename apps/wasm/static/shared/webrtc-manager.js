// WebRTC Connection Manager Module
// Manages multiple peer-to-peer WebRTC connections for collaboration

import { getIceServers, getRTCConfiguration } from './ice-config.js';

/**
 * Connection states for tracking peer status
 */
export const ConnectionState = {
    CONNECTING: 'connecting',
    CONNECTED: 'connected',
    DISCONNECTED: 'disconnected',
    FAILED: 'failed',
    CLOSED: 'closed'
};

/**
 * Simple event emitter for connection events
 */
class EventEmitter {
    constructor() {
        this._listeners = {};
    }

    /**
     * Register an event listener
     * @param {string} event - Event name
     * @param {Function} callback - Callback function
     */
    on(event, callback) {
        if (!this._listeners[event]) {
            this._listeners[event] = [];
        }
        this._listeners[event].push(callback);
    }

    /**
     * Remove an event listener
     * @param {string} event - Event name
     * @param {Function} callback - Callback function to remove
     */
    off(event, callback) {
        if (!this._listeners[event]) return;
        this._listeners[event] = this._listeners[event].filter(cb => cb !== callback);
    }

    /**
     * Emit an event to all listeners
     * @param {string} event - Event name
     * @param {...any} args - Arguments to pass to listeners
     */
    emit(event, ...args) {
        if (!this._listeners[event]) return;
        this._listeners[event].forEach(callback => {
            try {
                callback(...args);
            } catch (err) {
                console.error(`Error in event listener for ${event}:`, err);
            }
        });
    }
}

/**
 * Represents a single peer connection with DataChannels
 */
class PeerConnection {
    /**
     * @param {string} peerId - Remote peer ID
     * @param {RTCConfiguration} config - WebRTC configuration
     * @param {boolean} isInitiator - Whether this peer initiates the connection
     */
    constructor(peerId, config, isInitiator) {
        this.peerId = peerId;
        this.isInitiator = isInitiator;
        this.state = ConnectionState.CONNECTING;
        this.pc = new RTCPeerConnection(config);
        this.operationsChannel = null;
        this.presenceChannel = null;
        this._iceCandidates = [];
        this._emitter = new EventEmitter();

        this._setupConnectionHandlers();

        if (isInitiator) {
            this._createDataChannels();
        } else {
            this._setupDataChannelHandler();
        }
    }

    /**
     * Set up RTCPeerConnection event handlers
     * @private
     */
    _setupConnectionHandlers() {
        this.pc.onicecandidate = (event) => {
            if (event.candidate) {
                this._emitter.emit('icecandidate', event.candidate);
            }
        };

        this.pc.onconnectionstatechange = () => {
            const state = this.pc.connectionState;
            switch (state) {
                case 'connected':
                    this.state = ConnectionState.CONNECTED;
                    this._emitter.emit('connected');
                    break;
                case 'disconnected':
                    this.state = ConnectionState.DISCONNECTED;
                    this._emitter.emit('disconnected');
                    break;
                case 'failed':
                    this.state = ConnectionState.FAILED;
                    this._emitter.emit('failed');
                    break;
                case 'closed':
                    this.state = ConnectionState.CLOSED;
                    this._emitter.emit('closed');
                    break;
            }
            this._emitter.emit('statechange', this.state);
        };

        this.pc.oniceconnectionstatechange = () => {
            this._emitter.emit('icestatechange', this.pc.iceConnectionState);
        };
    }

    /**
     * Create DataChannels (initiator only)
     * @private
     */
    _createDataChannels() {
        // Operations channel: ordered, reliable (for CRDT operations)
        this.operationsChannel = this.pc.createDataChannel('operations', {
            ordered: true
        });
        this._setupChannelHandlers(this.operationsChannel, 'operations');

        // Presence channel: unordered, unreliable (for cursor positions, etc.)
        this.presenceChannel = this.pc.createDataChannel('presence', {
            ordered: false,
            maxRetransmits: 0
        });
        this._setupChannelHandlers(this.presenceChannel, 'presence');
    }

    /**
     * Set up handler for incoming DataChannels (non-initiator)
     * @private
     */
    _setupDataChannelHandler() {
        this.pc.ondatachannel = (event) => {
            const channel = event.channel;
            if (channel.label === 'operations') {
                this.operationsChannel = channel;
                this._setupChannelHandlers(channel, 'operations');
            } else if (channel.label === 'presence') {
                this.presenceChannel = channel;
                this._setupChannelHandlers(channel, 'presence');
            }
        };
    }

    /**
     * Set up event handlers for a DataChannel
     * @param {RTCDataChannel} channel - The DataChannel
     * @param {string} type - Channel type ('operations' or 'presence')
     * @private
     */
    _setupChannelHandlers(channel, type) {
        channel.onopen = () => {
            this._emitter.emit('channelopen', type);
        };

        channel.onclose = () => {
            this._emitter.emit('channelclose', type);
        };

        channel.onerror = (error) => {
            this._emitter.emit('channelerror', type, error);
        };

        channel.onmessage = (event) => {
            this._emitter.emit('message', type, event.data);
        };
    }

    /**
     * Create an SDP offer
     * @returns {Promise<RTCSessionDescriptionInit>}
     */
    async createOffer() {
        const offer = await this.pc.createOffer();
        await this.pc.setLocalDescription(offer);
        return offer;
    }

    /**
     * Create an SDP answer
     * @returns {Promise<RTCSessionDescriptionInit>}
     */
    async createAnswer() {
        const answer = await this.pc.createAnswer();
        await this.pc.setLocalDescription(answer);
        return answer;
    }

    /**
     * Set remote SDP description
     * @param {RTCSessionDescriptionInit} description
     */
    async setRemoteDescription(description) {
        await this.pc.setRemoteDescription(new RTCSessionDescription(description));

        // Add any buffered ICE candidates
        for (const candidate of this._iceCandidates) {
            await this.pc.addIceCandidate(candidate);
        }
        this._iceCandidates = [];
    }

    /**
     * Add an ICE candidate from remote peer
     * @param {RTCIceCandidateInit} candidate
     */
    async addIceCandidate(candidate) {
        if (this.pc.remoteDescription) {
            await this.pc.addIceCandidate(new RTCIceCandidate(candidate));
        } else {
            // Buffer candidates until remote description is set
            this._iceCandidates.push(new RTCIceCandidate(candidate));
        }
    }

    /**
     * Send a message on the operations channel
     * @param {string} data - Message data (typically JSON string)
     * @returns {boolean} - Whether the send was successful
     */
    sendOperation(data) {
        if (this.operationsChannel && this.operationsChannel.readyState === 'open') {
            this.operationsChannel.send(data);
            return true;
        }
        return false;
    }

    /**
     * Send a message on the presence channel
     * @param {string} data - Message data (typically JSON string)
     * @returns {boolean} - Whether the send was successful
     */
    sendPresence(data) {
        if (this.presenceChannel && this.presenceChannel.readyState === 'open') {
            this.presenceChannel.send(data);
            return true;
        }
        return false;
    }

    /**
     * Check if both data channels are ready
     * @returns {boolean}
     */
    isReady() {
        return (
            this.operationsChannel &&
            this.operationsChannel.readyState === 'open' &&
            this.presenceChannel &&
            this.presenceChannel.readyState === 'open'
        );
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

    /**
     * Close the connection and clean up
     */
    close() {
        if (this.operationsChannel) {
            this.operationsChannel.close();
        }
        if (this.presenceChannel) {
            this.presenceChannel.close();
        }
        this.pc.close();
        this.state = ConnectionState.CLOSED;
    }
}

/**
 * Manages multiple WebRTC peer connections
 */
export class WebRTCManager {
    /**
     * @param {Object} options - Configuration options
     * @param {RTCIceServer[]} [options.iceServers] - ICE server configuration (uses centralized config if not provided)
     * @param {number} [options.maxPeers] - Maximum number of peers (default: 10)
     * @param {boolean} [options.stunOnly] - Use only STUN servers
     * @param {boolean} [options.turnOnly] - Use only TURN servers (relay mode)
     */
    constructor(options = {}) {
        // Use provided iceServers or get from centralized config
        this.iceServers = options.iceServers || getIceServers({
            stunOnly: options.stunOnly,
            turnOnly: options.turnOnly
        });
        this.maxPeers = options.maxPeers || 10;
        this._configOptions = options;
        this.localPeerId = null;
        this._peers = new Map();
        this._emitter = new EventEmitter();
    }

    /**
     * Set the local peer ID
     * @param {string} peerId
     */
    setLocalPeerId(peerId) {
        this.localPeerId = peerId;
    }

    /**
     * Get RTCConfiguration for peer connections
     * @returns {RTCConfiguration}
     * @private
     */
    _getConfig() {
        // Use centralized config if no custom iceServers were provided
        if (this._configOptions.iceServers) {
            return {
                iceServers: this.iceServers,
                iceCandidatePoolSize: 10
            };
        }
        return getRTCConfiguration({
            stunOnly: this._configOptions.stunOnly,
            turnOnly: this._configOptions.turnOnly
        });
    }

    /**
     * Create a new peer connection (as initiator)
     * @param {string} peerId - Remote peer ID
     * @returns {PeerConnection}
     */
    createPeerConnection(peerId) {
        if (this._peers.has(peerId)) {
            console.warn(`Peer connection already exists for ${peerId}`);
            return this._peers.get(peerId);
        }

        if (this._peers.size >= this.maxPeers) {
            throw new Error(`Maximum peer limit (${this.maxPeers}) reached`);
        }

        const peer = new PeerConnection(peerId, this._getConfig(), true);
        this._setupPeerEventHandlers(peer);
        this._peers.set(peerId, peer);
        this._emitter.emit('peercreated', peerId, peer);
        return peer;
    }

    /**
     * Accept an incoming peer connection (as non-initiator)
     * @param {string} peerId - Remote peer ID
     * @returns {PeerConnection}
     */
    acceptPeerConnection(peerId) {
        if (this._peers.has(peerId)) {
            console.warn(`Peer connection already exists for ${peerId}`);
            return this._peers.get(peerId);
        }

        if (this._peers.size >= this.maxPeers) {
            throw new Error(`Maximum peer limit (${this.maxPeers}) reached`);
        }

        const peer = new PeerConnection(peerId, this._getConfig(), false);
        this._setupPeerEventHandlers(peer);
        this._peers.set(peerId, peer);
        this._emitter.emit('peercreated', peerId, peer);
        return peer;
    }

    /**
     * Set up event handlers for a peer connection
     * @param {PeerConnection} peer
     * @private
     */
    _setupPeerEventHandlers(peer) {
        peer.on('connected', () => {
            this._emitter.emit('peerconnected', peer.peerId);
        });

        peer.on('disconnected', () => {
            this._emitter.emit('peerdisconnected', peer.peerId);
        });

        peer.on('failed', () => {
            this._emitter.emit('peerfailed', peer.peerId);
            this.removePeer(peer.peerId);
        });

        peer.on('closed', () => {
            this._emitter.emit('peerclosed', peer.peerId);
        });

        peer.on('icecandidate', (candidate) => {
            this._emitter.emit('icecandidate', peer.peerId, candidate);
        });

        peer.on('message', (type, data) => {
            this._emitter.emit('message', peer.peerId, type, data);
        });

        peer.on('channelopen', (type) => {
            this._emitter.emit('channelopen', peer.peerId, type);

            // Check if all channels are ready
            if (peer.isReady()) {
                this._emitter.emit('peerready', peer.peerId);
            }
        });

        peer.on('channelclose', (type) => {
            this._emitter.emit('channelclose', peer.peerId, type);
        });

        peer.on('channelerror', (type, error) => {
            this._emitter.emit('channelerror', peer.peerId, type, error);
        });
    }

    /**
     * Get a peer connection by ID
     * @param {string} peerId
     * @returns {PeerConnection|undefined}
     */
    getPeer(peerId) {
        return this._peers.get(peerId);
    }

    /**
     * Get all connected peer IDs
     * @returns {string[]}
     */
    getConnectedPeerIds() {
        return Array.from(this._peers.entries())
            .filter(([_, peer]) => peer.state === ConnectionState.CONNECTED)
            .map(([id, _]) => id);
    }

    /**
     * Get all peer IDs
     * @returns {string[]}
     */
    getAllPeerIds() {
        return Array.from(this._peers.keys());
    }

    /**
     * Get count of active connections
     * @returns {number}
     */
    getConnectionCount() {
        return this._peers.size;
    }

    /**
     * Get count of ready connections (with open data channels)
     * @returns {number}
     */
    getReadyConnectionCount() {
        let count = 0;
        for (const peer of this._peers.values()) {
            if (peer.isReady()) {
                count++;
            }
        }
        return count;
    }

    /**
     * Remove and close a peer connection
     * @param {string} peerId
     */
    removePeer(peerId) {
        const peer = this._peers.get(peerId);
        if (peer) {
            peer.close();
            this._peers.delete(peerId);
            this._emitter.emit('peerremoved', peerId);
        }
    }

    /**
     * Send a message to a specific peer on the operations channel
     * @param {string} peerId
     * @param {string} data
     * @returns {boolean}
     */
    sendOperationToPeer(peerId, data) {
        const peer = this._peers.get(peerId);
        if (peer) {
            return peer.sendOperation(data);
        }
        return false;
    }

    /**
     * Send a message to all connected peers on the operations channel
     * @param {string} data
     */
    broadcastOperation(data) {
        for (const peer of this._peers.values()) {
            peer.sendOperation(data);
        }
    }

    /**
     * Send a message to a specific peer on the presence channel
     * @param {string} peerId
     * @param {string} data
     * @returns {boolean}
     */
    sendPresenceToPeer(peerId, data) {
        const peer = this._peers.get(peerId);
        if (peer) {
            return peer.sendPresence(data);
        }
        return false;
    }

    /**
     * Send a message to all connected peers on the presence channel
     * @param {string} data
     */
    broadcastPresence(data) {
        for (const peer of this._peers.values()) {
            peer.sendPresence(data);
        }
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

    /**
     * Close all connections and clean up
     */
    close() {
        for (const [peerId, peer] of this._peers) {
            peer.close();
            this._emitter.emit('peerremoved', peerId);
        }
        this._peers.clear();
    }
}
