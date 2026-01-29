// =============================================================================
// WASM Bindings Header
// =============================================================================
//
// Declares the CellsEngine class that exposes the spreadsheet engine to
// JavaScript via Emscripten's Embind. This class is the main interface between
// the C++ core and the TypeScript UI.
//
// The implementation is split across multiple files:
// - bindings.cc: EMSCRIPTEN_BINDINGS registration, helpers
// - bindings_core.cc: Sheet info, cell operations, structure operations
// - bindings_viewport.cc: Viewport queries, spatial indexing
// - bindings_file.cc: File loading/saving (XLSX, CSV, ZCD)
// - bindings_format.cc: Number formatting, input parsing
// - bindings_formula.cc: Formula parsing, evaluation, display
// - bindings_crdt.cc: CRDT operations, sync management
// - bindings_luau.cc: Luau scripting, autocomplete, AI agent
//
// =============================================================================

#ifndef APPS_WASM_BINDINGS_H_
#define APPS_WASM_BINDINGS_H_

#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <string>

#include "core/cells/agent_client.h"
#include "core/cells/luau_autocomplete.h"
#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"
#include "core/cells/number_format.h"
#include "core/cells/ref_converter.h"
#include "core/cells/sync_manager.h"
#include "core/cells/viewport_index.h"
#include "core/net/include/SSEParser.h"
#include "core/net/include/SyncClient.h"

namespace cells::wasm {

using namespace emscripten;

// ============================================================================
// Change notification types for listener pattern
// ============================================================================

enum class ChangeType {
    CELL_CHANGED,       // Cell value/formula modified
    STRUCTURE_CHANGED,  // Rows/columns added, removed, resized, moved
    SHEET_CHANGED,      // Active sheet changed, sheet added/deleted/renamed/moved
    DATA_LOADED,        // New file loaded or workbook created
    LOAD_PROGRESS,      // File loading progress update (includes cell count)
    SYNC_STATE_CHANGED, // Sync connection state changed
    PEER_JOINED,        // A peer joined the sync session
    PEER_LEFT,          // A peer left the sync session
    PRESENCE_CHANGED    // Remote peer presence (cursor/selection) changed
};

// ============================================================================
// Helper functions (defined in bindings.cc)
// ============================================================================

std::string jsonEscape(const std::string& str);
std::string extractPayloadField(const std::string& payload, const std::string& key);

// ============================================================================
// CellsEngine - main wrapper class exposing the spreadsheet engine to JS
// ============================================================================

class CellsEngine : public cells::net::SyncClientDelegate, public cells::AgentClientDelegate {
public:
    CellsEngine();
    ~CellsEngine() override;

    // ========================================================================
    // Listener registration for change notifications (bindings_core.cc)
    // ========================================================================

    void setListener(val callback);
    void removeListener();

    // ========================================================================
    // File loading methods (bindings_file.cc)
    // ========================================================================

    std::string loadFromCells(const std::string& content);
    std::string loadFromCSV(const std::string& content, char delimiter, bool hasHeader);
    std::string loadFromXLSXDataPtr(uintptr_t ptr, size_t size);

    // ========================================================================
    // Sheet info methods (bindings_core.cc)
    // ========================================================================

    std::string getSheetInfo();
    int getSheetCount();
    std::string getSheetName(int index);
    int getActiveSheetIndex();
    void setActiveSheet(int index);
    std::string addSheet(const std::string& name);
    std::string deleteSheet(int index);
    std::string renameSheet(int index, const std::string& name);
    std::string moveSheet(int fromIndex, int toIndex);
    void setFreezePanes(int freezeCol, int freezeRow);

    // ========================================================================
    // Viewport query (bindings_viewport.cc)
    // ========================================================================

    std::string queryViewport(uint32_t col1, uint32_t row1, uint32_t col2, uint32_t row2);
    int32_t getColumnPixelOffset(uint32_t position);
    int32_t getRowPixelOffset(uint32_t position);
    uint32_t getTotalWidth();
    uint32_t getTotalHeight();

