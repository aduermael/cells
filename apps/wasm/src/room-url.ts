// =============================================================================
// Room URL Manager
// =============================================================================
//
// URL management for collaboration rooms. Parses room IDs from URL query
// parameters and updates URL without page reload for shareable links.
//
// This is a UI-ONLY module. Room management is purely URL-based; actual
// room joining goes through CppSyncAdapter.
//
// Key responsibilities:
// - Parse room ID from URL (?room=XXXXXXXX)
// - Validate room ID format (8-char base62)
// - Update URL with room ID via history API
// - Clear room ID from URL when leaving
// - Generate unique room IDs for new rooms
// - Handle browser back/forward navigation
//
// URL format:
// - https://example.com/?room=abc12XYZ
// - Room IDs are 8-character alphanumeric strings
//
// =============================================================================

import type { RoomId, RoomManagerCallbacks } from "./types";

/**
 * Parse room ID from URL query parameters
 * @returns Room ID or null if not present
 */
export function getRoomIdFromUrl(): RoomId | null {
  const url = new URL(window.location.href);
  const roomId = url.searchParams.get("room");

  // Validate room ID format (8-char base62)
  if (roomId && isValidRoomId(roomId)) {
    return roomId;
  }

  return null;
}

/**
 * Validate room ID format (8-char alphanumeric)
 * @param roomId - The room ID to validate
 * @returns True if valid
 */
export function isValidRoomId(roomId: unknown): roomId is RoomId {
  if (!roomId || typeof roomId !== "string") return false;
  // Accept 8-char base62 IDs
  return /^[0-9A-Za-z]{8}$/.test(roomId);
}

/**
 * Update URL with room ID without page reload
 * Uses pushState to enable browser back/forward navigation
 * @param roomId - Room ID to add to URL
 */
export function setRoomIdInUrl(roomId: RoomId): void {
  const url = new URL(window.location.href);
  url.searchParams.set("room", roomId);
  window.history.pushState({ roomId }, "", url.toString());
}

/**
 * Remove room ID from URL without page reload
 * Uses pushState to enable browser back/forward navigation
 */
export function clearRoomIdFromUrl(): void {
  const url = new URL(window.location.href);
  url.searchParams.delete("room");
  window.history.pushState({}, "", url.toString());
}

/**
 * Generate a random room ID (8-char base62)
 * @returns A new room ID
 */
export function generateRoomId(): RoomId {
  const chars =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  let id = "";
  for (let i = 0; i < 8; i++) {
    id += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return id;
}

/** Interface for CollabManager-like objects */
interface CollabManagerLike {
  joinRoom(roomId: RoomId): Promise<void>;
  leaveRoom(): void;
}

/**
 * Room join manager - handles auto-join from URL and room state management
 */
export class RoomManager {
  private readonly _collabManager: CollabManagerLike;
  private readonly _onJoining: (roomId: RoomId) => void;
  private readonly _onJoined: (roomId: RoomId) => void;
  private readonly _onError: (error: Error, roomId: RoomId) => void;

  private _isJoining = false;
  private _currentRoomId: RoomId | null = null;

  /**
   * Create a new RoomManager
   * @param options - Configuration options
   */
  constructor(options: RoomManagerCallbacks & { collabManager: CollabManagerLike }) {
    if (!options.collabManager) {
      throw new Error("RoomManager requires collabManager option");
    }

    this._collabManager = options.collabManager;
    this._onJoining = options.onJoining ?? (() => {});
    this._onJoined = options.onJoined ?? (() => {});
    this._onError = options.onError ?? (() => {});
  }

  /**
   * Check URL for room ID and auto-join if present
   * Should be called after CollabManager is initialized
   * @returns True if joined a room from URL
   */
  async checkAndJoinFromUrl(): Promise<boolean> {
    const roomId = getRoomIdFromUrl();
    if (!roomId) {
      return false;
    }

    try {
      await this.joinRoom(roomId);
      return true;
    } catch (err) {
      console.error("Failed to join room from URL:", err);
      this._onError(err instanceof Error ? err : new Error(String(err)), roomId);
      // Clear invalid room from URL
      clearRoomIdFromUrl();
      return false;
    }
  }

  /**
   * Join a room by ID
   * @param roomId - The room to join
   */
  async joinRoom(roomId: RoomId): Promise<void> {
    if (!isValidRoomId(roomId)) {
      throw new Error("Invalid room ID format");
    }

    if (this._isJoining) {
      throw new Error("Already joining a room");
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
   * @returns The new room ID
   */
  async createAndJoinRoom(): Promise<RoomId> {
    const roomId = generateRoomId();
    await this.joinRoom(roomId);
    return roomId;
  }

  /**
   * Leave the current room
   */
  leaveRoom(): void {
    if (this._currentRoomId) {
      this._collabManager.leaveRoom();
      this._currentRoomId = null;
      clearRoomIdFromUrl();
    }
  }

  /**
   * Get the current room ID
   */
  get currentRoomId(): RoomId | null {
    return this._currentRoomId;
  }

  /**
   * Check if currently joining a room
   */
  get isJoining(): boolean {
    return this._isJoining;
  }
}
