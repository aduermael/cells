// =============================================================================
// Cells Worker
// =============================================================================
//
// Web Worker that runs the spreadsheet engine (WASM module) in a background
// thread. Provides the same API semantics as the REST server but via postMessage.
//
// This is the BRIDGE between UI and C++ core. All spreadsheet operations are
// executed here via the WASM module, with results sent back to the main thread.
//
// Key responsibilities:
// - Load and initialize the Emscripten-compiled WASM module
// - Route messages to appropriate handlers (core or collab)
// - Manage agent streaming for AI features
// - Handle worker lifecycle and pending messages
//
// Architecture:
// - Single CellsEngine instance per worker
// - JSON-based message protocol with request/response IDs
// - Listener callback for push notifications (cell changes, sync events)
// - WASM memory management for binary file operations
//
// Split into modules:
// - worker-types.ts: Type definitions shared across worker modules
// - worker-handlers.ts: Core spreadsheet operation handlers
// - worker-collab.ts: Collaboration/sync operation handlers
//
// =============================================================================

import type {
    CellsModule,
    CellsEngine,
    WorkerRequest,
    WorkerResponse,
} from "./worker-types.js";

// Core spreadsheet handlers
import {
    handleLoad,
    handleExport,
    handleGetSheetInfo,
    handleQueryViewport,
    handleSetActiveSheet,
    handleGetSheets,
    handleAddSheet,
    handleDeleteSheet,
    handleRenameSheet,
    handleMoveSheet,
    handleSetFreezePanes,
    handleUpdateCell,
    handleUpdateCellWithFormatDetection,
    handleCreateCell,
    handleGetOrCreateCellAt,
    handleDeleteCell,
    handleDeleteCellAt,
    handleSetCellFormat,
    handleSetCellFormatAt,
    handleGetAvailableFormats,
    handleCreateCustomFormat,
    handleGetFormulaFunctions,
    handleGetNamedRanges,
    handleGetCellFormatId,
    handleParseUserInputValue,
    handleFormatCellValue,
    handleFormatWithCode,
    handleFormatCellById,
    handleGetFormatDetails,
    handleMakeFormatId,
    handleSetCellStyle,
    handleSetCellStyleAt,
    handleGetCellStyle,
    handleGetCellStyleAt,
    handleCreateStyle,
    handleGetAvailableStyles,
    handleSetRangeStyle,
    handleRemoveRangeStyle,
    handleGetEffectiveCellStyle,
    handleGetEffectiveStyleForRange,
    handleResizeColumn,
    handleResizeColumnByPos,
    handleResizeRow,
    handleResizeRowByPos,
    handleRenameColumn,
    handleRenameColumnByPos,
    handleMoveColumn,
    handleMoveRow,
    handleShiftColumnsForEmptyMove,
    handleShiftRowsForEmptyMove,
    handleInsertColumnAt,
    handleInsertRowAt,
    handleDeleteColumnById,
    handleDeleteRowById,
    handleFillRange,
    handleAddMergeRange,
    handleRemoveMergeRange,
    handleHasFormulas,
    handleCreateEmpty,
    handleSetWorkbookName,
    handleGetWorkbookName,
    handleDebugParseFormula,
    handleGetColumnPixelOffset,
    handleGetRowPixelOffset,
    handleGetTotalWidth,
    handleGetTotalHeight,
    handleValidateFormula,
    handleGetFormulaDisplay,
    handleGetCellDependencies,
    handleGetCellDependents,
    handleGetFormulaReferences,
    handleGetReferencesFromPartial,
    handleDetectCircularRef,
    handleGetVolatileCells,
    handleExecuteScript,
    handleTokenizeLuau,
    handleGetAutocomplete,
    handleGetSpillRangeAt,
    handleInitAgent,
    handleIsAgentInitialized,
    handleGetAgentConversationId,
    handleClearAgentConversation,
    handleCancelAgent,
    handleIsAgentProcessing,
} from "./worker-handlers.js";

