// ICE Server Configuration Module
// Configures STUN/TURN servers for WebRTC NAT traversal

/**
 * Default public STUN servers for NAT traversal
 * These are free, reliable STUN servers for discovering public IP addresses
 */
export const DEFAULT_STUN_SERVERS = [
    { urls: 'stun:stun.l.google.com:19302' },
    { urls: 'stun:stun1.l.google.com:19302' },
    { urls: 'stun:stun2.l.google.com:19302' },
    { urls: 'stun:stun.cloudflare.com:3478' }
];

/**
 * Placeholder for custom TURN server configuration
 * TURN servers are needed for relaying traffic when direct P2P fails
 *
 * To configure a TURN server, add entries like:
 * {
 *     urls: 'turn:turn.example.com:3478',
 *     username: 'user',
 *     credential: 'password'
 * }
 *
 * Or for TURNS (TLS):
 * {
 *     urls: 'turns:turn.example.com:443',
 *     username: 'user',
 *     credential: 'password'
 * }
 */
let customTurnServers = [];

/**
 * Whether to use TURN servers
 * Set to true when custom TURN servers are configured
 */
let turnEnabled = false;

/**
 * Configure custom TURN servers
 * @param {RTCIceServer[]} servers - Array of TURN server configurations
 */
export function setTurnServers(servers) {
    customTurnServers = servers || [];
    turnEnabled = customTurnServers.length > 0;
}

/**
 * Add a single TURN server
 * @param {string} url - TURN server URL (e.g., 'turn:turn.example.com:3478')
 * @param {string} [username] - Username for authentication
 * @param {string} [credential] - Credential for authentication
 */
export function addTurnServer(url, username, credential) {
    const server = { urls: url };
    if (username) server.username = username;
    if (credential) server.credential = credential;
    customTurnServers.push(server);
    turnEnabled = true;
}

/**
 * Clear all custom TURN servers
 */
export function clearTurnServers() {
    customTurnServers = [];
    turnEnabled = false;
}

/**
 * Check if TURN servers are configured
 * @returns {boolean}
 */
export function hasTurnServers() {
    return turnEnabled;
}

/**
 * Get the complete ICE server configuration
 * Combines STUN and TURN servers for WebRTC
 * @param {Object} [options] - Configuration options
 * @param {boolean} [options.stunOnly] - Use only STUN servers (ignore TURN)
 * @param {boolean} [options.turnOnly] - Use only TURN servers (for relay-only mode)
 * @returns {RTCIceServer[]}
 */
export function getIceServers(options = {}) {
    if (options.turnOnly && turnEnabled) {
        return [...customTurnServers];
    }

    if (options.stunOnly || !turnEnabled) {
        return [...DEFAULT_STUN_SERVERS];
    }

    // Combine STUN and TURN servers
    return [...DEFAULT_STUN_SERVERS, ...customTurnServers];
}

/**
 * Get RTCConfiguration object for peer connections
 * @param {Object} [options] - Configuration options
 * @param {boolean} [options.stunOnly] - Use only STUN servers
 * @param {boolean} [options.turnOnly] - Use only TURN servers
 * @param {number} [options.iceCandidatePoolSize] - ICE candidate pool size (default: 10)
 * @param {string} [options.iceTransportPolicy] - 'all' or 'relay' (default: 'all')
 * @returns {RTCConfiguration}
 */
export function getRTCConfiguration(options = {}) {
    const config = {
        iceServers: getIceServers(options),
        iceCandidatePoolSize: options.iceCandidatePoolSize || 10
    };

    // Force relay-only if specified
    if (options.turnOnly || options.iceTransportPolicy === 'relay') {
        config.iceTransportPolicy = 'relay';
    }

    return config;
}

/**
 * Load ICE configuration from URL query parameters
 * Supports: ?turn=url&turn_user=user&turn_pass=pass
 */
export function loadFromUrlParams() {
    if (typeof window === 'undefined' || !window.location) return;

    const params = new URLSearchParams(window.location.search);
    const turnUrl = params.get('turn');
    const turnUser = params.get('turn_user');
    const turnPass = params.get('turn_pass');

    if (turnUrl) {
        addTurnServer(turnUrl, turnUser, turnPass);
    }
}

