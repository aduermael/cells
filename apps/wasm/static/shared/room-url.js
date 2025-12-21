// Room URL Module
// Handles parsing room ID from URL and managing URL state for collaboration

/**
 * Parse room ID from URL query parameters
 * @returns {string|null} Room ID or null if not present
 */
export function getRoomIdFromUrl() {
    const url = new URL(window.location.href);
    const roomId = url.searchParams.get('room');

    // Validate room ID format (8-char base62)
    if (roomId && isValidRoomId(roomId)) {
        return roomId;
    }

    return null;
}

/**
 * Validate room ID format (8-char alphanumeric)
 * @param {string} roomId
 * @returns {boolean}
 */
export function isValidRoomId(roomId) {
    if (!roomId || typeof roomId !== 'string') return false;
    // Accept 8-char base62 IDs
    return /^[0-9A-Za-z]{8}$/.test(roomId);
}

/**
 * Update URL with room ID without page reload
 * @param {string} roomId - Room ID to add to URL
 */
export function setRoomIdInUrl(roomId) {
    const url = new URL(window.location.href);
    url.searchParams.set('room', roomId);
    window.history.replaceState({}, '', url.toString());
}

/**
 * Remove room ID from URL without page reload
 */
export function clearRoomIdFromUrl() {
    const url = new URL(window.location.href);
    url.searchParams.delete('room');
    window.history.replaceState({}, '', url.toString());
}

/**
 * Generate a random room ID (8-char base62)
 * @returns {string}
 */
export function generateRoomId() {
    const chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
    let id = '';
    for (let i = 0; i < 8; i++) {
        id += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return id;
}

/**
 * Room join manager - handles auto-join from URL and room state management
 */
export class RoomManager {
    /**
     * @param {Object} options
     * @param {Object} options.collabManager - CollabManager instance
     * @param {Function} [options.onJoining] - Callback when starting to join
     * @param {Function} [options.onJoined] - Callback when successfully joined
     * @param {Function} [options.onError] - Callback on join error
     */
    constructor(options) {
        if (!options.collabManager) {
            throw new Error('RoomManager requires collabManager option');
        }

        this._collabManager = options.collabManager;
        this._onJoining = options.onJoining || (() => {});
        this._onJoined = options.onJoined || (() => {});
        this._onError = options.onError || (() => {});

        this._isJoining = false;
        this._currentRoomId = null;
    }

    /**
     * Check URL for room ID and auto-join if present
     * Should be called after CollabManager is initialized
     * @returns {Promise<boolean>} True if joined a room from URL
     */
    async checkAndJoinFromUrl() {
        const roomId = getRoomIdFromUrl();
        if (!roomId) {
            return false;
        }

        try {
            await this.joinRoom(roomId);
            return true;
        } catch (err) {
            console.error('Failed to join room from URL:', err);
            this._onError(err, roomId);
            // Clear invalid room from URL
            clearRoomIdFromUrl();
            return false;
        }
    }

    /**
     * Join a room by ID
     * @param {string} roomId
     * @returns {Promise<void>}
     */
    async joinRoom(roomId) {
        if (!isValidRoomId(roomId)) {
            throw new Error('Invalid room ID format');
        }

        if (this._isJoining) {
            throw new Error('Already joining a room');
        }

        this._isJoining = true;
        this._onJoining(roomId);

        try {
            await this._collabManager.joinRoom(roomId);
            this._currentRoomId = roomId;
            setRoomIdInUrl(roomId);
            this._onJoined(roomId);
        } finally {
            this._isJoining = false;
        }
    }

    /**
     * Create and join a new room
     * @returns {Promise<string>} The new room ID
     */
    async createAndJoinRoom() {
        const roomId = generateRoomId();
        await this.joinRoom(roomId);
        return roomId;
    }

    /**
     * Leave the current room
     */
    leaveRoom() {
        if (this._currentRoomId) {
            this._collabManager.leaveRoom();
            this._currentRoomId = null;
            clearRoomIdFromUrl();
        }
    }

    /**
     * Get the current room ID
     * @returns {string|null}
     */
    get currentRoomId() {
        return this._currentRoomId;
    }

    /**
     * Check if currently joining a room
     * @returns {boolean}
     */
    get isJoining() {
        return this._isJoining;
    }
}
