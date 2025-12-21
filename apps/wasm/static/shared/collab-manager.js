// Collaboration Manager Module
// Connects CRDT operations from the WASM engine to the WebRTC P2P layer

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
 * Manages collaboration between the WASM engine and WebRTC peers
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

        // Track last synced HLC per peer for incremental sync
        this._peerSyncState = new Map();

        // Queue for operations waiting to be sent (while connecting)
        this._pendingOperations = [];

        // Statistics
        this._stats = {
            operationsSent: 0,
            operationsReceived: 0,
            operationsApplied: 0,
            operationsDuplicate: 0
        };

        this._setupWebRTCHandlers();
        this._setupSignalingHandlers();
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
     * Initialize collaboration with a peer ID
     * @param {string} [peerId] - Optional peer ID (generated if not provided)
     */
    initialize(peerId = null) {
        this._peerId = peerId || this._loadOrGeneratePeerId();

        // Set node ID in the WASM engine for HLC generation
        const result = JSON.parse(this._engine.setNodeId(this._peerId));
        if (result.error) {
            throw new Error(`Failed to set node ID: ${result.error}`);
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
        this._signalingClient.leave();
        this._webrtcManager.close();
        this._peerSyncState.clear();
        this._pendingOperations = [];
        this._roomId = null;
        this._setState(CollabState.OFFLINE);
        this._emitter.emit('roomleft');
    }

    /**
     * Broadcast a local operation to all connected peers
     * Called by the application when user makes an edit
     * @param {Object} operation - Operation in JSON format
     */
    broadcastOperation(operation) {
        const message = JSON.stringify({
            type: 'operations',
            batch: [operation]
        });

        const readyCount = this._webrtcManager.getReadyConnectionCount();
        if (readyCount > 0) {
            this._webrtcManager.broadcastOperation(message);
            this._stats.operationsSent++;
            this._emitter.emit('operationsent', operation);
        } else if (this._state === CollabState.CONNECTING || this._state === CollabState.SYNCING) {
            // Queue operation for later
            this._pendingOperations.push(operation);
        }
    }

    /**
     * Broadcast multiple operations as a batch
     * @param {Object[]} operations - Array of operations
     */
    broadcastOperations(operations) {
        if (operations.length === 0) return;

        const message = JSON.stringify({
            type: 'operations',
            batch: operations
        });

        const readyCount = this._webrtcManager.getReadyConnectionCount();
        if (readyCount > 0) {
            this._webrtcManager.broadcastOperation(message);
            this._stats.operationsSent += operations.length;
            operations.forEach(op => this._emitter.emit('operationsent', op));
        } else if (this._state === CollabState.CONNECTING || this._state === CollabState.SYNCING) {
            // Queue operations for later
            this._pendingOperations.push(...operations);
        }
    }

    /**
     * Get all operations since a given HLC (for sync)
     * @param {string} [sinceHLC=''] - HLC to get operations after
     * @returns {Object[]} Array of operations
     */
    getOperationsSince(sinceHLC = '') {
        const result = JSON.parse(this._engine.getOperationsSince(sinceHLC));
        if (result.error) {
            console.error('Failed to get operations:', result.error);
            return [];
        }
        return result.operations || [];
    }

    /**
     * Get the current HLC timestamp
     * @returns {string}
     */
    getCurrentHLC() {
        return this._engine.getCurrentHLC();
    }

    /**
     * Get the number of operations in the OpLog
     * @returns {number}
     */
    getOpLogSize() {
        return this._engine.getOpLogSize();
    }

    /**
     * Set up WebRTC event handlers
     * @private
     */
    _setupWebRTCHandlers() {
        // Handle incoming messages
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

        // Handle peer disconnection
        this._webrtcManager.on('peerdisconnected', (peerId) => {
            this._peerSyncState.delete(peerId);
            this._updateState();
        });

        this._webrtcManager.on('peerremoved', (peerId) => {
            this._peerSyncState.delete(peerId);
            this._updateState();
        });
    }

    /**
     * Set up signaling event handlers
     * @private
     */
    _setupSignalingHandlers() {
        // Handle room join confirmation
        this._signalingClient.on('joined', (room, existingPeers) => {
            // Connect to all existing peers in the room
            for (const peerId of existingPeers) {
                this._initiateConnection(peerId);
            }

            if (existingPeers.length === 0) {
                // We're the first/only peer, go online immediately
                this._setState(CollabState.ONLINE);
            }
        });

        // Handle new peer joining
        this._signalingClient.on('peerjoined', (peerId) => {
            // Existing peers create connections to new peers
            // New peer already initiated connections in 'joined' handler
            // This ensures we don't create duplicate connections
        });

        // Handle peer leaving
        this._signalingClient.on('peerleft', (peerId) => {
            this._webrtcManager.removePeer(peerId);
            this._peerSyncState.delete(peerId);
            this._updateState();
        });

        // Handle incoming offer
        this._signalingClient.on('offer', async (fromPeerId, sdp) => {
            await this._handleOffer(fromPeerId, sdp);
        });

        // Handle incoming answer
        this._signalingClient.on('answer', async (fromPeerId, sdp) => {
            await this._handleAnswer(fromPeerId, sdp);
        });

        // Handle incoming ICE candidate
        this._signalingClient.on('icecandidate', async (fromPeerId, candidate) => {
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
            const peer = this._webrtcManager.createPeerConnection(peerId);

            // Set up ICE candidate handler
            peer.on('icecandidate', (candidate) => {
                this._signalingClient.sendIceCandidate(peerId, candidate);
            });

            // Create and send offer
            const offer = await peer.createOffer();
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
            const peer = this._webrtcManager.acceptPeerConnection(fromPeerId);

            // Set up ICE candidate handler
            peer.on('icecandidate', (candidate) => {
                this._signalingClient.sendIceCandidate(fromPeerId, candidate);
            });

            // Set remote description and create answer
            await peer.setRemoteDescription(sdp);
            const answer = await peer.createAnswer();
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
    _onPeerReady(peerId) {
        this._setState(CollabState.SYNCING);

        // Send hello message with our current HLC
        this._sendHello(peerId);
    }

    /**
     * Send hello message to initiate sync
     * @param {string} peerId
     * @private
     */
    _sendHello(peerId) {
        const currentHLC = this.getCurrentHLC();
        const message = JSON.stringify({
            type: 'hello',
            peer_id: this._peerId,
            hlc: currentHLC,
            op_count: this.getOpLogSize()
        });

        this._webrtcManager.sendOperationToPeer(peerId, message);
    }

    /**
     * Handle incoming operations message
     * @param {string} peerId
     * @param {string} data - JSON string
     * @private
     */
    _handleOperationsMessage(peerId, data) {
        try {
            const message = JSON.parse(data);

            switch (message.type) {
                case 'hello':
                    this._handleHello(peerId, message);
                    break;

                case 'sync-request':
                    this._handleSyncRequest(peerId, message);
                    break;

                case 'sync-response':
                    this._handleSyncResponse(peerId, message);
                    break;

                case 'operations':
                    this._handleOperationsBatch(peerId, message.batch || []);
                    break;

                default:
                    console.warn('Unknown operations message type:', message.type);
            }
        } catch (err) {
            console.error('Failed to handle operations message:', err);
        }
    }

    /**
     * Handle hello message from peer
     * @param {string} peerId
     * @param {Object} message
     * @private
     */
    _handleHello(peerId, message) {
        const peerHLC = message.hlc || '';
        const peerOpCount = message.op_count || 0;

        // Store peer's HLC for tracking
        this._peerSyncState.set(peerId, {
            lastHLC: peerHLC,
            synced: false
        });

        // Request operations we don't have
        const ourHLC = this.getCurrentHLC();
        this._sendSyncRequest(peerId, ourHLC);
    }

    /**
     * Send sync request to get operations since our last known HLC
     * @param {string} peerId
     * @param {string} sinceHLC
     * @private
     */
    _sendSyncRequest(peerId, sinceHLC) {
        const message = JSON.stringify({
            type: 'sync-request',
            since_hlc: sinceHLC
        });

        this._webrtcManager.sendOperationToPeer(peerId, message);
    }

    /**
     * Handle sync request from peer
     * @param {string} peerId
     * @param {Object} message
     * @private
     */
    _handleSyncRequest(peerId, message) {
        const sinceHLC = message.since_hlc || '';
        const operations = this.getOperationsSince(sinceHLC);

        // Send operations in response (batch of max 100)
        const batchSize = 100;
        for (let i = 0; i < operations.length; i += batchSize) {
            const batch = operations.slice(i, i + batchSize);
            const isLast = i + batchSize >= operations.length;

            const response = JSON.stringify({
                type: 'sync-response',
                operations: batch,
                complete: isLast
            });

            this._webrtcManager.sendOperationToPeer(peerId, response);
        }

        // If no operations, still send empty response
        if (operations.length === 0) {
            const response = JSON.stringify({
                type: 'sync-response',
                operations: [],
                complete: true
            });

            this._webrtcManager.sendOperationToPeer(peerId, response);
        }
    }

    /**
     * Handle sync response from peer
     * @param {string} peerId
     * @param {Object} message
     * @private
     */
    _handleSyncResponse(peerId, message) {
        const operations = message.operations || [];
        const isComplete = message.complete || false;

        // Apply received operations
        if (operations.length > 0) {
            this._applyRemoteOperations(operations);
        }

        // Mark sync as complete when we receive the final batch
        if (isComplete) {
            const state = this._peerSyncState.get(peerId);
            if (state) {
                state.synced = true;
                this._peerSyncState.set(peerId, state);
            }

            // Check if all peers are synced
            this._checkSyncComplete();
        }
    }

    /**
     * Handle batch of operations from peer
     * @param {string} peerId
     * @param {Object[]} operations
     * @private
     */
    _handleOperationsBatch(peerId, operations) {
        this._applyRemoteOperations(operations);

        // Update last known HLC for this peer
        if (operations.length > 0) {
            const lastOp = operations[operations.length - 1];
            const state = this._peerSyncState.get(peerId) || { synced: true };
            if (lastOp.hlc) {
                state.lastHLC = lastOp.hlc;
            }
            this._peerSyncState.set(peerId, state);
        }
    }

    /**
     * Apply remote operations to the local workbook
     * @param {Object[]} operations
     * @private
     */
    _applyRemoteOperations(operations) {
        for (const op of operations) {
            this._stats.operationsReceived++;

            // Apply via WASM
            const opJson = JSON.stringify(op);
            const resultStr = this._engine.applyRemoteOperation(opJson);
            const result = JSON.parse(resultStr);

            if (result.result === 'success' || result.result === 'resurrected') {
                this._stats.operationsApplied++;
                this._emitter.emit('operationapplied', op);
            } else if (result.result === 'already_applied') {
                this._stats.operationsDuplicate++;
            } else if (result.result !== 'superseded') {
                console.warn('Failed to apply operation:', result.result, op);
            }
        }
    }

    /**
     * Check if initial sync is complete with all peers
     * @private
     */
    _checkSyncComplete() {
        let allSynced = true;
        for (const [peerId, state] of this._peerSyncState) {
            if (!state.synced) {
                allSynced = false;
                break;
            }
        }

        if (allSynced && this._peerSyncState.size > 0) {
            // Send any pending operations
            if (this._pendingOperations.length > 0) {
                this.broadcastOperations(this._pendingOperations);
                this._pendingOperations = [];
            }

            this._setState(CollabState.ONLINE);
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
