// =============================================================================
// Presence Broadcaster
// =============================================================================
//
// Broadcasts local presence (cursor, selection, mouse position) to peers
// and interpolates remote presence for smooth rendering.
//
// This is a UI-ONLY module. Presence data is sent through CppSyncAdapter
// to the C++ sync layer via WASM.
//
// Key responsibilities:
// - Broadcast local cursor position when selection changes
// - Broadcast local selection range
// - Throttle mouse position broadcasts (every 200ms)
// - Interpolate remote mouse positions for smooth animation
// - Fade out idle mouse cursors after 3 seconds
// - Transform remote presence data for GridRenderer
//
// Animation:
// - Uses linear interpolation (lerp) for smooth mouse movement
// - Fades cursor opacity after MOUSE_FADE_START ms of no movement
// - Animation runs via requestAnimationFrame loop
//
// =============================================================================

import type { CppSyncAdapter } from "./cpp-sync-adapter";
import type { GridRenderer, RemotePresenceRender } from "./grid-renderer";
import type { Position } from "./types";

// =============================================================================
// Constants
// =============================================================================

/** Interval between mouse position broadcasts (ms) */
const MOUSE_BROADCAST_INTERVAL = 200;

/** Interpolation factor for smooth mouse movement (0-1, higher = faster) */
const MOUSE_LERP_FACTOR = 0.3;

/** Start fading mouse cursor after this many ms of no movement */
const MOUSE_FADE_START = 3000;

/** Duration of mouse cursor fade out (ms) */
const MOUSE_FADE_DURATION = 500;

// =============================================================================
// Types
// =============================================================================

/** Interpolated mouse position with animation state */
interface InterpolatedMouse {
  x: number;
  y: number;
  targetX: number;
  targetY: number;
  lastUpdate: number;
}

// =============================================================================
// PresenceBroadcaster Class
// =============================================================================

/**
 * PresenceBroadcaster manages presence broadcasting and remote presence display.
 *
 * Responsibilities:
 * - Broadcasting local cursor, selection, and mouse position to peers
 * - Throttling mouse position broadcasts with trailing edge
 * - Interpolating remote mouse positions for smooth rendering
 * - Managing mouse cursor fade-out for idle cursors
 */
export class PresenceBroadcaster {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private syncAdapter: CppSyncAdapter | null = null;
  private renderer: GridRenderer;

  // =========================================================================
  // State
  // =========================================================================

  /** Whether collaboration is initialized and ready */
  private collaborationInitialized: boolean = false;

  /** Current active sheet index */
  private activeSheetIndex: number = 0;

  // Mouse broadcast throttling state
  private lastMouseBroadcastTime: number = 0;
  private lastSentMouseX: number | null = null;
  private lastSentMouseY: number | null = null;
  private pendingMouseTimer: ReturnType<typeof setTimeout> | null = null;

  // Mouse interpolation state (peerId -> interpolated position)
  private interpolatedMouse: Map<string, InterpolatedMouse> = new Map();

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(renderer: GridRenderer) {
    this.renderer = renderer;
  }

  // =========================================================================
  // Configuration
  // =========================================================================

  /**
   * Set the sync adapter for broadcasting
   */
  setSyncAdapter(adapter: CppSyncAdapter | null): void {
    this.syncAdapter = adapter;
  }

  /**
   * Set whether collaboration is initialized
   */
  setCollaborationInitialized(initialized: boolean): void {
    this.collaborationInitialized = initialized;
  }

  /**
   * Set the active sheet index
   */
  setActiveSheetIndex(index: number): void {
    this.activeSheetIndex = index;
  }

  // =========================================================================
  // Local Presence Broadcasting
  // =========================================================================

  /**
   * Broadcast local cursor and selection state to peers
   */
  broadcastLocalPresence(
    selectedCell: Position | null,
    selectionStart: Position | null,
    selectionEnd: Position | null
  ): void {
    if (!this.syncAdapter || !this.collaborationInitialized) return;

    // Update current sheet (use index as identifier)
    this.syncAdapter.setCurrentSheet(String(this.activeSheetIndex));

    // Update cursor position (anchor cell of selection)
    const cursorCell = selectionStart || selectedCell;
    if (cursorCell) {
      this.syncAdapter.setCursor(cursorCell.col, cursorCell.row);
    }

    // Update selection range if different from cursor
    if (selectionStart && selectionEnd) {
      this.syncAdapter.setSelection(
        { col: selectionStart.col, row: selectionStart.row },
        { col: selectionEnd.col, row: selectionEnd.row }
      );
    }
  }

  /**
   * Broadcast mouse position (throttled with trailing edge to capture final position)
   */
  broadcastMousePosition(x: number, y: number): void {
    if (!this.syncAdapter || !this.collaborationInitialized) return;

    // Skip if position hasn't changed from last sent
    if (x === this.lastSentMouseX && y === this.lastSentMouseY) return;

    const now = Date.now();

    // Clear any pending trailing timer
    if (this.pendingMouseTimer) {
      clearTimeout(this.pendingMouseTimer);
      this.pendingMouseTimer = null;
    }

    if (now - this.lastMouseBroadcastTime >= MOUSE_BROADCAST_INTERVAL) {
      // Enough time passed - send immediately
      this.syncAdapter.setMousePosition(x, y);
      this.lastMouseBroadcastTime = now;
      this.lastSentMouseX = x;
      this.lastSentMouseY = y;
    } else {
      // Schedule trailing edge send for final position
      const delay = MOUSE_BROADCAST_INTERVAL - (now - this.lastMouseBroadcastTime);
      this.pendingMouseTimer = setTimeout(() => {
        if (x !== this.lastSentMouseX || y !== this.lastSentMouseY) {
          this.syncAdapter?.setMousePosition(x, y);
          this.lastMouseBroadcastTime = Date.now();
          this.lastSentMouseX = x;
          this.lastSentMouseY = y;
        }
        this.pendingMouseTimer = null;
      }, delay);
    }
  }

