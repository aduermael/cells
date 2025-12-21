// Collaboration Manager Module
// Thin transport layer that routes messages between WebRTC peers and the C++ engine.
// All CRDT sync state and protocol logic lives in the C++ SyncManager.

/**
 * Collaboration states
 */
export const CollabState = {
    OFFLINE: 'offline',
    CONNECTING: 'connecting',
    SYNCING: 'syncing',
    ONLINE: 'online'
};

/**
 * Simple event emitter for collaboration events
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
                console.error(`Error in collab event listener for ${event}:`, err);
            }
        });
    }
}

/**
 * Generate a random 8-character base62 ID
 * @returns {string}
 */
function generatePeerId() {
    const chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
    let id = '';
    for (let i = 0; i < 8; i++) {
        id += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return id;
}

/**
 * Manages collaboration between the WASM engine and WebRTC peers.
 * This is a thin transport layer - all CRDT sync state lives in C++ SyncManager.
 */
export class CollabManager {
    /**
     * @param {Object} options - Configuration options
     * @param {Object} options.engine - The CellsEngine WASM instance
     * @param {Object} options.webrtcManager - WebRTCManager instance
     * @param {Object} options.signalingClient - SignalingClient instance
     */
    constructor(options) {
        if (!options.engine) {
            throw new Error('CollabManager requires engine option');
        }
        if (!options.webrtcManager) {
            throw new Error('CollabManager requires webrtcManager option');
        }
        if (!options.signalingClient) {
            throw new Error('CollabManager requires signalingClient option');
        }

        this._engine = options.engine;
        this._webrtcManager = options.webrtcManager;
        this._signalingClient = options.signalingClient;
        this._emitter = new EventEmitter();

        this._state = CollabState.OFFLINE;
        this._peerId = null;
        this._roomId = null;

        // Statistics
        this._stats = {
            operationsSent: 0,
            operationsReceived: 0,
            operationsApplied: 0,
            operationsDuplicate: 0
        };

        // Latency tracking
        this._peerLatencies = new Map(); // peerId -> {lastPing, latency}
        this._pingInterval = null;
        this._pingIntervalMs = 5000; // Ping every 5 seconds

        // Outgoing message pump interval
        this._outgoingPumpInterval = null;
        this._outgoingPumpIntervalMs = 50; // Check for outgoing messages every 50ms

        // Debug mode
        this._debugMode = this._loadDebugMode();
        this._operationLog = []; // Log of all operations for debugging
        this._maxLogSize = 500; // Maximum operations to keep in log

        this._setupWebRTCHandlers();
        this._setupSignalingHandlers();
    }

    /**
     * Load debug mode setting from localStorage
     * @returns {boolean}
     * @private
     */
    _loadDebugMode() {
        try {
            return localStorage.getItem('cells.debugMode') === 'true';
        } catch (e) {
            return false;
        }
    }

    /**
     * Enable or disable debug mode
     * @param {boolean} enabled
     */
    setDebugMode(enabled) {
        this._debugMode = enabled;
        try {
            if (enabled) {
                localStorage.setItem('cells.debugMode', 'true');
                this._log('Debug mode enabled');
            } else {
                localStorage.removeItem('cells.debugMode');
                this._log('Debug mode disabled');
            }
        } catch (e) {
            // Ignore storage errors
        }
    }

    /**
     * Check if debug mode is enabled
     * @returns {boolean}
     */
    get debugMode() {
        return this._debugMode;
    }

    /**
     * Log a debug message
     * @param {...any} args
     * @private
     */
    _log(...args) {
        if (this._debugMode) {
            console.log('[Collab]', ...args);
        }
    }

    /**
     * Log an operation to the operation log
     * @param {string} direction - 'sent' or 'received'
     * @param {string} peerId - Peer ID (or 'broadcast' for outgoing)
     * @param {Object} operation - The operation
     * @private
     */
    _logOperation(direction, peerId, operation) {
        const entry = {
            timestamp: Date.now(),
            direction,
            peerId,
            operation: JSON.parse(JSON.stringify(operation)) // Deep copy
        };

        this._operationLog.push(entry);

        // Trim log if too large
        if (this._operationLog.length > this._maxLogSize) {
            this._operationLog = this._operationLog.slice(-this._maxLogSize);
        }

        if (this._debugMode) {
            this._log(`Operation ${direction}`, peerId, operation);
        }
    }

    /**
     * Get the operation log
     * @returns {Array}
     */
    getOperationLog() {
        return [...this._operationLog];
    }

    /**
     * Clear the operation log
     */
    clearOperationLog() {
        this._operationLog = [];
        this._log('Operation log cleared');
    }

    /**
     * Export debug data as JSON
     * @returns {Object}
     */
    exportDebugData() {
        return {
            timestamp: new Date().toISOString(),
            peerId: this._peerId,
            roomId: this._roomId,
            state: this._state,
            stats: { ...this._stats },
            peerLatencies: Object.fromEntries(
                Array.from(this._peerLatencies.entries()).map(([k, v]) => [k, v.latency])
            ),
            operationLog: this._operationLog
        };
    }

    /**
     * Download debug data as a JSON file
     */
    downloadDebugData() {
        const data = this.exportDebugData();
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `collab-debug-${Date.now()}.json`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }

    /**
     * Reset sync state for recovery (use when stuck)
     */
    resetSyncState() {
        this._log('Resetting sync state');

        // Clear latencies
        this._peerLatencies.clear();

        // Close existing connections
        this._webrtcManager.close();

        // Go offline
        this._setState(CollabState.OFFLINE);

        this._emitter.emit('syncreset');
    }

    /**
     * Get current collaboration state
     * @returns {string}
     */
    get state() {
        return this._state;
    }

    /**
     * Get local peer ID
     * @returns {string|null}
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
     * Get collaboration statistics
     * @returns {Object}
     */
    get stats() {
        return { ...this._stats };
    }

    /**
     * Get number of connected peers
     * @returns {number}
     */
    getConnectedPeerCount() {
        return this._webrtcManager.getReadyConnectionCount();
    }

    /**
     * Get average latency across all connected peers (in ms)
     * @returns {number|null} Average latency or null if no data
     */
    getAverageLatency() {
        let total = 0;
        let count = 0;
        for (const data of this._peerLatencies.values()) {
            if (data.latency !== null) {
                total += data.latency;
                count++;
            }
        }
        return count > 0 ? Math.round(total / count) : null;
    }

    /**
     * Get latency for a specific peer
     * @param {string} peerId
     * @returns {number|null}
     */
    getPeerLatency(peerId) {
        const data = this._peerLatencies.get(peerId);
        return data ? data.latency : null;
    }

    /**
     * Check if connection quality is poor (latency > 500ms)
     * @returns {boolean}
     */
    isConnectionPoor() {
        const avgLatency = this.getAverageLatency();
        return avgLatency !== null && avgLatency > 500;
    }

    /**
     * Start periodic latency pinging
     * @private
     */
    _startPinging() {
        if (this._pingInterval) return;

        this._pingInterval = setInterval(() => {
            this._sendPingToAll();
        }, this._pingIntervalMs);

        // Send initial ping immediately
        this._sendPingToAll();
    }

    /**
     * Stop periodic pinging
     * @private
     */
    _stopPinging() {
        if (this._pingInterval) {
            clearInterval(this._pingInterval);
            this._pingInterval = null;
        }
    }

    /**
     * Send ping to all connected peers
     * @private
     */
    _sendPingToAll() {
        const timestamp = Date.now();
        const message = JSON.stringify({
            type: 'ping',
            ts: timestamp
        });

        for (const peerId of this._webrtcManager.getAllPeerIds()) {
            const peer = this._webrtcManager.getPeer(peerId);
            if (peer && peer.isReady()) {
                peer.sendOperation(message);
                // Store ping timestamp for this peer
                const data = this._peerLatencies.get(peerId) || { lastPing: 0, latency: null };
                data.lastPing = timestamp;
                this._peerLatencies.set(peerId, data);
            }
        }
    }

    /**
     * Handle incoming ping message
     * @param {string} peerId
     * @param {number} timestamp
     * @private
     */
    _handlePing(peerId, timestamp) {
        // Respond with pong
        const message = JSON.stringify({
            type: 'pong',
            ts: timestamp
        });
        this._webrtcManager.sendOperationToPeer(peerId, message);
    }

    /**
     * Handle incoming pong message
     * @param {string} peerId
     * @param {number} timestamp
     * @private
     */
    _handlePong(peerId, timestamp) {
        const now = Date.now();
        const rtt = now - timestamp;

        const data = this._peerLatencies.get(peerId) || { lastPing: 0, latency: null };
        data.latency = rtt;
        this._peerLatencies.set(peerId, data);

        this._emitter.emit('latencyupdate', peerId, rtt);
    }

    /**
     * Force reconnect to all peers
     */
    forceReconnect() {
        if (this._roomId && this._signalingClient) {
            // Leave and rejoin the room to force new connections
            this._signalingClient.leave();
            this._webrtcManager.close();
            this._peerLatencies.clear();
            this._setState(CollabState.CONNECTING);

            // Reconnect after a short delay
            setTimeout(() => {
                this._signalingClient.connect(this._roomId, this._peerId)
                    .catch(err => {
                        console.error('Force reconnect failed:', err);
                        this._setState(CollabState.OFFLINE);
                    });
            }, 500);
        }
    }

    /**
     * Initialize collaboration with a peer ID
     * @param {string} [peerId] - Optional peer ID (generated if not provided)
     * @returns {Promise<void>}
     */
    async initialize(peerId = null) {
        this._peerId = peerId || this._loadOrGeneratePeerId();

        // Set node ID in the WASM engine for HLC generation
        const resultStr = await this._engine.setNodeId(this._peerId);
        const result = JSON.parse(resultStr);
        if (result.error) {
            throw new Error(`Failed to set node ID: ${result.error}`);
        }

        // Initialize the SyncManager in the engine
        const syncResultStr = await this._engine.initSyncManager();
        const syncResult = JSON.parse(syncResultStr);
        if (syncResult.error) {
            throw new Error(`Failed to init SyncManager: ${syncResult.error}`);
        }

        this._webrtcManager.setLocalPeerId(this._peerId);
        this._emitter.emit('initialized', this._peerId);
    }

    /**
     * Load peer ID from localStorage or generate a new one
     * @returns {string}
     * @private
     */
    _loadOrGeneratePeerId() {
        const storageKey = 'cells.peerId';
        let peerId = null;

        try {
            peerId = localStorage.getItem(storageKey);
        } catch (e) {
            // localStorage not available
        }

        if (!peerId || peerId.length !== 8) {
            peerId = generatePeerId();
            try {
                localStorage.setItem(storageKey, peerId);
            } catch (e) {
                // localStorage not available
            }
        }

        return peerId;
    }

    /**
     * Join a collaboration room
     * @param {string} roomId - Room ID to join
     * @returns {Promise<void>}
     */
    async joinRoom(roomId) {
        if (!this._peerId) {
            throw new Error('Must call initialize() before joining a room');
        }

        this._roomId = roomId;
        this._setState(CollabState.CONNECTING);

        try {
            await this._signalingClient.connect(roomId, this._peerId);
            this._emitter.emit('roomjoined', roomId);
        } catch (err) {
            this._setState(CollabState.OFFLINE);
            throw err;
        }
    }

    /**
     * Leave the current room
     */
    leaveRoom() {
        this._stopPinging();
        this._stopOutgoingPump();
        this._signalingClient.leave();
        this._webrtcManager.close();
        this._peerLatencies.clear();
        this._roomId = null;
        this._setState(CollabState.OFFLINE);
        this._emitter.emit('roomleft');
    }

    /**
     * Queue local operations for broadcast to all connected peers.
     * Called by the application after a local edit.
     * The C++ engine tracks which operations need to be sent to which peers.
     */
    async queueLocalOperationsBroadcast() {
        try {
            const resultStr = await this._engine.queueOperationsBroadcast();
            const result = JSON.parse(resultStr);
            if (result.error) {
                console.error('[Collab] Failed to queue operations broadcast:', result.error);
                return;
            }
            // Immediately flush outgoing messages
            await this._flushOutgoing();
        } catch (err) {
            console.error('[Collab] queueLocalOperationsBroadcast error:', err);
        }
    }

    /**
     * Set up WebRTC event handlers
     * @private
     */
    _setupWebRTCHandlers() {
        // Handle incoming messages - route through engine's SyncManager
        this._webrtcManager.on('message', (peerId, channelType, data) => {
            if (channelType === 'operations') {
                this._handleOperationsMessage(peerId, data);
            }
            // Presence messages are handled elsewhere
        });

        // Handle peer ready (all DataChannels open)
        this._webrtcManager.on('peerready', (peerId) => {
            this._onPeerReady(peerId);
        });

        // Handle peer disconnection - notify engine
        this._webrtcManager.on('peerdisconnected', async (peerId) => {
            await this._removePeerFromEngine(peerId);
            this._updateState();
        });

        this._webrtcManager.on('peerremoved', async (peerId) => {
            await this._removePeerFromEngine(peerId);
            this._peerLatencies.delete(peerId);
            this._updateState();
        });
    }

    /**
     * Remove peer from engine's SyncManager
     * @param {string} peerId
     * @private
     */
    async _removePeerFromEngine(peerId) {
        try {
            await this._engine.removePeer(peerId);
        } catch (err) {
            console.error(`[Collab] Failed to remove peer ${peerId} from engine:`, err);
        }
    }

    /**
     * Set up signaling event handlers
     * @private
     */
    _setupSignalingHandlers() {
        // Handle room join confirmation
        this._signalingClient.on('joined', (room, existingPeers) => {
            console.log(`[Collab] Joined room ${room}, existing peers:`, existingPeers);
            // Don't initiate connections here - existing peers will initiate via 'peer-joined'
            // This avoids race conditions where both sides try to create offers simultaneously

            if (existingPeers.length === 0) {
                // We're the first/only peer, go online immediately
                this._setState(CollabState.ONLINE);
            }
            // If there are existing peers, wait for them to send offers via 'peer-joined' handler
        });

        // Handle peer list update (e.g., after reconnection)
        this._signalingClient.on('peerlist', (peers) => {
            console.log(`[Collab] Received peer list:`, peers);
            // Don't initiate connections - existing peers will initiate when they see us join
        });

        // Handle new peer joining
        this._signalingClient.on('peerjoined', (peerId) => {
            console.log(`[Collab] Peer joined: ${peerId}, initiating connection`);
            // Existing peer initiates connection to new peer
            this._initiateConnection(peerId);
        });

        // Handle peer leaving
        this._signalingClient.on('peerleft', async (peerId) => {
            this._webrtcManager.removePeer(peerId);
            await this._removePeerFromEngine(peerId);
            this._updateState();
        });

        // Handle incoming offer
        this._signalingClient.on('offer', async (fromPeerId, sdp) => {
            console.log(`[Collab] Received offer from ${fromPeerId}`);
            await this._handleOffer(fromPeerId, sdp);
        });

        // Handle incoming answer
        this._signalingClient.on('answer', async (fromPeerId, sdp) => {
            console.log(`[Collab] Received answer from ${fromPeerId}`);
            await this._handleAnswer(fromPeerId, sdp);
        });

        // Handle incoming ICE candidate
        this._signalingClient.on('icecandidate', async (fromPeerId, candidate) => {
            console.log(`[Collab] Received ICE candidate from ${fromPeerId}`);
            await this._handleIceCandidate(fromPeerId, candidate);
        });

        // Handle disconnection
        this._signalingClient.on('disconnected', () => {
            this._updateState();
        });

        // Handle reconnection
        this._signalingClient.on('reconnected', () => {
            this._updateState();
        });
    }

    /**
     * Initiate a WebRTC connection to a peer
     * @param {string} peerId
     * @private
     */
    async _initiateConnection(peerId) {
        try {
            console.log(`[Collab] Initiating connection to ${peerId}`);
            const peer = this._webrtcManager.createPeerConnection(peerId);

            // Set up ICE candidate handler
            peer.on('icecandidate', (candidate) => {
                this._signalingClient.sendIceCandidate(peerId, candidate);
            });

            // Create and send offer
            const offer = await peer.createOffer();
            console.log(`[Collab] Sending offer to ${peerId}`);
            this._signalingClient.sendOffer(peerId, offer);
        } catch (err) {
            console.error(`Failed to initiate connection to ${peerId}:`, err);
        }
    }

    /**
     * Handle incoming SDP offer
     * @param {string} fromPeerId
     * @param {RTCSessionDescriptionInit} sdp
     * @private
     */
    async _handleOffer(fromPeerId, sdp) {
        try {
            console.log(`[Collab] Handling offer from ${fromPeerId}`);
            const peer = this._webrtcManager.acceptPeerConnection(fromPeerId);

            // Set up ICE candidate handler
            peer.on('icecandidate', (candidate) => {
                this._signalingClient.sendIceCandidate(fromPeerId, candidate);
            });

            // Set remote description and create answer
            await peer.setRemoteDescription(sdp);
            const answer = await peer.createAnswer();
            console.log(`[Collab] Sending answer to ${fromPeerId}`);
            this._signalingClient.sendAnswer(fromPeerId, answer);
        } catch (err) {
            console.error(`Failed to handle offer from ${fromPeerId}:`, err);
        }
    }

    /**
     * Handle incoming SDP answer
     * @param {string} fromPeerId
     * @param {RTCSessionDescriptionInit} sdp
     * @private
     */
    async _handleAnswer(fromPeerId, sdp) {
        try {
            const peer = this._webrtcManager.getPeer(fromPeerId);
            if (peer) {
                await peer.setRemoteDescription(sdp);
            }
        } catch (err) {
            console.error(`Failed to handle answer from ${fromPeerId}:`, err);
        }
    }

    /**
     * Handle incoming ICE candidate
     * @param {string} fromPeerId
     * @param {RTCIceCandidateInit} candidate
     * @private
     */
    async _handleIceCandidate(fromPeerId, candidate) {
        try {
            const peer = this._webrtcManager.getPeer(fromPeerId);
            if (peer) {
                await peer.addIceCandidate(candidate);
            }
        } catch (err) {
            console.error(`Failed to handle ICE candidate from ${fromPeerId}:`, err);
        }
    }

    /**
     * Called when a peer connection becomes ready
     * @param {string} peerId
     * @private
     */
    async _onPeerReady(peerId) {
        console.log(`[Collab] Peer ${peerId} is ready (data channels open)`);
        this._setState(CollabState.SYNCING);

        // Register peer with engine's SyncManager (this queues a hello message)
        try {
            const resultStr = await this._engine.addPeer(peerId);
            const result = JSON.parse(resultStr);
            if (result.error) {
                console.error(`[Collab] Failed to add peer ${peerId}:`, result.error);
                return;
            }
        } catch (err) {
            console.error(`[Collab] Failed to add peer ${peerId}:`, err);
            return;
        }

        // Start the outgoing message pump
        this._startOutgoingPump();

        // Immediately flush outgoing messages (hello message)
        await this._flushOutgoing();

        // Start latency pinging
        this._startPinging();
    }

    /**
     * Handle incoming operations message.
     * Routes all sync protocol messages through the C++ SyncManager.
     * @param {string} peerId
     * @param {string} data - JSON string
     * @private
     */
    async _handleOperationsMessage(peerId, data) {
        try {
            // Handle ping/pong locally for latency measurement
            const message = JSON.parse(data);
            if (message.type === 'ping') {
                this._handlePing(peerId, message.ts);
                return;
            }
            if (message.type === 'pong') {
                this._handlePong(peerId, message.ts);
                return;
            }

            // Log received message for debugging
            this._log('Received message from', peerId, ':', message.type);

            // Signal that we're about to apply remote operations
            this._emitter.emit('remoteoperationsstart');

            try {
                // Route all other messages through engine's SyncManager
                const resultStr = await this._engine.handlePeerMessage(peerId, data);
                const result = JSON.parse(resultStr);

                if (result.error) {
                    console.error(`[Collab] handlePeerMessage error:`, result.error);
                    return;
                }

                // Send any response messages
                if (result.messages && result.messages.length > 0) {
                    await this._sendOutgoingMessages(result.messages);
                }

                // Check if we transitioned to online state
                // The engine marks peers as synced after sync-response
                this._updateSyncState();
            } finally {
                this._emitter.emit('remoteoperationsend');
            }
        } catch (err) {
            console.error('[Collab] Failed to handle operations message:', err);
        }
    }

    /**
     * Start the outgoing message pump
     * @private
     */
    _startOutgoingPump() {
        if (this._outgoingPumpInterval) return;

        this._outgoingPumpInterval = setInterval(() => {
            this._flushOutgoing();
        }, this._outgoingPumpIntervalMs);
    }

    /**
     * Stop the outgoing message pump
     * @private
     */
    _stopOutgoingPump() {
        if (this._outgoingPumpInterval) {
            clearInterval(this._outgoingPumpInterval);
            this._outgoingPumpInterval = null;
        }
    }

    /**
     * Flush outgoing messages from the engine to WebRTC peers
     * @private
     */
    async _flushOutgoing() {
        try {
            const resultStr = await this._engine.getOutgoingMessages();
            const result = JSON.parse(resultStr);

            if (result.error) {
                console.error('[Collab] getOutgoingMessages error:', result.error);
                return;
            }

            if (result.messages && result.messages.length > 0) {
                await this._sendOutgoingMessages(result.messages);
            }
        } catch (err) {
            console.error('[Collab] _flushOutgoing error:', err);
        }
    }

    /**
     * Send outgoing messages to WebRTC peers
     * @param {Array} messages - Array of {peerId, json} objects
     * @private
     */
    async _sendOutgoingMessages(messages) {
        for (const msg of messages) {
            // Unescape the JSON string (it was escaped for transport)
            const jsonStr = msg.json;

            if (msg.peerId === null || msg.peerId === '') {
                // Broadcast to all peers
                this._log('Broadcasting message:', jsonStr.substring(0, 100));
                this._webrtcManager.broadcastOperation(jsonStr);
                this._stats.operationsSent++;
            } else {
                // Send to specific peer
                this._log('Sending to peer', msg.peerId, ':', jsonStr.substring(0, 100));
                this._webrtcManager.sendOperationToPeer(msg.peerId, jsonStr);
                this._stats.operationsSent++;
            }
        }
    }

    /**
     * Update sync state based on engine peer count
     * @private
     */
    async _updateSyncState() {
        try {
            const peerCount = await this._engine.getPeerCount();
            if (peerCount > 0 && this._state === CollabState.SYNCING) {
                // We have peers and are syncing - transition to online
                // The engine handles sync completion internally
                this._setState(CollabState.ONLINE);
            }
        } catch (err) {
            console.error('[Collab] _updateSyncState error:', err);
        }
    }

    /**
     * Update state based on connection status
     * @private
     */
    _updateState() {
        const readyCount = this._webrtcManager.getReadyConnectionCount();
        const isSignalingConnected = this._signalingClient.isConnected();

        if (!isSignalingConnected && readyCount === 0) {
            this._setState(CollabState.OFFLINE);
        } else if (readyCount === 0 && isSignalingConnected) {
            // Connected to signaling but no peers yet
            this._setState(CollabState.ONLINE);
        } else if (this._state === CollabState.OFFLINE) {
            this._setState(CollabState.CONNECTING);
        }
    }

    /**
     * Set collaboration state and emit event
     * @param {string} newState
     * @private
     */
    _setState(newState) {
        if (this._state !== newState) {
            const oldState = this._state;
            this._state = newState;
            this._emitter.emit('statechange', newState, oldState);
        }
    }

    /**
     * Register an event listener
     * Events:
     * - 'initialized' (peerId)
     * - 'statechange' (newState, oldState)
     * - 'roomjoined' (roomId)
     * - 'roomleft'
     * - 'operationsent' (operation)
     * - 'operationapplied' (operation)
     * @param {string} event
     * @param {Function} callback
     */
    on(event, callback) {
        this._emitter.on(event, callback);
    }

    /**
     * Remove an event listener
     * @param {string} event
     * @param {Function} callback
     */
    off(event, callback) {
        this._emitter.off(event, callback);
    }

    /**
     * Clean up resources
     */
    destroy() {
        this.leaveRoom();
        this._signalingClient.disconnect();
    }
}