// Collaboration handlers
import {
    handleSetNodeId,
    handleGetNodeId,
    handleGetCurrentHLC,
    handleGetOperationsSince,
    handleApplyRemoteOperation,
    handleApplyRemoteOperations,
    handleGetOpLogSize,
    handleHasOperation,
    handleInitSyncManager,
    handleAddPeer,
    handleRemovePeer,
    handleGetPeerIds,
    handleGetPeerCount,
    handlePeerMessage,
    handleGetOutgoingMessages,
    handleQueueOperationsBroadcast,
    handleStartCollaboration,
    handleEnableSync,
    handleDisableSync,
    handleGetSyncState,
    handleIsSyncEnabled,
    handleProcessSyncOutgoing,
    handleProcessSyncPresence,
    handleBroadcastSyncOperations,
    handleSetSyncLocalName,
    handleSetSyncCurrentSheet,
    handleSetSyncCursor,
    handleClearSyncCursor,
    handleSetSyncSelection,
    handleClearSyncSelection,
    handleSetSyncMousePosition,
    handleClearSyncMousePosition,
    handleSetSyncEditing,
    handleClearSyncEditing,
    handleGetRemotePresences,
} from "./worker-collab.js";

// Use the global self from DedicatedWorkerGlobalScope
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const workerSelf = self as unknown as DedicatedWorkerGlobalScope;

/** Factory function type for WASM module initialization */
declare function createCellsModule(): Promise<CellsModule>;

// ============================================================================
// Module State
// ============================================================================

let Module: CellsModule | null = null;
let engine: CellsEngine | null = null;
let isReady = false;
const pendingMessages: WorkerRequest[] = [];

// ============================================================================
// Module Initialization
// ============================================================================

async function initModule(): Promise<void> {
    try {
        // Import the Emscripten-generated module
        // Using importScripts for classic workers, or dynamic import for module workers
        if (typeof importScripts === "function") {
            // Classic worker - use importScripts
            importScripts("./cells_wasm_bin.js");
            Module = await createCellsModule();
        } else {
            // Module worker - use dynamic import
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const module = (await import("./cells_wasm_bin.js" as any)) as {
                default: () => Promise<CellsModule>;
            };
            Module = await module.default();
        }

        // Create the engine instance (Module is guaranteed non-null here)
        engine = new (Module as CellsModule).CellsEngine();

        // Register listener for change notifications from WASM
        // This sends unsolicited messages to the main thread when data changes
        // The callback receives (changeType, data?) where data is optional extra info
        engine.setListener((changeType: string, data?: string) => {
            if (changeType === "load_progress" && data) {
                // Parse progress data: "cellsLoaded:totalEstimate"
                const [loaded, total] = data.split(":").map(Number);
                workerSelf.postMessage({
                    type: "loadProgress",
                    cellsLoaded: loaded,
                    totalEstimate: total,
                });
            } else {
                workerSelf.postMessage({
                    type: "dataChanged",
                    changeType: changeType, // 'cell', 'structure', 'sheet', or 'loaded'
                });
            }
        });

        // Register agent listener for AI events
        // The agent callback receives (eventType, data) where data is JSON or text
        // Note: engine is guaranteed non-null here since we're inside the init callback
        const eng = engine!;
        eng.setAgentListener((eventType: string, data: string) => {
            // Handle special event to send tool results
            if (eventType === "send_tool_result") {
                const toolResult = JSON.parse(data);
                const serverUrl = eng.getAgentServerUrl();
                if (serverUrl != null) {
                    streamAgentMessage(serverUrl + "/api/agent/tool-result", {
                        conversation_id: eng.getAgentConversationId(),
                        tool_use_id: toolResult.tool_use_id,
                        result: toolResult.result,
                        is_error: toolResult.is_error,
                    });
                }
                return;
            }

            workerSelf.postMessage({
                type: "agentEvent",
                eventType: eventType, // 'text', 'tool_use', 'tool_result_needed', 'done', 'error'
                data: data,
            });
        });

        isReady = true;

        // Notify main thread that we're ready
        workerSelf.postMessage({ type: "ready" });

        // Process any messages that arrived before we were ready
        for (const msg of pendingMessages) {
            handleMessage(msg);
        }
        pendingMessages.length = 0;
    } catch (err) {
        const error = err instanceof Error ? err.message : String(err);
        workerSelf.postMessage({
            type: "error",
            error: "Failed to initialize WASM module: " + error,
        });
    }
}

