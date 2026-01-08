// =============================================================================
// Collaboration Initialization
// =============================================================================
//
// Handles collaboration setup for real-time multi-user editing. Creates and
// configures the sync adapter, room manager, and presence broadcasting.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Initialize CppSyncAdapter for CRDT synchronization
// - Create RoomManager for room joining/leaving
// - Set up presence update listeners
// - Wire collaboration components to UI modules
// - Handle auto-join from URL parameters
// - Handle browser history navigation (popstate)
//
// =============================================================================

import type { CellsClient } from "./client";
import type { App } from "./app";
import type { CellEditor } from "./cell-editor";
import type { FormulaBarEditor } from "./header-editor";
import type { PresenceBroadcaster } from "./presence-broadcast";
import type { FileLoader } from "./file-loader";
import { CppSyncAdapter } from "./cpp-sync-adapter";
import { RoomManager, getRoomIdFromUrl, clearRoomIdFromUrl } from "./room-url";

// =============================================================================
// Types
// =============================================================================

/** Configuration for collaboration setup */
export interface CollabConfig {
  app: App;
  cellEditor: CellEditor;
  formulaBarEditor: FormulaBarEditor;
  presenceBroadcaster: PresenceBroadcaster;
  fileLoader: FileLoader;
  renderPresenceOnly: () => void;
}

// =============================================================================
// Collaboration Setup
// =============================================================================

/**
 * Set up collaboration for the application.
 * Returns functions to initialize and check for auto-join.
 */
export function setupCollaboration(config: CollabConfig): {
  initializeCollaboration: (client: CellsClient) => Promise<void>;
  checkAutoJoinRoom: () => Promise<void>;
} {
  const {
    app,
    cellEditor,
    formulaBarEditor,
    presenceBroadcaster,
    fileLoader,
    renderPresenceOnly,
  } = config;

  /**
   * Initialize collaboration subsystem.
   * Creates sync adapter, room manager, and wires up presence.
   */
  async function initializeCollaboration(client: CellsClient): Promise<void> {
    if (app.collaborationInitialized) {
      console.log("Collaboration already initialized");
      return;
    }
    if (app.collaborationInitializing) {
      console.log("Collaboration initialization in progress, waiting...");
      while (app.collaborationInitializing && !app.collaborationInitialized) {
        await new Promise((resolve) => setTimeout(resolve, 50));
      }
      return;
    }
    app.collaborationInitializing = true;
    console.log("Starting collaboration initialization...");

    try {
      // Create C++ sync adapter
      app.syncAdapter = new CppSyncAdapter({ client });

      // Create room manager
      app.roomManager = new RoomManager({ collabManager: app.syncAdapter });

      // Initialize the adapter
      await app.syncAdapter.initialize();

      // Connect UI
      app.collabUI.setCollabManager(app.syncAdapter);
      app.collabUI.setPresenceManager(app.syncAdapter);
      app.collabUI.setRoomManager(app.roomManager);

      // Set up for modules
      cellEditor.setSyncAdapter(app.syncAdapter);
      formulaBarEditor.setSyncAdapter(app.syncAdapter);
      presenceBroadcaster.setSyncAdapter(app.syncAdapter);
      presenceBroadcaster.setCollaborationInitialized(true);

      // Listen for presence updates
      app.syncAdapter.on("presenceupdated", () => {
        presenceBroadcaster.updateRemotePresenceDisplay();
        renderPresenceOnly();
      });
      app.syncAdapter.on("peerleft", () => {
        presenceBroadcaster.updateRemotePresenceDisplay();
        renderPresenceOnly();
      });

      // Start presence render loop
      setInterval(() => {
        if (app.collaborationInitialized && app.syncAdapter) {
          const hadPresence = app.renderer.remotePresence.length > 0;
          presenceBroadcaster.updateRemotePresenceDisplay();
          const hasPresence = app.renderer.remotePresence.length > 0;
          if (hadPresence || hasPresence) {
            renderPresenceOnly();
          }
        }
      }, 50);

      app.collaborationInitialized = true;
      app.collaborationInitializing = false;

      // Expose sync adapter on window for e2e testing
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (window as any)._syncAdapter = app.syncAdapter;

      console.log(
        "Collaboration initialized successfully, peer ID:",
        app.syncAdapter.peerId
      );
    } catch (err) {
      console.error("Failed to initialize collaboration:", err);
      app.collaborationInitializing = false;
      throw err;
    }
  }

  /**
   * Check URL for room ID and auto-join if present.
   */
  async function checkAutoJoinRoom(): Promise<void> {
    const roomIdFromUrl = getRoomIdFromUrl();
    if (roomIdFromUrl && fileLoader.getHasFileLoaded()) {
      console.log("Auto-joining room from URL:", roomIdFromUrl);
      try {
        const client = await fileLoader.ensureWasmClient();
        await initializeCollaboration(client);
        await app.roomManager!.joinRoom(roomIdFromUrl);
      } catch (err) {
        console.error("Failed to auto-join room:", err);
      }
    }
  }

  // Set up collab UI callback
  app.collabUI.setOnInitializeRequest(async () => {
    const client = await fileLoader.ensureWasmClient();
    if (!fileLoader.getHasFileLoaded()) {
      await fileLoader.createEmptyWorkbook();
    }
    await initializeCollaboration(client);
  });

  // Handle browser back/forward
  window.addEventListener("popstate", async () => {
    const roomIdFromUrl = getRoomIdFromUrl();
    const currentRoomId = app.roomManager?.currentRoomId;

    if (
      roomIdFromUrl &&
      roomIdFromUrl !== currentRoomId &&
      fileLoader.getHasFileLoaded()
    ) {
      console.log("Popstate: rejoining room from URL:", roomIdFromUrl);
      try {
        const client = await fileLoader.ensureWasmClient();
        await initializeCollaboration(client);
        await app.roomManager!.joinRoom(roomIdFromUrl);
      } catch (err) {
        console.error("Failed to rejoin room from popstate:", err);
      }
    } else if (!roomIdFromUrl && currentRoomId) {
      console.log("Popstate: leaving room (no room in URL)");
      app.roomManager!.leaveRoom();
    }
  });

  return {
    initializeCollaboration,
    checkAutoJoinRoom,
  };
}

// Re-export for convenience
export { getRoomIdFromUrl, clearRoomIdFromUrl };
