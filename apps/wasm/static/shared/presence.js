// Presence Utilities
// Helper functions for collaboration presence features (user names, colors)
//
// Note: The full PresenceManager class has been replaced by C++ PresenceManager
// accessed via CppSyncAdapter. This file now only exports utility functions.

/**
 * Adjectives for random name generation
 */
const ADJECTIVES = [
    'Swift', 'Happy', 'Clever', 'Bold', 'Bright',
    'Quick', 'Wise', 'Noble', 'Brave', 'Kind'
];

/**
 * Animals for random name generation
 */
const ANIMALS = [
    'Fox', 'Bear', 'Eagle', 'Wolf', 'Owl',
    'Lion', 'Hawk', 'Deer', 'Tiger', 'Panda'
];

/**
 * Color palette for user cursors - distinct colors with good contrast
 * Colors are chosen to work well with white text labels
 */
export const USER_COLORS = [
    '#E53935', // Red
    '#1E88E5', // Blue
    '#8E24AA', // Purple
    '#00897B', // Teal
    '#F57C00', // Orange
    '#5E35B1', // Deep Purple
    '#00ACC1', // Cyan
    '#D81B60', // Pink
    '#43A047', // Green
    '#6D4C41'  // Brown
];

/**
 * Generate a random display name (Adjective + Animal)
 * @returns {string}
 */
export function generateRandomName() {
    const adjective = ADJECTIVES[Math.floor(Math.random() * ADJECTIVES.length)];
    const animal = ANIMALS[Math.floor(Math.random() * ANIMALS.length)];
    return `${adjective} ${animal}`;
}

/**
 * Get a color from the palette based on peer ID
 * Uses a simple hash to consistently assign colors
 * @param {string} peerId
 * @returns {string}
 */
export function getColorForPeer(peerId) {
    let hash = 0;
    for (let i = 0; i < peerId.length; i++) {
        hash = ((hash << 5) - hash) + peerId.charCodeAt(i);
        hash = hash & hash; // Convert to 32bit integer
    }
    return USER_COLORS[Math.abs(hash) % USER_COLORS.length];
}