    // ========================================================================
    // Spill range queries (bindings_viewport.cc)
    // ========================================================================

    // Get spill range info for a cell at the given position
    // Returns JSON with: masterId, masterCol, masterRow, endCol, endRow, spillCount
    // Returns empty object if position is not part of any spill range
    std::string getSpillRangeAt(uint32_t col, uint32_t row);

    // ========================================================================
    // Cell operations (bindings_core.cc)
    // ========================================================================

    std::string updateCell(const std::string& cellIdStr, const std::string& value);
    std::string updateCellWithFormatDetection(const std::string& cellIdStr, const std::string& value);
    std::string createCell(uint32_t col, uint32_t row, const std::string& value);
    std::string getOrCreateCellAt(uint32_t col, uint32_t row);
    std::string deleteCell(const std::string& cellIdStr);
    std::string deleteCellAt(uint32_t col, uint32_t row);

    // ========================================================================
    // Number format operations (bindings_format.cc)
    // ========================================================================

    std::string setCellFormat(const std::string& cellIdStr, const std::string& formatIdStr);
    std::string setCellFormatAt(uint32_t col, uint32_t row, const std::string& formatIdStr);
    std::string getAvailableFormats();
    std::string createCustomFormat(const std::string& formatCode);
    std::string getFormulaFunctions();
    std::string getCellFormatId(const std::string& cellIdStr);
    std::string parseUserInputValue(const std::string& input);
    std::string formatCellValue(double value, const std::string& formatIdStr);
    std::string formatWithCode(double value, const std::string& formatCode);
    std::string formatCellById(const std::string& cellIdStr);
    std::string getFormatDetails(const std::string& formatId);
    std::string makeFormatId(const std::string& category, int decimals, bool separator,
                             const std::string& currency);

    // ========================================================================
    // Cell style operations (bindings_format.cc)
    // ========================================================================

    std::string setCellStyle(const std::string& cellIdStr, const std::string& styleJson);
    std::string setCellStyleAt(uint32_t col, uint32_t row, const std::string& styleJson);
    std::string getCellStyle(const std::string& cellIdStr);
    std::string getCellStyleAt(uint32_t col, uint32_t row);
    std::string getAvailableStyles();

    // Range style operations (creates a Range with RANGE_STYLE flag)
    std::string setRangeStyle(uint32_t startCol, uint32_t startRow, uint32_t endCol, uint32_t endRow,
                              const std::string& styleJson);
    std::string setRangeStyleOnSheet(uint32_t sheetIndex, uint32_t startCol, uint32_t startRow,
                                     uint32_t endCol, uint32_t endRow, const std::string& styleJson);
    std::string removeRangeStyle(uint32_t col, uint32_t row);

    // Effective style operations (resolves cell > range > column > row hierarchy)
    // Used by UI to show the actual style in the toolbar
    std::string getEffectiveCellStyle(uint32_t col, uint32_t row);
    std::string getEffectiveStyleForRange(uint32_t col1, uint32_t row1, uint32_t col2, uint32_t row2);

    // Axis style operations (set/get styles for entire columns or rows)
    std::string setColumnStyle(uint32_t colPosition, const std::string& styleJson);
    std::string setRowStyle(uint32_t rowPosition, const std::string& styleJson);
    std::string getColumnStyle(uint32_t colPosition);
    std::string getRowStyle(uint32_t rowPosition);
    std::string getColumnFormat(uint32_t colPosition);
    std::string getRowFormat(uint32_t rowPosition);

    // Axis format operations (set/clear formats for entire columns or rows)
    std::string setColumnFormat(uint32_t colPosition, const std::string& formatJson);
    std::string setRowFormat(uint32_t rowPosition, const std::string& formatJson);
    std::string clearColumnFormat(uint32_t colPosition);
    std::string clearRowFormat(uint32_t rowPosition);