  /**
   * Flush pending mouse position (call when mouse leaves or becomes idle)
   */
  flushPendingMousePosition(): void {
    // Cancel pending timer - no need to send if we're flushing
    if (this.pendingMouseTimer) {
      clearTimeout(this.pendingMouseTimer);
      this.pendingMouseTimer = null;
    }
  }

  /**
   * Clear mouse position when leaving canvas
   */
  clearMousePosition(): void {
    if (this.syncAdapter && this.collaborationInitialized) {
      this.syncAdapter.clearMousePosition();
    }
    // Cancel pending timer
    if (this.pendingMouseTimer) {
      clearTimeout(this.pendingMouseTimer);
      this.pendingMouseTimer = null;
    }
    this.lastSentMouseX = null;
    this.lastSentMouseY = null;
  }

  // =========================================================================
  // Remote Presence Display
  // =========================================================================

  /**
   * Update renderer's remote presence display from syncAdapter
   * Handles mouse position interpolation and fade-out
   */
  updateRemotePresenceDisplay(): void {
    if (!this.syncAdapter) {
      this.renderer.remotePresence = [];
      this.interpolatedMouse.clear();
      return;
    }

    const currentSheetId = String(this.activeSheetIndex);
    const allPeers = this.syncAdapter.getRemotePeers();
    const peers = this.syncAdapter.getPeersOnSheet(currentSheetId);
    const now = Date.now();

    // Debug: log remote presence data occasionally
    // if (allPeers.size > 0 && Math.random() < 0.01) {
    //   console.debug(
    //     "[Presence Debug] currentSheet:",
    //     currentSheetId,
    //     "allPeers:",
    //     Array.from(allPeers.entries()).map(([id, p]) => ({
    //       id,
    //       sheet: p.sheet_id,
    //       cursor: p.cursor,
    //     })),
    //     "peersOnSheet:",
    //     peers.length
    //   );
    // }

    const activePeerIds = new Set<string>();

    this.renderer.remotePresence = peers.map((presence) => {
      const peerId = presence.peer_id;
      activePeerIds.add(peerId);

      // Lerp mouse position for smooth movement and track last update time
      let smoothMouse: { x: number; y: number } | null = null;
      let mouseOpacity = 0;

      if (presence.mouse && presence.mouse.x !== undefined) {
        const targetX = presence.mouse.x;
        const targetY = presence.mouse.y;

        if (this.interpolatedMouse.has(peerId)) {
          const current = this.interpolatedMouse.get(peerId)!;
          // Check if mouse position changed
          const moved =
            Math.abs(targetX - current.targetX) > 0.5 ||
            Math.abs(targetY - current.targetY) > 0.5;
          if (moved) {
            current.lastUpdate = now;
            current.targetX = targetX;
            current.targetY = targetY;
          }
          // Lerp toward target
          current.x += (targetX - current.x) * MOUSE_LERP_FACTOR;
          current.y += (targetY - current.y) * MOUSE_LERP_FACTOR;
          smoothMouse = { x: current.x, y: current.y };

          // Calculate opacity based on time since last movement
          mouseOpacity = this.calculateMouseOpacity(now - current.lastUpdate);
        } else {
          // First time seeing this peer's mouse - start at target
          const newPos: InterpolatedMouse = {
            x: targetX,
            y: targetY,
            targetX,
            targetY,
            lastUpdate: now,
          };
          this.interpolatedMouse.set(peerId, newPos);
          smoothMouse = { x: targetX, y: targetY };
          mouseOpacity = 1.0;
        }
      } else if (this.interpolatedMouse.has(peerId)) {
        // No new mouse data, but we have a cached position - use it with fade
        const current = this.interpolatedMouse.get(peerId)!;
        smoothMouse = { x: current.x, y: current.y };

        // Calculate opacity based on time since last movement
        mouseOpacity = this.calculateMouseOpacity(now - current.lastUpdate);
      }

      return {
        peerId: peerId,
        name: presence.name,
        color: presence.color,
        cursor: presence.cursor,
        selection: presence.selection,
        mouse: smoothMouse,
        mouseOpacity: mouseOpacity,
        editing: presence.editing, // Ephemeral editing text
      } as RemotePresenceRender;
    });

    // Clean up interpolated positions for peers no longer present
    for (const peerId of this.interpolatedMouse.keys()) {
      if (!activePeerIds.has(peerId)) {
        this.interpolatedMouse.delete(peerId);
      }
    }
  }

  // =========================================================================
  // Private Helpers
  // =========================================================================

  /**
   * Calculate mouse cursor opacity based on time since last movement
   */
  private calculateMouseOpacity(timeSinceMove: number): number {
    if (timeSinceMove < MOUSE_FADE_START) {
      return 1.0;
    } else if (timeSinceMove < MOUSE_FADE_START + MOUSE_FADE_DURATION) {
      return 1.0 - (timeSinceMove - MOUSE_FADE_START) / MOUSE_FADE_DURATION;
    } else {
      return 0;
    }
  }
}
