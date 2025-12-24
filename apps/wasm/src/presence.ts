// Presence Utilities
// Helper functions for collaboration presence features (user names, colors)
//
// Note: The full PresenceManager class has been replaced by C++ PresenceManager
// accessed via CppSyncAdapter. This file now only exports utility functions.

import type { PeerId, UserColor } from "./types";

/**
 * Adjectives for random name generation
 */
const ADJECTIVES = [
  "Swift",
  "Happy",
  "Clever",
  "Bold",
  "Bright",
  "Quick",
  "Wise",
  "Noble",
  "Brave",
  "Kind",
] as const;

/**
 * Animals for random name generation
 */
const ANIMALS = [
  "Fox",
  "Bear",
  "Eagle",
  "Wolf",
  "Owl",
  "Lion",
  "Hawk",
  "Deer",
  "Tiger",
  "Panda",
] as const;

/**
 * Color palette for user cursors - distinct colors with good contrast
 * Colors are chosen to work well with white text labels
 */
export const USER_COLORS: readonly UserColor[] = [
  "#E53935", // Red
  "#1E88E5", // Blue
  "#8E24AA", // Purple
  "#0288D1", // Light Blue (replacing teal - too green-ish)
  "#F57C00", // Orange
  "#5E35B1", // Deep Purple
  "#00ACC1", // Cyan
  "#D81B60", // Pink
  "#546E7A", // Blue Gray (avoiding green - too similar to local selection)
  "#6D4C41", // Brown
];

/**
 * Generate a random display name (Adjective + Animal)
 * @returns A randomly generated name
 */
export function generateRandomName(): string {
  const adjective = ADJECTIVES[Math.floor(Math.random() * ADJECTIVES.length)];
  const animal = ANIMALS[Math.floor(Math.random() * ANIMALS.length)];
  return `${adjective} ${animal}`;
}

/**
 * Get a color from the palette based on peer ID
 * Uses a simple hash to consistently assign colors
 * @param peerId - The peer's unique identifier
 * @returns A hex color string
 */
export function getColorForPeer(peerId: PeerId): UserColor {
  let hash = 0;
  for (let i = 0; i < peerId.length; i++) {
    hash = (hash << 5) - hash + peerId.charCodeAt(i);
    hash = hash & hash; // Convert to 32bit integer
  }
  return USER_COLORS[Math.abs(hash) % USER_COLORS.length] ?? USER_COLORS[0]!;
}