    // Range format operations (set/clear formats for rectangular ranges)
    std::string setRangeFormat(uint32_t startCol, uint32_t startRow, uint32_t endCol, uint32_t endRow,
                               const std::string& formatJson);
    std::string setRangeFormatOnSheet(uint32_t sheetIndex, uint32_t startCol, uint32_t startRow,
                                      uint32_t endCol, uint32_t endRow, const std::string& formatJson);
    std::string removeRangeFormat(uint32_t col, uint32_t row);

    // ========================================================================
    // Column/row resize operations (bindings_core.cc)
    // ========================================================================

    std::string resizeColumn(const std::string& colIdStr, uint32_t width);
    std::string resizeColumnByPos(uint32_t pos, uint32_t width);
    std::string resizeRow(const std::string& rowIdStr, uint32_t height);
    std::string resizeRowByPos(uint32_t pos, uint32_t height);

    // ========================================================================
    // Column/row rename operations (bindings_core.cc)
    // ========================================================================

    std::string renameColumn(const std::string& colIdStr, const std::string& name);
    std::string renameColumnByPos(uint32_t pos, const std::string& name);

    // ========================================================================
    // Column/row move operations (bindings_core.cc)
    // ========================================================================

    std::string shiftColumnsForEmptyMove(uint32_t sourcePos, uint32_t targetPos);
    std::string shiftRowsForEmptyMove(uint32_t sourcePos, uint32_t targetPos);
    std::string moveColumn(const std::string& colIdStr, uint32_t targetPos);
    std::string moveRow(const std::string& rowIdStr, uint32_t targetPos);

    // ========================================================================
    // Column/row insert/delete operations (bindings_core.cc)
    // ========================================================================

    std::string insertColumnAt(uint32_t position, bool insertBefore);
    std::string insertRowAt(uint32_t position, bool insertBefore);
    std::string deleteColumnById(const std::string& colIdStr);
    std::string deleteRowById(const std::string& rowIdStr);

    // ========================================================================
    // Fill range (bindings_core.cc)
    // ========================================================================

    std::string fillRange(int sourceMinCol, int sourceMinRow,
                          int sourceMaxCol, int sourceMaxRow,
                          int targetMinCol, int targetMinRow,
                          int targetMaxCol, int targetMaxRow);

    // ========================================================================
    // Merge cell operations (bindings_core.cc)
    // ========================================================================

    std::string addMergeRange(uint32_t startCol, uint32_t startRow,
                              uint32_t endCol, uint32_t endRow);
    std::string removeMergeRange(uint32_t col, uint32_t row);

    // ========================================================================
    // Export methods (bindings_file.cc)
    // ========================================================================

    std::string exportToCells();
    std::string exportToCSV();
    std::string exportToXLSXPtr();  // Returns {ptr, size} for binary-safe transfer
    void freeExportBuffer();        // Release memory after JS copies the data
    bool hasFormulas();

    // ========================================================================
    // Workbook name (bindings_core.cc)
    // ========================================================================

    std::string getWorkbookName();
    void setWorkbookName(const std::string& name);

    // ========================================================================
    // Named ranges (bindings_core.cc)
    // ========================================================================

    std::string getNamedRanges();

    // ========================================================================
    // Create empty workbook (bindings_core.cc)
    // ========================================================================

    void createEmptyWorkbook();

    // ========================================================================
    // CRDT collaboration methods (bindings_crdt.cc)
    // ========================================================================

    std::string setNodeId(const std::string& nodeIdStr);
    std::string getNodeId();
    std::string getCurrentHLC();
    std::string getOperationsSince(const std::string& sinceHLCStr);
    std::string applyRemoteOperation(const std::string& opJson);
    std::string applyRemoteOperations(const std::string& opsJson);
    int getOpLogSize();
    bool hasOperation(const std::string& hlcStr);

    // ========================================================================
    // SyncManager methods (bindings_crdt.cc)
    // ========================================================================

