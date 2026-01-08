// =============================================================================
// Worker Collaboration Handlers
// =============================================================================
//
// Message handlers for real-time collaboration features in the worker.
// Extracted from worker.ts to separate collaboration concerns.
//
// This is a BRIDGE module. All collaboration state is managed in C++ via CRDT.
// These handlers simply relay commands between the main thread and WASM.
//
// Key responsibilities:
// - CRDT operation handlers (setNodeId, getOperationsSince, applyRemote...)
// - SyncManager handlers (initSyncManager, addPeer, handlePeerMessage...)
// - C++ SyncClient handlers (enableSync, disableSync, getSyncState...)
// - Presence handlers (setSyncCursor, setSyncSelection, getRemotePresences...)
//
// =============================================================================

import type { CellsEngine, WorkerResponse } from "./worker-types.js";

type RespondFn = (response: WorkerResponse, transfer?: Transferable[]) => void;

// ============================================================================
// CRDT Collaboration Handlers
// ============================================================================

export function handleSetNodeId(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { nodeId } = params as { nodeId: string };
    const result = engine.setNodeId(nodeId);
    respond({ type: "nodeIdSet", result });
}

export function handleGetNodeId(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const nodeId = engine.getNodeId();
    respond({ type: "nodeId", nodeId });
}

export function handleGetCurrentHLC(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const hlc = engine.getCurrentHLC();
    respond({ type: "currentHLC", hlc });
}

export function handleGetOperationsSince(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { sinceHLC } = params as { sinceHLC?: string };
    const result = engine.getOperationsSince(sinceHLC || "");
    respond({ type: "operationsSince", result });
}

export function handleApplyRemoteOperation(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { opJson } = params as { opJson: string };
    const result = engine.applyRemoteOperation(opJson);
    respond({ type: "operationApplied", result });
}

export function handleApplyRemoteOperations(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { opsJson } = params as { opsJson: string };
    const result = engine.applyRemoteOperations(opsJson);
    respond({ type: "operationsApplied", result });
}

export function handleGetOpLogSize(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const size = engine.getOpLogSize();
    respond({ type: "opLogSize", size });
}

export function handleHasOperation(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { hlc } = params as { hlc: string };
    const exists = engine.hasOperation(hlc);
    respond({ type: "hasOperation", exists });
}

// ============================================================================
// SyncManager Handlers
// ============================================================================

export function handleInitSyncManager(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.initSyncManager();
    respond({ type: "syncManagerInitialized", result });
}

export function handleAddPeer(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { peerId } = params as { peerId: string };
    const result = engine.addPeer(peerId);
    respond({ type: "peerAdded", result });
}

export function handleRemovePeer(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { peerId } = params as { peerId: string };
    const result = engine.removePeer(peerId);
    respond({ type: "peerRemoved", result });
}

export function handleGetPeerIds(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getPeerIds();
    respond({ type: "peerIds", result });
}

export function handleGetPeerCount(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const count = engine.getPeerCount();
    respond({ type: "peerCount", count });
}

export function handlePeerMessage(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { peerId, messageJson } = params as {
        peerId: string;
        messageJson: string;
    };
    const result = engine.handlePeerMessage(peerId, messageJson);
    respond({ type: "peerMessageHandled", result });
}

export function handleGetOutgoingMessages(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getOutgoingMessages();
    respond({ type: "outgoingMessages", result });
}

export function handleQueueOperationsBroadcast(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.queueOperationsBroadcast();
    respond({ type: "operationsQueued", result });
}

export function handleStartCollaboration(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.startCollaboration();
    respond({ type: "collaborationStarted", result });
}

// ============================================================================
// C++ SyncClient Handlers (P2P WebRTC sync)
// ============================================================================

export function handleEnableSync(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { url, roomId, peerId } = params as {
        url: string;
        roomId: string;
        peerId?: string;
    };
    const result = engine.enableSync(url, roomId, peerId || "");
    respond({ type: "syncEnabled", result });
}

export function handleDisableSync(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.disableSync();
    respond({ type: "syncDisabled", success: true });
}

export function handleGetSyncState(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getSyncState();
    respond({ type: "syncState", result });
}

export function handleIsSyncEnabled(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const enabled = engine.isSyncEnabled();
    respond({ type: "syncEnabled", enabled });
}

export function handleProcessSyncOutgoing(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.processSyncOutgoing();
    respond({ type: "syncOutgoingProcessed", success: true });
}

export function handleProcessSyncPresence(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.processSyncPresence();
    respond({ type: "syncPresenceProcessed", success: true });
}

export function handleBroadcastSyncOperations(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.broadcastSyncOperations();
    respond({ type: "syncOperationsBroadcast", success: true });
}

// ============================================================================
// C++ SyncClient Presence Handlers
// ============================================================================

export function handleSetSyncLocalName(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { name } = params as { name: string };
    engine.setSyncLocalName(name);
    respond({ type: "syncLocalNameSet", success: true });
}

export function handleSetSyncCurrentSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { sheetId } = params as { sheetId: string };
    engine.setSyncCurrentSheet(sheetId);
    respond({ type: "syncCurrentSheetSet", success: true });
}

export function handleSetSyncCursor(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    engine.setSyncCursor(col, row);
    respond({ type: "syncCursorSet", success: true });
}

export function handleClearSyncCursor(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.clearSyncCursor();
    respond({ type: "syncCursorCleared", success: true });
}

export function handleSetSyncSelection(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { startCol, startRow, endCol, endRow } = params as {
        startCol: number;
        startRow: number;
        endCol: number;
        endRow: number;
    };
    engine.setSyncSelection(startCol, startRow, endCol, endRow);
    respond({ type: "syncSelectionSet", success: true });
}

export function handleClearSyncSelection(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.clearSyncSelection();
    respond({ type: "syncSelectionCleared", success: true });
}

export function handleSetSyncMousePosition(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { x, y } = params as { x: number; y: number };
    engine.setSyncMousePosition(x, y);
    respond({ type: "syncMousePositionSet", success: true });
}

export function handleClearSyncMousePosition(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.clearSyncMousePosition();
    respond({ type: "syncMousePositionCleared", success: true });
}

export function handleSetSyncEditing(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row, text } = params as {
        col: number;
        row: number;
        text: string;
    };
    engine.setSyncEditing(col, row, text);
    respond({ type: "syncEditingSet", success: true });
}

export function handleClearSyncEditing(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.clearSyncEditing();
    respond({ type: "syncEditingCleared", success: true });
}

export function handleGetRemotePresences(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getRemotePresences();
    respond({ type: "remotePresences", result });
}