// ============================================================================
// Agent Streaming Fetch
// ============================================================================

// Perform a streaming fetch to the agent server and feed chunks to C++
async function streamAgentMessage(
    url: string,
    body: Record<string, unknown>,
): Promise<void> {
    const isToolResult = url.includes("/tool-result");

    try {
        const response = await fetch(url, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(body),
        });

        if (!response.ok) {
            // Provide more context for tool result failures
            if (isToolResult) {
                const convId = body.conversation_id || "unknown";
                const toolId = body.tool_use_id || "unknown";
                console.error(
                    `[Agent] Tool result failed: HTTP ${response.status} for conversation=${convId}, tool=${toolId}`,
                );

                // Try to read error body for more details
                let errorDetail = response.statusText;
                try {
                    const errorText = await response.text();
                    if (errorText) errorDetail = errorText;
                } catch {
                    // Ignore error reading body
                }

                throw new Error(
                    `Failed to send tool result (${response.status}): ${errorDetail}. The AI session may have been lost - please try your request again.`,
                );
            }
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const reader = response.body?.getReader();
        if (!reader) {
            throw new Error("ReadableStream not supported");
        }

        const decoder = new TextDecoder();

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;

            // Feed raw SSE data to C++ for parsing
            const chunk = decoder.decode(value, { stream: true });
            engine?.feedAgentStreamData(chunk);
        }

        // Signal stream end
        engine?.endAgentStream();
    } catch (err) {
        const error = err instanceof Error ? err.message : String(err);
        console.error(
            `[Agent] Stream error (isToolResult=${isToolResult}):`,
            error,
        );
        engine?.errorAgentStream(error);
    }
}

// ============================================================================
// Message Handler - Routes to appropriate handler module
// ============================================================================

