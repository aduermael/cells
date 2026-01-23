// =============================================================================
// WASM Bindings - Main Entry with Embind Registration
// =============================================================================
//
// This file contains:
// - Helper functions used across implementation files
// - EMSCRIPTEN_BINDINGS registration for exposing CellsEngine to JavaScript
//
// The implementation is split across multiple files:
// - bindings.h: Class declaration
// - bindings_core.cc: Constructor, listeners, sheet/cell/structure operations
// - bindings_viewport.cc: Viewport queries, spatial indexing
// - bindings_file.cc: File loading/saving (XLSX, CSV, ZCD)
// - bindings_format.cc: Number formatting, input parsing
// - bindings_formula.cc: Formula parsing, evaluation, display
// - bindings_crdt.cc: CRDT operations, sync management
// - bindings_luau.cc: Luau scripting, autocomplete, AI agent
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <emscripten/bind.h>

#include "core/log/include/Logger.h"

using namespace emscripten;

namespace cells::wasm {

// ============================================================================
// Helper functions used by multiple implementation files
// ============================================================================

std::string jsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

std::string extractPayloadField(const std::string& payload, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t pos = payload.find(searchKey);
    if (pos == std::string::npos) {
        return "";
    }
    pos += searchKey.length();
    size_t endPos = payload.find('"', pos);
    if (endPos == std::string::npos) {
        return "";
    }
    return payload.substr(pos, endPos - pos);
}

}  // namespace cells::wasm

// ============================================================================
// Embind bindings
// ============================================================================