    std::string initSyncManager();
    std::string addPeer(const std::string& peerIdStr);
    std::string removePeer(const std::string& peerIdStr);
    std::string getPeerIds();
    int getPeerCount();
    std::string handlePeerMessage(const std::string& peerIdStr, const std::string& messageJson);
    std::string getOutgoingMessages();
    std::string queueOperationsBroadcast();
    void setDebugNoPrune(bool noPrune);

    // ========================================================================
    // Collaboration mode methods (bindings_crdt.cc)
    // ========================================================================

    std::string getCollabMode();
    bool isCollaborating();
    std::string startCollaboration();
    std::string setCollabMode(const std::string& mode);

    // ========================================================================
    // C++ SyncClient methods (bindings_crdt.cc)
    // ========================================================================

    std::string enableSync(const std::string& serverUrl, const std::string& roomId);
    void disableSync();
    std::string getSyncState();
    bool isSyncEnabled();
    void processSyncOutgoing();
    void processSyncPresence();
    void broadcastSyncOperations();

    // ========================================================================
    // C++ SyncClient presence methods (bindings_crdt.cc)
    // ========================================================================

    void setSyncLocalName(const std::string& name);
    void setSyncCurrentSheet(const std::string& sheetId);
    void setSyncCursor(int col, int row);
    void clearSyncCursor();
    void setSyncSelection(int startCol, int startRow, int endCol, int endRow);
    void clearSyncSelection();
    void setSyncMousePosition(double x, double y);
    void clearSyncMousePosition();
    void setSyncEditing(int32_t col, int32_t row, const std::string& text);
    void clearSyncEditing();
    std::string getRemotePresences();

    // ========================================================================
    // SyncClientDelegate implementation (bindings_crdt.cc)
    // ========================================================================

    void syncClientStateDidChange(cells::net::SyncClient& client,
                                  cells::net::SyncClientState newState) override;
    void syncClientPeerDidChange(cells::net::SyncClient& client,
                                 const cells::net::PeerInfo& peer) override;
    void syncClientPeerDidDisconnect(cells::net::SyncClient& client,
                                     const std::string& peerId) override;
    void syncClientDataDidChange(cells::net::SyncClient& client) override;
    void syncClientDidError(cells::net::SyncClient& client,
                            const std::string& error) override;
    void syncClientLatencyDidUpdate(cells::net::SyncClient& client,
                                    const std::string& peer_id,
                                    int latency_ms) override;
    void syncClientPresenceDidUpdate(cells::net::SyncClient& client,
                                     const std::string& peerId,
                                     const cells::net::PresenceData& presence) override;
    void syncClientPresenceDidRemove(cells::net::SyncClient& client,
                                     const std::string& peerId) override;

    // ========================================================================
    // AgentClientDelegate implementation (bindings_luau.cc)
    // ========================================================================

    void onAgentText(const std::string& text) override;
    void onAgentToolUse(const std::string& toolId, const std::string& name,
                        const std::string& input) override;
    void onAgentToolResultNeeded(const std::string& toolUseId) override;
    void onAgentComplete(const std::string& stopReason,
                         const std::string& conversationId) override;
    void onAgentError(const std::string& message) override;

    // ========================================================================
    // Agent API methods (bindings_luau.cc)
    // ========================================================================

    void setAgentListener(val callback);
    void removeAgentListener();
    void initAgent(const std::string& serverUrl);
    std::string getAgentServerUrl() const;
    void feedAgentStreamData(const std::string& data);
    void endAgentStream();
    void errorAgentStream(const std::string& error);
    bool isAgentStreaming() const;
    void setAgentStreaming(bool streaming);
    bool isAgentInitialized() const;
    void sendAgentMessage(const std::string& prompt, const std::string& conversationId);
    std::string getAgentConversationId() const;
    void clearAgentConversation();
    void cancelAgent();
    bool isAgentProcessing() const;

    // ========================================================================
    // Formula API methods (bindings_formula.cc)
    // ========================================================================