function handleMessage(msg: WorkerRequest): void {
    const { id, type, ...params } = msg;

    // Wrap response with request ID for correlation
    function respond(
        response: WorkerResponse,
        transfer?: Transferable[],
    ): void {
        workerSelf.postMessage(
            { id, ...response },
            transfer ? { transfer } : undefined,
        );
    }

    // Ensure engine is available
    if (!engine || !Module) {
        respond({ type: "error", error: "Engine not initialized" });
        return;
    }

    try {
        // Route to appropriate handler based on message type
        switch (type) {
            // ================================================================
            // File Operations
            // ================================================================
            case "load":
                handleLoad(engine, Module, params, respond);
                break;
            case "export":
                handleExport(engine, Module, params, respond);
                break;

            // ================================================================
            // Sheet Operations
            // ================================================================
            case "getSheetInfo":
                handleGetSheetInfo(engine, params, respond);
                break;
            case "queryViewport":
                handleQueryViewport(engine, params, respond);
                break;
            case "setActiveSheet":
                handleSetActiveSheet(engine, params, respond);
                break;
            case "getSheets":
                handleGetSheets(engine, params, respond);
                break;
            case "addSheet":
                handleAddSheet(engine, params, respond);
                break;
            case "deleteSheet":
                handleDeleteSheet(engine, params, respond);
                break;
            case "renameSheet":
                handleRenameSheet(engine, params, respond);
                break;
            case "moveSheet":
                handleMoveSheet(engine, params, respond);
                break;
            case "setFreezePanes":
                handleSetFreezePanes(engine, params, respond);
                break;

            // ================================================================
            // Cell Operations
            // ================================================================
            case "updateCell":
                handleUpdateCell(engine, params, respond);
                break;
            case "updateCellWithFormatDetection":
                handleUpdateCellWithFormatDetection(engine, params, respond);
                break;
            case "createCell":
                handleCreateCell(engine, params, respond);
                break;
            case "getOrCreateCellAt":
                handleGetOrCreateCellAt(engine, params, respond);
                break;
            case "deleteCell":
                handleDeleteCell(engine, params, respond);
                break;
            case "deleteCellAt":
                handleDeleteCellAt(engine, params, respond);
                break;

            // ================================================================
            // Format Operations
            // ================================================================
            case "setCellFormat":
                handleSetCellFormat(engine, params, respond);
                break;
            case "setCellFormatAt":
                handleSetCellFormatAt(engine, params, respond);
                break;
            case "getAvailableFormats":
                handleGetAvailableFormats(engine, params, respond);
                break;
            case "createCustomFormat":
                handleCreateCustomFormat(engine, params, respond);
                break;
            case "getFormulaFunctions":
                handleGetFormulaFunctions(engine, params, respond);
                break;
            case "getNamedRanges":
                handleGetNamedRanges(engine, params, respond);
                break;
            case "getCellFormatId":
                handleGetCellFormatId(engine, params, respond);
                break;
            case "parseUserInputValue":
                handleParseUserInputValue(engine, params, respond);
                break;
            case "formatCellValue":
                handleFormatCellValue(engine, params, respond);
                break;
            case "formatWithCode":
                handleFormatWithCode(engine, params, respond);
                break;
            case "formatCellById":
                handleFormatCellById(engine, params, respond);
                break;
            case "getFormatDetails":
                handleGetFormatDetails(engine, params, respond);
                break;
            case "makeFormatId":
                handleMakeFormatId(engine, params, respond);
                break;

            // ================================================================
            // Cell Style Operations
            // ================================================================
            case "setCellStyle":
                handleSetCellStyle(engine, params, respond);
                break;
            case "setCellStyleAt":
                handleSetCellStyleAt(engine, params, respond);
                break;
            case "getCellStyle":
                handleGetCellStyle(engine, params, respond);
                break;
            case "getCellStyleAt":
                handleGetCellStyleAt(engine, params, respond);
                break;
            case "createStyle":
                handleCreateStyle(engine, params, respond);
                break;
            case "getAvailableStyles":
                handleGetAvailableStyles(engine, params, respond);
                break;

            // ================================================================
            // Range Style Operations
            // ================================================================
            case "setRangeStyle":
                handleSetRangeStyle(engine, params, respond);
                break;
            case "removeRangeStyle":
                handleRemoveRangeStyle(engine, params, respond);
                break;

            // ================================================================
            // Effective Style Operations
            // ================================================================
            case "getEffectiveCellStyle":
                handleGetEffectiveCellStyle(engine, params, respond);
                break;
            case "getEffectiveStyleForRange":
                handleGetEffectiveStyleForRange(engine, params, respond);
                break;

            // ================================================================
            // Column/Row Operations
            // ================================================================
            case "resizeColumn":
                handleResizeColumn(engine, params, respond);
                break;
            case "resizeColumnByPos":
                handleResizeColumnByPos(engine, params, respond);
                break;
            case "resizeRow":
                handleResizeRow(engine, params, respond);
                break;
            case "resizeRowByPos":
                handleResizeRowByPos(engine, params, respond);
                break;
            case "renameColumn":
                handleRenameColumn(engine, params, respond);
                break;
            case "renameColumnByPos":
                handleRenameColumnByPos(engine, params, respond);
                break;
            case "moveColumn":
                handleMoveColumn(engine, params, respond);
                break;
            case "moveRow":
                handleMoveRow(engine, params, respond);
                break;
            case "shiftColumnsForEmptyMove":
                handleShiftColumnsForEmptyMove(engine, params, respond);
                break;
            case "shiftRowsForEmptyMove":
                handleShiftRowsForEmptyMove(engine, params, respond);
                break;
            case "insertColumnAt":
                handleInsertColumnAt(engine, params, respond);
                break;
            case "insertRowAt":
                handleInsertRowAt(engine, params, respond);
                break;
            case "deleteColumnById":
                handleDeleteColumnById(engine, params, respond);
                break;
            case "deleteRowById":
                handleDeleteRowById(engine, params, respond);
                break;
            case "fillRange":
                handleFillRange(engine, params, respond);
                break;
            case "addMergeRange":
                handleAddMergeRange(engine, params, respond);
                break;
            case "removeMergeRange":
                handleRemoveMergeRange(engine, params, respond);
                break;

            // ================================================================
            // Workbook Operations
            // ================================================================
            case "hasFormulas":
                handleHasFormulas(engine, params, respond);
                break;
            case "createEmpty":
                handleCreateEmpty(engine, params, respond);
                break;
            case "setWorkbookName":
                handleSetWorkbookName(engine, params, respond);
                break;
            case "getWorkbookName":
                handleGetWorkbookName(engine, params, respond);
                break;

            // ================================================================
            // CRDT Collaboration
            // ================================================================
            case "setNodeId":
                handleSetNodeId(engine, params, respond);
                break;
            case "getNodeId":
                handleGetNodeId(engine, params, respond);
                break;
            case "getCurrentHLC":
                handleGetCurrentHLC(engine, params, respond);
                break;
            case "getOperationsSince":
                handleGetOperationsSince(engine, params, respond);
                break;
            case "applyRemoteOperation":
                handleApplyRemoteOperation(engine, params, respond);
                break;
            case "applyRemoteOperations":
                handleApplyRemoteOperations(engine, params, respond);
                break;
            case "getOpLogSize":
                handleGetOpLogSize(engine, params, respond);
                break;
            case "hasOperation":
                handleHasOperation(engine, params, respond);
                break;

            // ================================================================
            // SyncManager
            // ================================================================
            case "initSyncManager":
                handleInitSyncManager(engine, params, respond);
                break;
            case "addPeer":
                handleAddPeer(engine, params, respond);
                break;
            case "removePeer":
                handleRemovePeer(engine, params, respond);
                break;
            case "getPeerIds":
                handleGetPeerIds(engine, params, respond);
                break;
            case "getPeerCount":
                handleGetPeerCount(engine, params, respond);
                break;
            case "handlePeerMessage":
                handlePeerMessage(engine, params, respond);
                break;
            case "getOutgoingMessages":
                handleGetOutgoingMessages(engine, params, respond);
                break;
            case "queueOperationsBroadcast":
                handleQueueOperationsBroadcast(engine, params, respond);
                break;
            case "startCollaboration":
                handleStartCollaboration(engine, params, respond);
                break;

            // ================================================================
            // C++ SyncClient (P2P WebRTC sync)
            // ================================================================
            case "enableSync":
                handleEnableSync(engine, params, respond);
                break;
            case "disableSync":
                handleDisableSync(engine, params, respond);
                break;
            case "getSyncState":
                handleGetSyncState(engine, params, respond);
                break;
            case "isSyncEnabled":
                handleIsSyncEnabled(engine, params, respond);
                break;
            case "processSyncOutgoing":
                handleProcessSyncOutgoing(engine, params, respond);
                break;
            case "processSyncPresence":
                handleProcessSyncPresence(engine, params, respond);
                break;
            case "broadcastSyncOperations":
                handleBroadcastSyncOperations(engine, params, respond);
                break;

            // ================================================================
            // C++ SyncClient Presence
            // ================================================================
            case "setSyncLocalName":
                handleSetSyncLocalName(engine, params, respond);
                break;
            case "setSyncCurrentSheet":
                handleSetSyncCurrentSheet(engine, params, respond);
                break;
            case "setSyncCursor":
                handleSetSyncCursor(engine, params, respond);
                break;
            case "clearSyncCursor":
                handleClearSyncCursor(engine, params, respond);
                break;
            case "setSyncSelection":
                handleSetSyncSelection(engine, params, respond);
                break;
            case "clearSyncSelection":
                handleClearSyncSelection(engine, params, respond);
                break;
            case "setSyncMousePosition":
                handleSetSyncMousePosition(engine, params, respond);
                break;
            case "clearSyncMousePosition":
                handleClearSyncMousePosition(engine, params, respond);
                break;
            case "setSyncEditing":
                handleSetSyncEditing(engine, params, respond);
                break;
            case "clearSyncEditing":
                handleClearSyncEditing(engine, params, respond);
                break;
            case "getRemotePresences":
                handleGetRemotePresences(engine, params, respond);
                break;

            // ================================================================
            // Debug/Development
            // ================================================================
            case "debugParseFormula":
                handleDebugParseFormula(engine, params, respond);
                break;

            // ================================================================
            // Viewport Pixel Queries
            // ================================================================
            case "getColumnPixelOffset":
                handleGetColumnPixelOffset(engine, params, respond);
                break;
            case "getRowPixelOffset":
                handleGetRowPixelOffset(engine, params, respond);
                break;
            case "getTotalWidth":
                handleGetTotalWidth(engine, params, respond);
                break;
            case "getTotalHeight":
                handleGetTotalHeight(engine, params, respond);
                break;

            // ================================================================
            // Formula API
            // ================================================================
            case "validateFormula":
                handleValidateFormula(engine, params, respond);
                break;
            case "getFormulaDisplay":
                handleGetFormulaDisplay(engine, params, respond);
                break;
            case "getCellDependencies":
                handleGetCellDependencies(engine, params, respond);
                break;
            case "getCellDependents":
                handleGetCellDependents(engine, params, respond);
                break;
            case "getFormulaReferences":
                handleGetFormulaReferences(engine, params, respond);
                break;
            case "getReferencesFromPartial":
                handleGetReferencesFromPartial(engine, params, respond);
                break;
            case "detectCircularRef":
                handleDetectCircularRef(engine, params, respond);
                break;
            case "getVolatileCells":
                handleGetVolatileCells(engine, params, respond);
                break;

            // ================================================================
            // Scripting (Luau)
            // ================================================================
            case "executeScript":
                handleExecuteScript(engine, params, respond);
                break;
            case "tokenizeLuau":
                handleTokenizeLuau(engine, params, respond);
                break;
            case "getAutocomplete":
                handleGetAutocomplete(engine, params, respond);
                break;

            // ================================================================
            // Spill Range Queries
            // ================================================================
            case "getSpillRangeAt":
                handleGetSpillRangeAt(engine, params, respond);
                break;

            // ================================================================
            // AI Agent
            // ================================================================
            case "initAgent":
                handleInitAgent(engine, params, respond);
                break;
            case "isAgentInitialized":
                handleIsAgentInitialized(engine, params, respond);
                break;
            case "sendAgentMessage": {
                // Use JavaScript streaming fetch for reliable SSE
                const serverUrl = engine.getAgentServerUrl();
                if (serverUrl == null) {
                    respond({
                        type: "error",
                        error: "Agent server URL not set",
                    });
                    break;
                }
                const { prompt, conversationId } = params as {
                    prompt: string;
                    conversationId: string;
                };
                engine.setAgentStreaming(true);
                respond({ type: "agentMessageSent", success: true });

                // Perform streaming fetch
                streamAgentMessage(serverUrl + "/api/agent/message", {
                    prompt,
                    conversation_id: conversationId || undefined,
                });
                break;
            }
            case "sendAgentToolResult": {
                const serverUrl = engine.getAgentServerUrl();
                if (serverUrl == null) {
                    respond({
                        type: "error",
                        error: "Agent server URL not set",
                    });
                    break;
                }
                const {
                    conversationId: convId,
                    toolUseId,
                    result,
                    isError,
                } = params as {
                    conversationId: string;
                    toolUseId: string;
                    result: string;
                    isError: boolean;
                };
                engine.setAgentStreaming(true);
                respond({ type: "agentToolResultSent", success: true });

                // Perform streaming fetch for tool result
                streamAgentMessage(serverUrl + "/api/agent/tool-result", {
                    conversation_id: convId,
                    tool_use_id: toolUseId,
                    result,
                    is_error: isError,
                });
                break;
            }
            case "getAgentConversationId":
                handleGetAgentConversationId(engine, params, respond);
                break;
            case "clearAgentConversation":
                handleClearAgentConversation(engine, params, respond);
                break;
            case "cancelAgent":
                handleCancelAgent(engine, params, respond);
                break;
            case "isAgentProcessing":
                handleIsAgentProcessing(engine, params, respond);
                break;

            default:
                respond({
                    type: "error",
                    error: "Unknown message type: " + type,
                });
        }
    } catch (err) {
        const error = err instanceof Error ? err.message : String(err);
        respond({ type: "error", error });
    }
}

// ============================================================================
// Event Listeners
// ============================================================================

// Listen for messages from main thread
workerSelf.onmessage = function (e: MessageEvent<WorkerRequest>): void {
    if (!isReady) {
        pendingMessages.push(e.data);
        return;
    }
    handleMessage(e.data);
};

// Start initialization
initModule();
