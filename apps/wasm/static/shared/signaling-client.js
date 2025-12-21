// WebSocket Signaling Client Module
// Manages WebSocket connection to Go signaling server for WebRTC setup

/**
 * Connection states for the signaling client
 */
export const SignalingState = {
    DISCONNECTED: 'disconnected',
    CONNECTING: 'connecting',
    CONNECTED: 'connected',
    RECONNECTING: 'reconnecting'
};

/**
 * Simple event emitter for signaling events
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
                console.error(`Error in signaling event listener for ${event}:`, err);
            }
        });
    }
}

/**
 * WebSocket signaling client for WebRTC peer-to-peer connection setup
 */
export class SignalingClient {
    /**
     * @param {Object} options - Configuration options
     * @param {string} [options.url] - WebSocket URL (default: derived from window.location)
     * @param {number} [options.reconnectDelay] - Initial reconnect delay in ms (default: 1000)
     * @param {number} [options.maxReconnectDelay] - Maximum reconnect delay in ms (default: 30000)
     * @param {number} [options.reconnectMultiplier] - Delay multiplier for exponential backoff (default: 1.5)
     * @param {number} [options.maxReconnectAttempts] - Maximum reconnect attempts (default: 10)
     */
    constructor(options = {}) {
        this._url = options.url || null;
        this._reconnectDelay = options.reconnectDelay || 1000;
        this._maxReconnectDelay = options.maxReconnectDelay || 30000;
        this._reconnectMultiplier = options.reconnectMultiplier || 1.5;
        this._maxReconnectAttempts = options.maxReconnectAttempts || 10;

        this._ws = null;
        this._roomId = null;
        this._peerId = null;
        this._state = SignalingState.DISCONNECTED;
        this._currentReconnectDelay = this._reconnectDelay;
        this._reconnectAttempts = 0;
        this._reconnectTimer = null;
        this._shouldReconnect = false;
        this._emitter = new EventEmitter();
    }

    /**
     * Get current connection state
     * @returns {string}
     */
    get state() {
        return this._state;
    }

    /**
     * Get current room ID
     * @returns {string|null}
     */
    get roomId() {
        return this._roomId;
    }

    /**
     * Get local peer ID
     * @returns {string|null}
     */
    get peerId() {
        return this._peerId;
    }

    /**
     * Derive WebSocket URL from current page location
     * @returns {string}
     * @private
     */
    _deriveUrl() {
        if (this._url) return this._url;

        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const host = window.location.host;
        return `${protocol}//${host}/ws`;
    }

    /**
     * Connect to the signaling server and join a room
     * @param {string} roomId - Room ID to join
     * @param {string} peerId - Local peer ID
     * @returns {Promise<void>}
     */
    connect(roomId, peerId) {
        return new Promise((resolve, reject) => {
            if (this._ws && this._ws.readyState === WebSocket.OPEN) {
                // Already connected, leave current room and join new one
                this._roomId = roomId;
                this._peerId = peerId;
                this._sendJoin();
                resolve();
                return;
            }

            this._roomId = roomId;
            this._peerId = peerId;
            this._shouldReconnect = true;
            this._state = SignalingState.CONNECTING;
            this._emitter.emit('statechange', this._state);

            const url = this._deriveUrl();
            try {
                this._ws = new WebSocket(url);
            } catch (err) {
                this._state = SignalingState.DISCONNECTED;
                this._emitter.emit('statechange', this._state);
                reject(err);
                return;
            }

            const onOpen = () => {
                cleanup();
                this._state = SignalingState.CONNECTED;
                this._currentReconnectDelay = this._reconnectDelay;
                this._reconnectAttempts = 0;
                this._emitter.emit('statechange', this._state);
                this._setupMessageHandler();
                this._sendJoin();
                resolve();
            };

            const onError = (err) => {
                cleanup();
                this._state = SignalingState.DISCONNECTED;
                this._emitter.emit('statechange', this._state);
                reject(err);
            };

            const onClose = () => {
                cleanup();
                this._state = SignalingState.DISCONNECTED;
                this._emitter.emit('statechange', this._state);
                reject(new Error('WebSocket closed before connection established'));
            };

            const cleanup = () => {
                this._ws.removeEventListener('open', onOpen);
                this._ws.removeEventListener('error', onError);
                this._ws.removeEventListener('close', onClose);
            };

            this._ws.addEventListener('open', onOpen);
            this._ws.addEventListener('error', onError);
            this._ws.addEventListener('close', onClose);
        });
    }

    /**
     * Set up WebSocket message and close handlers
     * @private
     */
    _setupMessageHandler() {
        this._ws.onmessage = (event) => {
            try {
                const message = JSON.parse(event.data);
                this._handleMessage(message);
            } catch (err) {
                console.error('Error parsing signaling message:', err);
            }
        };

        this._ws.onclose = () => {
            const wasConnected = this._state === SignalingState.CONNECTED;
            this._state = SignalingState.DISCONNECTED;
            this._emitter.emit('statechange', this._state);
            this._emitter.emit('disconnected');

            if (wasConnected && this._shouldReconnect) {
                this._scheduleReconnect();
            }
        };

        this._ws.onerror = (err) => {
            this._emitter.emit('error', err);
        };
    }