EMSCRIPTEN_BINDINGS(cells) {
    class_<cells::wasm::CellsEngine>("CellsEngine")
        .constructor<>()
        // Listener registration
        .function("setListener", &cells::wasm::CellsEngine::setListener)
        .function("removeListener", &cells::wasm::CellsEngine::removeListener)
        // File loading
        .function("loadFromCells", &cells::wasm::CellsEngine::loadFromCells)
        .function("loadFromCSV", &cells::wasm::CellsEngine::loadFromCSV)
        .function("loadFromXLSXDataPtr", &cells::wasm::CellsEngine::loadFromXLSXDataPtr)
        // Sheet info
        .function("getSheetInfo", &cells::wasm::CellsEngine::getSheetInfo)
        .function("getSheetCount", &cells::wasm::CellsEngine::getSheetCount)
        .function("getSheetName", &cells::wasm::CellsEngine::getSheetName)
        .function("getActiveSheetIndex", &cells::wasm::CellsEngine::getActiveSheetIndex)
        .function("setActiveSheet", &cells::wasm::CellsEngine::setActiveSheet)
        .function("addSheet", &cells::wasm::CellsEngine::addSheet)
        .function("deleteSheet", &cells::wasm::CellsEngine::deleteSheet)
        .function("renameSheet", &cells::wasm::CellsEngine::renameSheet)
        .function("moveSheet", &cells::wasm::CellsEngine::moveSheet)
        .function("setFreezePanes", &cells::wasm::CellsEngine::setFreezePanes)
        // Viewport
        .function("queryViewport", &cells::wasm::CellsEngine::queryViewport)
        .function("getColumnPixelOffset", &cells::wasm::CellsEngine::getColumnPixelOffset)
        .function("getRowPixelOffset", &cells::wasm::CellsEngine::getRowPixelOffset)
        .function("getTotalWidth", &cells::wasm::CellsEngine::getTotalWidth)
        .function("getTotalHeight", &cells::wasm::CellsEngine::getTotalHeight)
        // Spill range queries
        .function("getSpillRangeAt", &cells::wasm::CellsEngine::getSpillRangeAt)
        // Cell operations
        .function("updateCell", &cells::wasm::CellsEngine::updateCell)
        .function("updateCellWithFormatDetection",
                  &cells::wasm::CellsEngine::updateCellWithFormatDetection)
        .function("createCell", &cells::wasm::CellsEngine::createCell)
        .function("getOrCreateCellAt", &cells::wasm::CellsEngine::getOrCreateCellAt)
        .function("deleteCell", &cells::wasm::CellsEngine::deleteCell)
        .function("deleteCellAt", &cells::wasm::CellsEngine::deleteCellAt)
        // Number formats
        .function("setCellFormat", &cells::wasm::CellsEngine::setCellFormat)
        .function("setCellFormatAt", &cells::wasm::CellsEngine::setCellFormatAt)
        .function("getAvailableFormats", &cells::wasm::CellsEngine::getAvailableFormats)
        .function("createCustomFormat", &cells::wasm::CellsEngine::createCustomFormat)
        .function("getFormulaFunctions", &cells::wasm::CellsEngine::getFormulaFunctions)
        .function("getCellFormatId", &cells::wasm::CellsEngine::getCellFormatId)
        .function("parseUserInputValue", &cells::wasm::CellsEngine::parseUserInputValue)
        .function("formatCellValue", &cells::wasm::CellsEngine::formatCellValue)
        .function("formatWithCode", &cells::wasm::CellsEngine::formatWithCode)
        .function("formatCellById", &cells::wasm::CellsEngine::formatCellById)
        .function("getFormatDetails", &cells::wasm::CellsEngine::getFormatDetails)
        .function("makeFormatId", &cells::wasm::CellsEngine::makeFormatId)
        // Cell styles
        .function("setCellStyle", &cells::wasm::CellsEngine::setCellStyle)
        .function("setCellStyleAt", &cells::wasm::CellsEngine::setCellStyleAt)
        .function("getCellStyle", &cells::wasm::CellsEngine::getCellStyle)
        .function("getCellStyleAt", &cells::wasm::CellsEngine::getCellStyleAt)
        .function("getAvailableStyles", &cells::wasm::CellsEngine::getAvailableStyles)
        // Range styles
        .function("setRangeStyle", &cells::wasm::CellsEngine::setRangeStyle)
        .function("setRangeStyleOnSheet", &cells::wasm::CellsEngine::setRangeStyleOnSheet)
        .function("removeRangeStyle", &cells::wasm::CellsEngine::removeRangeStyle)
        // Effective style (resolves cell > range > column > row hierarchy)
        .function("getEffectiveCellStyle", &cells::wasm::CellsEngine::getEffectiveCellStyle)
        .function("getEffectiveStyleForRange", &cells::wasm::CellsEngine::getEffectiveStyleForRange)
        // Axis styles (entire column/row styles)
        .function("setColumnStyle", &cells::wasm::CellsEngine::setColumnStyle)
        .function("setRowStyle", &cells::wasm::CellsEngine::setRowStyle)
        .function("getColumnStyle", &cells::wasm::CellsEngine::getColumnStyle)
        .function("getRowStyle", &cells::wasm::CellsEngine::getRowStyle)
        // Column/row resize
        .function("resizeColumn", &cells::wasm::CellsEngine::resizeColumn)
        .function("resizeColumnByPos", &cells::wasm::CellsEngine::resizeColumnByPos)
        .function("resizeRow", &cells::wasm::CellsEngine::resizeRow)
        .function("resizeRowByPos", &cells::wasm::CellsEngine::resizeRowByPos)
        // Column/row rename
        .function("renameColumn", &cells::wasm::CellsEngine::renameColumn)
        .function("renameColumnByPos", &cells::wasm::CellsEngine::renameColumnByPos)
        // Column/row move
        .function("moveColumn", &cells::wasm::CellsEngine::moveColumn)
        .function("moveRow", &cells::wasm::CellsEngine::moveRow)
        .function("shiftColumnsForEmptyMove", &cells::wasm::CellsEngine::shiftColumnsForEmptyMove)
        .function("shiftRowsForEmptyMove", &cells::wasm::CellsEngine::shiftRowsForEmptyMove)
        // Column/row insert/delete
        .function("insertColumnAt", &cells::wasm::CellsEngine::insertColumnAt)
        .function("insertRowAt", &cells::wasm::CellsEngine::insertRowAt)
        .function("deleteColumnById", &cells::wasm::CellsEngine::deleteColumnById)
        .function("deleteRowById", &cells::wasm::CellsEngine::deleteRowById)
        // Fill range
        .function("fillRange", &cells::wasm::CellsEngine::fillRange)
        // Merge cell operations
        .function("addMergeRange", &cells::wasm::CellsEngine::addMergeRange)
        .function("removeMergeRange", &cells::wasm::CellsEngine::removeMergeRange)
        // Export
        .function("exportToCells", &cells::wasm::CellsEngine::exportToCells)
        .function("exportToCSV", &cells::wasm::CellsEngine::exportToCSV)
        .function("exportToXLSXPtr", &cells::wasm::CellsEngine::exportToXLSXPtr)
        .function("freeExportBuffer", &cells::wasm::CellsEngine::freeExportBuffer)
        .function("hasFormulas", &cells::wasm::CellsEngine::hasFormulas)
        // Workbook management
        .function("getWorkbookName", &cells::wasm::CellsEngine::getWorkbookName)
        .function("setWorkbookName", &cells::wasm::CellsEngine::setWorkbookName)
        .function("getNamedRanges", &cells::wasm::CellsEngine::getNamedRanges)
        .function("createEmptyWorkbook", &cells::wasm::CellsEngine::createEmptyWorkbook)
        // CRDT collaboration
        .function("setNodeId", &cells::wasm::CellsEngine::setNodeId)
        .function("getNodeId", &cells::wasm::CellsEngine::getNodeId)
        .function("getCurrentHLC", &cells::wasm::CellsEngine::getCurrentHLC)
        .function("getOperationsSince", &cells::wasm::CellsEngine::getOperationsSince)
        .function("applyRemoteOperation", &cells::wasm::CellsEngine::applyRemoteOperation)
        .function("applyRemoteOperations", &cells::wasm::CellsEngine::applyRemoteOperations)
        .function("getOpLogSize", &cells::wasm::CellsEngine::getOpLogSize)
        .function("hasOperation", &cells::wasm::CellsEngine::hasOperation)
        // SyncManager methods
        .function("initSyncManager", &cells::wasm::CellsEngine::initSyncManager)
        .function("addPeer", &cells::wasm::CellsEngine::addPeer)
        .function("removePeer", &cells::wasm::CellsEngine::removePeer)
        .function("getPeerIds", &cells::wasm::CellsEngine::getPeerIds)
        .function("getPeerCount", &cells::wasm::CellsEngine::getPeerCount)
        .function("handlePeerMessage", &cells::wasm::CellsEngine::handlePeerMessage)
        .function("getOutgoingMessages", &cells::wasm::CellsEngine::getOutgoingMessages)
        .function("queueOperationsBroadcast", &cells::wasm::CellsEngine::queueOperationsBroadcast)
        .function("setDebugNoPrune", &cells::wasm::CellsEngine::setDebugNoPrune)
        // Collaboration mode
        .function("getCollabMode", &cells::wasm::CellsEngine::getCollabMode)
        .function("isCollaborating", &cells::wasm::CellsEngine::isCollaborating)
        .function("startCollaboration", &cells::wasm::CellsEngine::startCollaboration)
        .function("setCollabMode", &cells::wasm::CellsEngine::setCollabMode)
        // C++ SyncClient (P2P WebRTC sync)
        .function("enableSync", &cells::wasm::CellsEngine::enableSync)
        .function("disableSync", &cells::wasm::CellsEngine::disableSync)
        .function("getSyncState", &cells::wasm::CellsEngine::getSyncState)
        .function("isSyncEnabled", &cells::wasm::CellsEngine::isSyncEnabled)
        .function("processSyncOutgoing", &cells::wasm::CellsEngine::processSyncOutgoing)
        .function("processSyncPresence", &cells::wasm::CellsEngine::processSyncPresence)
        .function("broadcastSyncOperations", &cells::wasm::CellsEngine::broadcastSyncOperations)
        // C++ SyncClient presence
        .function("setSyncLocalName", &cells::wasm::CellsEngine::setSyncLocalName)
        .function("setSyncCurrentSheet", &cells::wasm::CellsEngine::setSyncCurrentSheet)
        .function("setSyncCursor", &cells::wasm::CellsEngine::setSyncCursor)
        .function("clearSyncCursor", &cells::wasm::CellsEngine::clearSyncCursor)
        .function("setSyncSelection", &cells::wasm::CellsEngine::setSyncSelection)
        .function("clearSyncSelection", &cells::wasm::CellsEngine::clearSyncSelection)
        .function("setSyncMousePosition", &cells::wasm::CellsEngine::setSyncMousePosition)
        .function("clearSyncMousePosition", &cells::wasm::CellsEngine::clearSyncMousePosition)
        .function("setSyncEditing", &cells::wasm::CellsEngine::setSyncEditing)
        .function("clearSyncEditing", &cells::wasm::CellsEngine::clearSyncEditing)
        .function("getRemotePresences", &cells::wasm::CellsEngine::getRemotePresences)
        // Formula API
        .function("validateFormula", &cells::wasm::CellsEngine::validateFormula)
        .function("getFormulaDisplay", &cells::wasm::CellsEngine::getFormulaDisplay)
        .function("getCellDependencies", &cells::wasm::CellsEngine::getCellDependencies)
        .function("getCellDependents", &cells::wasm::CellsEngine::getCellDependents)
        .function("getFormulaReferences", &cells::wasm::CellsEngine::getFormulaReferences)
        .function("getReferencesFromPartial", &cells::wasm::CellsEngine::getReferencesFromPartial)
        .function("detectCircularRef", &cells::wasm::CellsEngine::detectCircularRef)
        .function("getVolatileCells", &cells::wasm::CellsEngine::getVolatileCells)
        // Formula Evaluation
        .function("getCellDisplayValue", &cells::wasm::CellsEngine::getCellDisplayValue)
        .function("recalculate", &cells::wasm::CellsEngine::recalculate)
        .function("hasDirtyCells", &cells::wasm::CellsEngine::hasDirtyCellsCheck)
        .function("markCellDirty", &cells::wasm::CellsEngine::markCellDirty)
        .function("getDirtyCellIds", &cells::wasm::CellsEngine::getDirtyCellIds)
        // Debug/Development
        .function("debugParseFormula", &cells::wasm::CellsEngine::debugParseFormula)
        // Scripting (Luau)
        .function("executeScript", &cells::wasm::CellsEngine::executeScript)
        .function("tokenizeLuau", &cells::wasm::CellsEngine::tokenizeLuau)
        .function("getAutocomplete", &cells::wasm::CellsEngine::getAutocomplete)
        // AI Agent
        .function("setAgentListener", &cells::wasm::CellsEngine::setAgentListener)
        .function("removeAgentListener", &cells::wasm::CellsEngine::removeAgentListener)
        .function("initAgent", &cells::wasm::CellsEngine::initAgent)
        .function("isAgentInitialized", &cells::wasm::CellsEngine::isAgentInitialized)
        .function("sendAgentMessage", &cells::wasm::CellsEngine::sendAgentMessage)
        .function("getAgentConversationId", &cells::wasm::CellsEngine::getAgentConversationId)
        .function("clearAgentConversation", &cells::wasm::CellsEngine::clearAgentConversation)
        .function("cancelAgent", &cells::wasm::CellsEngine::cancelAgent)
        .function("isAgentProcessing", &cells::wasm::CellsEngine::isAgentProcessing)
        // AI Agent - JS-based streaming
        .function("getAgentServerUrl", &cells::wasm::CellsEngine::getAgentServerUrl)
        .function("feedAgentStreamData", &cells::wasm::CellsEngine::feedAgentStreamData)
        .function("endAgentStream", &cells::wasm::CellsEngine::endAgentStream)
        .function("errorAgentStream", &cells::wasm::CellsEngine::errorAgentStream)
        .function("isAgentStreaming", &cells::wasm::CellsEngine::isAgentStreaming)
        .function("setAgentStreaming", &cells::wasm::CellsEngine::setAgentStreaming);

    // Logger bindings - control logging from JavaScript
    enum_<cells::log::Level>("LogLevel")
        .value("DEBUG", cells::log::Level::kDebug)
        .value("INFO", cells::log::Level::kInfo)
        .value("WARN", cells::log::Level::kWarn)
        .value("ERROR", cells::log::Level::kError);

    // Free functions for logging
    function("logDebug",
             +[](const std::string& msg) { cells::log::Logger::instance().debug("%s", msg.c_str()); });
    function("logInfo",
             +[](const std::string& msg) { cells::log::Logger::instance().info("%s", msg.c_str()); });
    function("logWarn",
             +[](const std::string& msg) { cells::log::Logger::instance().warn("%s", msg.c_str()); });
    function("logError",
             +[](const std::string& msg) { cells::log::Logger::instance().error("%s", msg.c_str()); });

    // Logger configuration
    function("setLogEnabled", +[](bool enabled) { cells::log::Logger::instance().setEnabled(enabled); });
    function("isLogEnabled", +[]() { return cells::log::Logger::instance().isEnabled(); });
    function("setLogLevel",
             +[](cells::log::Level level) { cells::log::Logger::instance().setMinLevel(level); });
    function("getLogLevel", +[]() { return cells::log::Logger::instance().getMinLevel(); });
}