    std::string validateFormula(const std::string& formulaText);
    std::string getFormulaDisplay(const std::string& cellIdStr);
    std::string getCellDependencies(const std::string& cellIdStr);
    std::string getCellDependents(const std::string& cellIdStr);
    std::string getFormulaReferences(const std::string& formulaText);
    std::string getReferencesFromPartial(const std::string& formulaText);
    std::string detectCircularRef(const std::string& cellIdStr);
    std::string getVolatileCells();

    // ========================================================================
    // Formula Evaluation (bindings_formula.cc)
    // ========================================================================

    std::string getCellDisplayValue(const std::string& cellIdStr);
    std::string recalculate();
    bool hasDirtyCellsCheck();
    std::string markCellDirty(const std::string& cellIdStr);
    std::string getDirtyCellIds();

    // ========================================================================
    // Debug/Development methods (bindings_formula.cc)
    // ========================================================================

    std::string debugParseFormula(const std::string& formulaText);

    // ========================================================================
    // Scripting API (Luau) (bindings_luau.cc)
    // ========================================================================

    std::string executeScript(const std::string& script);
    std::string tokenizeLuau(const std::string& source);
    std::string getAutocomplete(const std::string& source, unsigned line, unsigned column);

    // ========================================================================
    // Internal helpers used by multiple implementation files
    // ========================================================================

    void rebuildViewportIndex();
    void notifyListeners(ChangeType type);
    void notifyListenersWithData(ChangeType type, const std::string& data);
    void notifyLoadProgress(size_t cellsLoaded, size_t totalEstimate);

    // Accessors for implementation files
    Workbook* workbook() { return _workbook.get(); }
    const Workbook* workbook() const { return _workbook.get(); }
    Sheet* activeSheet();
    size_t activeSheetIndex() const { return _activeSheetIndex; }
    void setActiveSheetIndex(size_t index) { _activeSheetIndex = index; }
    ViewportIndex& viewportIndex() { return _viewportIndex; }
    RefConverter& refConverter() { return _refConverter; }
    SyncManager* syncManager() { return _syncManager.get(); }
    cells::net::SyncClient* syncClient() { return _syncClient.get(); }
    LuauSandbox& luauSandbox() { return _luauSandbox; }
    LuauAutocomplete& luauAutocomplete() { return _luauAutocomplete; }
    NumberFormatRegistry& formatRegistry() { return _formatRegistry; }
    val& listener() { return _listener; }
    val& agentListener() { return _agentListener; }

    // For setting workbook (used by file loaders)
    void setWorkbook(std::unique_ptr<Workbook> wb) { _workbook = std::move(wb); }

private:
    std::unique_ptr<Workbook> _workbook;
    size_t _activeSheetIndex;
    ViewportIndex _viewportIndex;
    RefConverter _refConverter;
    val _listener;
    std::unique_ptr<SyncManager> _syncManager;
    std::unique_ptr<cells::net::SyncClient> _syncClient;
    LuauSandbox _luauSandbox;
    LuauAutocomplete _luauAutocomplete;
    NumberFormatRegistry _formatRegistry;

    // Binary export buffer - used by exportToXLSXPtr() for binary-safe transfer
    std::vector<uint8_t> _exportBuffer;

    static inline const std::unordered_map<ID, std::string, IDHash> _emptyCustomFormats{};

    val _agentListener;
    std::unique_ptr<AgentClient> _agentClient;
    std::string _agentServerUrl;
    std::unique_ptr<net::SSEParser> _agentSseParser;
    bool _agentIsStreaming{false};
    std::string _agentConversationId;
    std::string _pendingToolId;
    std::string _pendingToolName;
    std::string _pendingToolInput;
    bool _needsToolExecution{false};

    // Internal helpers for agent SSE parsing
    void handleAgentSSEEvent(const std::string& eventType, const std::string& data);
    void executeAgentTool();

    // Broadcast pending operations to peers (queue + send)
    void broadcastPendingOperations();
};

}  // namespace cells::wasm

#endif  // APPS_WASM_BINDINGS_H_