/**
 * Load ICE configuration from localStorage
 * Key: 'cells.ice.turn_servers' = JSON array of server configs
 */
export function loadFromLocalStorage() {
    if (typeof localStorage === 'undefined') return;

    try {
        const stored = localStorage.getItem('cells.ice.turn_servers');
        if (stored) {
            const servers = JSON.parse(stored);
            if (Array.isArray(servers)) {
                setTurnServers(servers);
            }
        }
    } catch (err) {
        console.warn('Failed to load ICE config from localStorage:', err);
    }
}

/**
 * Save current TURN configuration to localStorage
 */
export function saveToLocalStorage() {
    if (typeof localStorage === 'undefined') return;

    try {
        if (customTurnServers.length > 0) {
            localStorage.setItem('cells.ice.turn_servers', JSON.stringify(customTurnServers));
        } else {
            localStorage.removeItem('cells.ice.turn_servers');
        }
    } catch (err) {
        console.warn('Failed to save ICE config to localStorage:', err);
    }
}

/**
 * Test if STUN servers are reachable
 * Creates a temporary peer connection to gather ICE candidates
 * @param {number} [timeout=5000] - Timeout in milliseconds
 * @returns {Promise<boolean>} Whether STUN is reachable
 */
export async function testStunConnectivity(timeout = 5000) {
    return new Promise((resolve) => {
        const pc = new RTCPeerConnection({
            iceServers: DEFAULT_STUN_SERVERS
        });

        let resolved = false;
        const cleanup = () => {
            if (!resolved) {
                resolved = true;
                pc.close();
            }
        };

        const timer = setTimeout(() => {
            cleanup();
            resolve(false);
        }, timeout);

        pc.onicecandidate = (event) => {
            if (event.candidate && event.candidate.type === 'srflx') {
                // Got a server-reflexive candidate (STUN worked)
                clearTimeout(timer);
                cleanup();
                resolve(true);
            }
        };

        pc.onicegatheringstatechange = () => {
            if (pc.iceGatheringState === 'complete') {
                clearTimeout(timer);
                cleanup();
                resolve(false); // Gathering complete but no srflx candidate
            }
        };

        // Create a data channel to trigger ICE gathering
        pc.createDataChannel('test');
        pc.createOffer()
            .then(offer => pc.setLocalDescription(offer))
            .catch(() => {
                clearTimeout(timer);
                cleanup();
                resolve(false);
            });
    });
}

/**
 * Test if TURN server is reachable and authenticated
 * @param {RTCIceServer} turnServer - TURN server configuration
 * @param {number} [timeout=5000] - Timeout in milliseconds
 * @returns {Promise<boolean>} Whether TURN is reachable
 */
export async function testTurnConnectivity(turnServer, timeout = 5000) {
    return new Promise((resolve) => {
        const pc = new RTCPeerConnection({
            iceServers: [turnServer],
            iceTransportPolicy: 'relay'
        });

        let resolved = false;
        const cleanup = () => {
            if (!resolved) {
                resolved = true;
                pc.close();
            }
        };

        const timer = setTimeout(() => {
            cleanup();
            resolve(false);
        }, timeout);

        pc.onicecandidate = (event) => {
            if (event.candidate && event.candidate.type === 'relay') {
                // Got a relay candidate (TURN worked)
                clearTimeout(timer);
                cleanup();
                resolve(true);
            }
        };

        pc.onicegatheringstatechange = () => {
            if (pc.iceGatheringState === 'complete') {
                clearTimeout(timer);
                cleanup();
                resolve(false);
            }
        };

        // Create a data channel to trigger ICE gathering
        pc.createDataChannel('test');
        pc.createOffer()
            .then(offer => pc.setLocalDescription(offer))
            .catch(() => {
                clearTimeout(timer);
                cleanup();
                resolve(false);
            });
    });
}

/**
 * Initialize ICE configuration from available sources
 * Loads from localStorage first, then URL params (URL takes precedence)
 */
export function initialize() {
    loadFromLocalStorage();
    loadFromUrlParams();
}
