// Operation Protocol Module
// Handles message serialization for CRDT operations over DataChannel

/**
 * Message types for operation protocol
 */
export const MessageType = {
    // Sync protocol messages
    HELLO: 'hello',
    SYNC_REQUEST: 'sync_request',
    SYNC_RESPONSE: 'sync_response',

    // Operation messages
    OPERATIONS: 'operations',

    // Acknowledgment
    ACK: 'ack'
};

/**
 * Maximum batch size for operations
 */
export const MAX_BATCH_SIZE = 100;

/**
 * Create a hello message for initial sync
 * @param {string} peerId - Local peer ID
 * @param {string} documentId - Current document ID
 * @param {string} hlc - Current HLC timestamp
 * @returns {string} JSON-encoded message
 */
export function createHelloMessage(peerId, documentId, hlc) {
    return JSON.stringify({
        type: MessageType.HELLO,
        peer_id: peerId,
        document_id: documentId,
        hlc: hlc
    });
}

/**
 * Create a sync request message
 * @param {string} sinceHlc - HLC timestamp to request operations since
 * @returns {string} JSON-encoded message
 */
export function createSyncRequestMessage(sinceHlc) {
    return JSON.stringify({
        type: MessageType.SYNC_REQUEST,
        since_hlc: sinceHlc
    });
}

/**
 * Create a sync response message with operations
 * @param {Object[]} operations - Array of operation objects
 * @param {boolean} hasMore - Whether there are more operations to send
 * @param {string|null} nextHlc - HLC to request more operations from
 * @returns {string} JSON-encoded message
 */
export function createSyncResponseMessage(operations, hasMore = false, nextHlc = null) {
    return JSON.stringify({
        type: MessageType.SYNC_RESPONSE,
        operations: operations,
        has_more: hasMore,
        next_hlc: nextHlc
    });
}

/**
 * Create an operations batch message
 * @param {Object[]} operations - Array of operation objects
 * @returns {string} JSON-encoded message
 */
export function createOperationsMessage(operations) {
    return JSON.stringify({
        type: MessageType.OPERATIONS,
        batch: operations
    });
}

/**
 * Create a single operation message
 * @param {string} hlc - HLC timestamp
 * @param {string} opType - Operation type (e.g., 'CELL_SET_VALUE')
 * @param {string} targetId - Target entity ID
 * @param {Object} payload - Operation payload
 * @returns {string} JSON-encoded message
 */
export function createSingleOperationMessage(hlc, opType, targetId, payload) {
    return createOperationsMessage([{
        hlc: hlc,
        op: opType,
        target: targetId,
        payload: payload
    }]);
}

/**
 * Create an acknowledgment message
 * @param {string} hlc - HLC of acknowledged operation
 * @returns {string} JSON-encoded message
 */
export function createAckMessage(hlc) {
    return JSON.stringify({
        type: MessageType.ACK,
        hlc: hlc
    });
}

/**
 * Parse a message from DataChannel
 * @param {string} data - Raw message data
 * @returns {Object|null} Parsed message object or null if invalid
 */
export function parseMessage(data) {
    try {
        const message = JSON.parse(data);
        if (!message.type) {
            console.error('Message missing type field');
            return null;
        }
        return message;
    } catch (err) {
        console.error('Error parsing operation message:', err);
        return null;
    }
}

/**
 * Validate an operation object
 * @param {Object} op - Operation to validate
 * @returns {boolean} Whether the operation is valid
 */
export function validateOperation(op) {
    if (!op || typeof op !== 'object') return false;
    if (!op.hlc || typeof op.hlc !== 'string') return false;
    if (!op.op || typeof op.op !== 'string') return false;
    if (!op.target || typeof op.target !== 'string') return false;
    // payload can be null for some operations
    return true;
}

/**
 * Validate a batch of operations
 * @param {Object[]} operations - Array of operations
 * @returns {Object[]} Array of valid operations (invalid ones filtered out)
 */
export function validateOperationBatch(operations) {
    if (!Array.isArray(operations)) return [];
    return operations.filter(validateOperation);
}

/**
 * Check if an HLC timestamp is valid format
 * Format: wall_time.logical.node_id (e.g., "1705312200000.0.N3f8hJ2w")
 * @param {string} hlc - HLC timestamp string
 * @returns {boolean} Whether the HLC is valid
 */
export function isValidHlc(hlc) {
    if (!hlc || typeof hlc !== 'string') return false;
    const parts = hlc.split('.');
    if (parts.length !== 3) return false;
    const wallTime = parseInt(parts[0], 10);
    const logical = parseInt(parts[1], 10);
    if (isNaN(wallTime) || isNaN(logical)) return false;
    if (wallTime < 0 || logical < 0) return false;
    if (!parts[2] || parts[2].length === 0) return false;
    return true;
}

/**
 * Compare two HLC timestamps
 * @param {string} hlc1 - First HLC
 * @param {string} hlc2 - Second HLC
 * @returns {number} -1 if hlc1 < hlc2, 0 if equal, 1 if hlc1 > hlc2
 */
export function compareHlc(hlc1, hlc2) {
    const parts1 = hlc1.split('.');
    const parts2 = hlc2.split('.');

    // Compare wall time
    const wall1 = parseInt(parts1[0], 10);
    const wall2 = parseInt(parts2[0], 10);
    if (wall1 !== wall2) return wall1 < wall2 ? -1 : 1;

    // Compare logical counter
    const log1 = parseInt(parts1[1], 10);
    const log2 = parseInt(parts2[1], 10);
    if (log1 !== log2) return log1 < log2 ? -1 : 1;

    // Compare node ID (lexicographic)
    if (parts1[2] !== parts2[2]) return parts1[2] < parts2[2] ? -1 : 1;

    return 0;
}

/**
 * Sort operations by HLC in ascending order
 * @param {Object[]} operations - Array of operations with hlc field
 * @returns {Object[]} Sorted array (new array, original not modified)
 */
export function sortOperationsByHlc(operations) {
    return [...operations].sort((a, b) => compareHlc(a.hlc, b.hlc));
}

/**
 * Deduplicate operations by HLC (keeps first occurrence)
 * @param {Object[]} operations - Array of operations
 * @returns {Object[]} Deduplicated array
 */
export function deduplicateOperations(operations) {
    const seen = new Set();
    return operations.filter(op => {
        if (seen.has(op.hlc)) return false;
        seen.add(op.hlc);
        return true;
    });
}

/**
 * Check if HLC timestamp is from the future (for security validation)
 * @param {string} hlc - HLC timestamp to check
 * @param {number} maxFutureMs - Maximum allowed future time in ms (default: 5 minutes)
 * @returns {boolean} Whether the HLC is from the future
 */
export function isFutureHlc(hlc, maxFutureMs = 5 * 60 * 1000) {
    const parts = hlc.split('.');
    const wallTime = parseInt(parts[0], 10);
    const now = Date.now();
    return wallTime > now + maxFutureMs;
}

/**
 * Estimate message size in bytes
 * @param {string} message - JSON message string
 * @returns {number} Approximate size in bytes
 */
export function estimateMessageSize(message) {
    // UTF-8 encoding can have multi-byte characters
    // This is a rough estimate
    return new Blob([message]).size;
}

/**
 * Maximum message size (1MB as per plan security considerations)
 */
export const MAX_MESSAGE_SIZE = 1024 * 1024;

/**
 * Check if message exceeds size limit
 * @param {string} message - Message to check
 * @returns {boolean} Whether message is too large
 */
export function isMessageTooLarge(message) {
    return estimateMessageSize(message) > MAX_MESSAGE_SIZE;
}