    /**
     * Handle incoming signaling message
     * @param {Object} message
     * @private
     */
    _handleMessage(message) {
        switch (message.type) {
            case 'joined':
                // Confirmation that we joined the room
                this._emitter.emit('joined', message.room, message.peers || []);
                break;

            case 'peer-joined':
                // Another peer joined the room
                this._emitter.emit('peerjoined', message.peer_id);
                break;

            case 'peer-left':
                // A peer left the room
                this._emitter.emit('peerleft', message.peer_id);
                break;

            case 'offer':
                // Received SDP offer from another peer
                this._emitter.emit('offer', message.from, message.sdp);
                break;

            case 'answer':
                // Received SDP answer from another peer
                this._emitter.emit('answer', message.from, message.sdp);
                break;

            case 'ice-candidate':
                // Received ICE candidate from another peer
                this._emitter.emit('icecandidate', message.from, message.candidate);
                break;

            case 'error':
                // Server error message
                this._emitter.emit('servererror', message.message);
                break;

            default:
                console.warn('Unknown signaling message type:', message.type);
        }
    }

    /**
     * Send join message to server
     * @private
     */
    _sendJoin() {
        this._send({
            type: 'join',
            room: this._roomId,
            peer_id: this._peerId
        });
    }

    /**
     * Send a message through WebSocket
     * @param {Object} message
     * @private
     */
    _send(message) {
        if (this._ws && this._ws.readyState === WebSocket.OPEN) {
            this._ws.send(JSON.stringify(message));
        }
    }

    /**
     * Schedule a reconnection attempt with exponential backoff
     * @private
     */
    _scheduleReconnect() {
        if (this._reconnectAttempts >= this._maxReconnectAttempts) {
            this._emitter.emit('maxreconnectsreached');
            return;
        }

        this._state = SignalingState.RECONNECTING;
        this._emitter.emit('statechange', this._state);
        this._emitter.emit('reconnecting', this._reconnectAttempts + 1, this._currentReconnectDelay);

        this._reconnectTimer = setTimeout(() => {
            this._reconnectAttempts++;
            this._attemptReconnect();
        }, this._currentReconnectDelay);

        // Exponential backoff with jitter
        const jitter = Math.random() * 0.3 + 0.85; // 0.85-1.15
        this._currentReconnectDelay = Math.min(
            this._currentReconnectDelay * this._reconnectMultiplier * jitter,
            this._maxReconnectDelay
        );
    }

    /**
     * Attempt to reconnect
     * @private
     */
    _attemptReconnect() {
        if (!this._shouldReconnect) return;

        this.connect(this._roomId, this._peerId)
            .then(() => {
                this._emitter.emit('reconnected');
            })
            .catch((err) => {
                console.error('Reconnect attempt failed:', err);
                if (this._shouldReconnect) {
                    this._scheduleReconnect();
                }
            });
    }

    /**
     * Send an SDP offer to a specific peer
     * @param {string} targetPeerId - Target peer ID
     * @param {RTCSessionDescriptionInit} sdp - SDP offer
     */
    sendOffer(targetPeerId, sdp) {
        this._send({
            type: 'offer',
            target: targetPeerId,
            sdp: sdp
        });
    }

    /**
     * Send an SDP answer to a specific peer
     * @param {string} targetPeerId - Target peer ID
     * @param {RTCSessionDescriptionInit} sdp - SDP answer
     */
    sendAnswer(targetPeerId, sdp) {
        this._send({
            type: 'answer',
            target: targetPeerId,
            sdp: sdp
        });
    }

    /**
     * Send an ICE candidate to a specific peer
     * @param {string} targetPeerId - Target peer ID
     * @param {RTCIceCandidateInit} candidate - ICE candidate
     */
    sendIceCandidate(targetPeerId, candidate) {
        this._send({
            type: 'ice-candidate',
            target: targetPeerId,
            candidate: candidate
        });
    }

    /**
     * Leave the current room
     */
    leave() {
        if (this._ws && this._ws.readyState === WebSocket.OPEN) {
            this._send({
                type: 'leave',
                room: this._roomId,
                peer_id: this._peerId
            });
        }
        this._roomId = null;
    }

    /**
     * Disconnect from the signaling server
     */
    disconnect() {
        this._shouldReconnect = false;
        if (this._reconnectTimer) {
            clearTimeout(this._reconnectTimer);
            this._reconnectTimer = null;
        }
        if (this._ws) {
            this._ws.close();
            this._ws = null;
        }
        this._state = SignalingState.DISCONNECTED;
        this._emitter.emit('statechange', this._state);
    }

    /**
     * Check if currently connected
     * @returns {boolean}
     */
    isConnected() {
        return this._ws && this._ws.readyState === WebSocket.OPEN;
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
