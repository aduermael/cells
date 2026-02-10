// =============================================================================
// Worker Core Handlers
// =============================================================================
//
// Message handlers for core spreadsheet operations in the worker.
// Extracted from worker.ts to separate spreadsheet logic from collaboration.
//
// This is a BRIDGE module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - File operations (load, export)
// - Sheet operations (add, delete, rename, move, setActive)
// - Cell operations (create, update, delete)
// - Format operations (number formatting, custom formats)
// - Column/row operations (resize, rename, move, insert, delete)
// - Viewport queries
// - Formula operations (validate, dependencies, references)
// - Scripting (Luau execute, tokenize, autocomplete)
// - AI Agent (init, message, cancel)
//
// =============================================================================

import type {
    CellsModule,
    CellsEngine,
    WorkerResponse,
    JsonResult,
} from "./worker-types.js";

type RespondFn = (response: WorkerResponse, transfer?: Transferable[]) => void;

// ============================================================================
// Encoding Detection
// ============================================================================

/**
 * Decode an ArrayBuffer with automatic encoding detection.
 * Strategy:
 * 1. Check for UTF-8 BOM - if present, decode as UTF-8
 * 2. Try UTF-8 with fatal=true - if it works without errors, use UTF-8
 * 3. Check if UTF-8 decoded content has replacement characters (U+FFFD)
 * 4. Fall back to Windows-1252 (superset of ISO-8859-1, common for European files)
 */
function decodeWithEncodingDetection(data: ArrayBuffer): string {
    const bytes = new Uint8Array(data);

    // Check for UTF-8 BOM (EF BB BF)
    const hasUtf8Bom = bytes.length >= 3 &&
        bytes[0] === 0xEF && bytes[1] === 0xBB && bytes[2] === 0xBF;

    if (hasUtf8Bom) {
        // Has UTF-8 BOM, definitely UTF-8
        return new TextDecoder("utf-8").decode(data);
    }

    // Try UTF-8 with fatal mode to detect if it's valid UTF-8
    try {
        const utf8Decoder = new TextDecoder("utf-8", { fatal: true });
        const decoded = utf8Decoder.decode(data);

        // Additional check: if decoding succeeded but contains replacement chars,
        // the file might have been saved with mixed encoding
        if (!decoded.includes("\uFFFD")) {
            return decoded;
        }
    } catch {
        // UTF-8 decoding failed, fall through to fallback encoding
    }

    // Fall back to Windows-1252 (superset of ISO-8859-1)
    // This is the most common encoding for European CSV files from Excel
    try {
        const win1252Decoder = new TextDecoder("windows-1252", { fatal: false });
        return win1252Decoder.decode(data);
    } catch {
        // If Windows-1252 is not supported (unlikely), fall back to UTF-8 lossy
        return new TextDecoder("utf-8", { fatal: false }).decode(data);
    }
}

// ============================================================================
// File Operations
// ============================================================================

export function handleLoad(
    engine: CellsEngine,
    Module: CellsModule,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { format, data } = params;
    let result: JsonResult;

    if (format === "zcd") {
        // data is a string for .zcd format
        const content =
            typeof data === "string"
                ? data
                : new TextDecoder().decode(data as ArrayBuffer);
        result = JSON.parse(engine.loadFromCells(content)) as JsonResult;
    } else if (format === "csv") {
        // data is ArrayBuffer, delimiter auto-detected if not specified
        // Auto-detect encoding: try UTF-8 first, fall back to Windows-1252
        const content = decodeWithEncodingDetection(data as ArrayBuffer);
        // Use 0 (null char) to signal auto-detection when no delimiter specified
        const delimiterCode = params.delimiter
            ? (params.delimiter as string).charCodeAt(0)
            : 0;
        const hasHeader = params.hasHeader !== false;
        try {
            const jsonResult = engine.loadFromCSV(content, delimiterCode, hasHeader);
            result = JSON.parse(jsonResult) as JsonResult;
        } catch (e) {
            // Provide detailed error info for debugging
            const err = e instanceof Error ? e.message : String(e);
            respond({ type: "error", error: `CSV load failed: ${err}` });
            return;
        }
    } else if (format === "xlsx") {
        // data is ArrayBuffer - copy directly to WASM heap to avoid UTF-8 encoding issues
        const bytes = new Uint8Array(data as ArrayBuffer);
        const ptr = Module._malloc(bytes.length);
        Module.HEAPU8.set(bytes, ptr);
        result = JSON.parse(
            engine.loadFromXLSXDataPtr(ptr, bytes.length),
        ) as JsonResult;
        Module._free(ptr);
    } else {
        respond({ type: "error", error: "Unknown format: " + format });
        return;
    }

    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        // Get sheet names
        const sheetCount = result.sheetCount as number;
        const sheetNames: string[] = [];
        for (let i = 0; i < sheetCount; i++) {
            sheetNames.push(engine.getSheetName(i));
        }
        respond({
            type: "loaded",
            sheetCount,
            sheetNames,
        });
    }
}

export function handleExport(
    engine: CellsEngine,
    Module: CellsModule,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { format } = params as { format: string };
    let data: ArrayBuffer;
    let filename: string;
    const workbookName = engine.getWorkbookName() || "spreadsheet";

    if (format === "zcd") {
        const content = engine.exportToCells();
        if (!content) {
            respond({ type: "error", error: "Export failed" });
            return;
        }
        // Convert string to ArrayBuffer for transfer
        data = new TextEncoder().encode(content).buffer;
        filename = workbookName + ".zcd";
    } else if (format === "csv") {
        const content = engine.exportToCSV();
        if (!content) {
            respond({ type: "error", error: "Export failed" });
            return;
        }
        data = new TextEncoder().encode(content).buffer;
        filename = workbookName + ".csv";
    } else if (format === "xlsx") {
        // Use pointer-based export to avoid UTF-8 encoding corruption
        // Binary data (ZIP files) contains bytes >= 128 that get corrupted
        // when passed as std::string through Embind (interpreted as UTF-8)
        const result = JSON.parse(engine.exportToXLSXPtr()) as {
            ptr?: number;
            size?: number;
            error?: string;
        };
        if (result.error) {
            respond({
                type: "error",
                error: result.error,
            });
            return;
        }
        if (result.ptr === undefined || result.size === undefined) {
            respond({
                type: "error",
                error: "XLSX export failed: invalid response",
            });
            return;
        }
        // Copy binary data directly from WASM heap - no string encoding involved
        const bytes = Module.HEAPU8.slice(result.ptr, result.ptr + result.size);
        data = bytes.buffer;
        // Release the C++ buffer memory
        engine.freeExportBuffer();
        filename = workbookName + ".xlsx";
    } else {
        respond({ type: "error", error: "Unknown export format: " + format });
        return;
    }

    // Transfer the ArrayBuffer for efficiency
    respond({ type: "exported", format, data, filename }, [data]);
}

// ============================================================================
// Sheet Operations
// ============================================================================

export function handleGetSheetInfo(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = JSON.parse(engine.getSheetInfo()) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "sheetInfo", ...result });
    }
}

export function handleQueryViewport(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { x1, y1, x2, y2 } = params as {
        x1: number;
        y1: number;
        x2: number;
        y2: number;
    };
    const result = JSON.parse(
        engine.queryViewport(x1, y1, x2, y2),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "viewport", ...result });
    }
}

export function handleSetActiveSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { index } = params as { index: number };
    engine.setActiveSheet(index);
    respond({ type: "sheetChanged", index });
}

export function handleGetSheets(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const count = engine.getSheetCount();
    const activeIndex = engine.getActiveSheetIndex();
    const sheets: Array<{ index: number; name: string; active: boolean }> = [];
    for (let i = 0; i < count; i++) {
        sheets.push({
            index: i,
            name: engine.getSheetName(i),
            active: i === activeIndex,
        });
    }
    respond({ type: "sheets", sheets, activeIndex });
}

export function handleAddSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { name } = params as { name?: string };
    const result = JSON.parse(engine.addSheet(name || "")) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({
            type: "sheetAdded",
            index: result.index,
            name: result.name,
        });
    }
}

export function handleDeleteSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { index } = params as { index: number };
    const result = JSON.parse(engine.deleteSheet(index)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "sheetDeleted", activeIndex: result.activeIndex });
    }
}

export function handleRenameSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { index, name } = params as { index: number; name: string };
    const result = JSON.parse(engine.renameSheet(index, name)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "sheetRenamed", success: true });
    }
}

export function handleMoveSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { fromIndex, toIndex } = params as {
        fromIndex: number;
        toIndex: number;
    };
    const result = JSON.parse(
        engine.moveSheet(fromIndex, toIndex),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "sheetMoved", activeIndex: result.activeIndex });
    }
}

export function handleSetFreezePanes(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { freezeCol, freezeRow } = params as {
        freezeCol: number;
        freezeRow: number;
    };
    engine.setFreezePanes(freezeCol, freezeRow);
    respond({ type: "freezePanesSet", success: true });
}

// ============================================================================
// Cell Operations
// ============================================================================

export function handleUpdateCell(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId, value } = params as { cellId: string; value: string };
    const result = JSON.parse(engine.updateCell(cellId, value)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "cellUpdated", success: true });
    }
}

export function handleUpdateCellWithFormatDetection(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId, value } = params as { cellId: string; value: string };
    const result = JSON.parse(
        engine.updateCellWithFormatDetection(cellId, value),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({
            type: "cellUpdated",
            success: true,
            formatId: result.formatId,
        });
    }
}

export function handleCreateCell(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row, value } = params as {
        col: number;
        row: number;
        value?: string;
    };
    const result = JSON.parse(
        engine.createCell(col, row, value || ""),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "cellCreated", cellId: result.id });
    }
}

export function handleGetOrCreateCellAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = JSON.parse(engine.getOrCreateCellAt(col, row)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({
            type: "cellInfo",
            cellId: result.id,
            existed: result.existed,
            value: result.value,
            editValue: result.editValue || result.value,
            formula: result.formula || null,
        });
    }
}

export function handleDeleteCell(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = JSON.parse(engine.deleteCell(cellId)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "cellDeleted", success: true });
    }
}

export function handleDeleteCellAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = JSON.parse(engine.deleteCellAt(col, row)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "cellDeleted", deleted: result.deleted });
    }
}

// ============================================================================
// Format Operations
// ============================================================================

export function handleSetCellFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId, formatId } = params as { cellId: string; formatId: string };
    const result = JSON.parse(
        engine.setCellFormat(cellId, formatId),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "formatSet", success: true });
    }
}

export function handleSetCellFormatAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row, formatJson } = params as {
        col: number;
        row: number;
        formatJson: string;  // JSON-encoded format properties
    };
    const result = JSON.parse(
        engine.setCellFormatAt(col, row, formatJson),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "formatSet", success: true });
    }
}

export function handleGetAvailableFormats(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const formats = JSON.parse(engine.getAvailableFormats());
    respond({ type: "formats", formats });
}

export function handleCreateCustomFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { formatCode } = params as { formatCode: string };
    const result = JSON.parse(engine.createCustomFormat(formatCode)) as {
        success?: boolean;
        format?: {
            category?: string;
            decimals?: number;
            separator?: boolean;
            currency?: string;
            formatCode?: string;
            effectiveFormatCode?: string;
            base64?: string;
        };
        error?: string;
    };
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        // Return format properties (content-addressed format system)
        respond({ type: "formatCreated", success: true, format: result.format });
    }
}

export function handleGetFormulaFunctions(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const functions = JSON.parse(engine.getFormulaFunctions());
    respond({ type: "functions", functions });
}

export function handleGetNamedRanges(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const namedRanges = JSON.parse(engine.getNamedRanges());
    respond({ type: "namedRanges", namedRanges });
}

export function handleGetCellFormatId(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = JSON.parse(engine.getCellFormatId(cellId)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "formatId", formatId: result.formatId });
    }
}

export function handleParseUserInputValue(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { input } = params as { input: string };
    const result = JSON.parse(engine.parseUserInputValue(input));
    respond({ type: "parsedInput", ...result });
}

export function handleFormatCellValue(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { value, formatId } = params as { value: number; formatId: string };
    const result = JSON.parse(
        engine.formatCellValue(value, formatId),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "formattedValue", text: result.text });
    }
}

export function handleFormatWithCode(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { value, formatCode } = params as {
        value: number;
        formatCode: string;
    };
    const result = JSON.parse(
        engine.formatWithCode(value, formatCode),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "formatWithCode", error: result.error });
    } else {
        respond({ type: "formatWithCode", text: result.text });
    }
}

export function handleFormatCellById(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = JSON.parse(engine.formatCellById(cellId)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "formattedValue", text: result.text });
    }
}

export function handleGetFormatDetails(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { formatId } = params as { formatId: string };
    const result = JSON.parse(engine.getFormatDetails(formatId));
    respond({ type: "formatDetails", ...result });
}

export function handleMakeFormatId(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { category, decimals, separator, currency } = params as {
        category: string;
        decimals: number;
        separator: boolean;
        currency: string;
    };
    const result = JSON.parse(
        engine.makeFormatId(category, decimals, separator, currency),
    );
    respond({ type: "formatIdGenerated", ...result });
}

// ============================================================================
// Cell Style Operations
// ============================================================================

export function handleSetCellStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId, styleJson } = params as { cellId: string; styleJson: string };
    const result = JSON.parse(engine.setCellStyle(cellId, styleJson)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "styleSet", success: true });
    }
}

export function handleSetCellStyleAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row, styleJson } = params as {
        col: number;
        row: number;
        styleJson: string;
    };
    const result = JSON.parse(engine.setCellStyleAt(col, row, styleJson)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "styleSet", success: true });
    }
}

export function handleGetCellStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = JSON.parse(engine.getCellStyle(cellId));
    respond({ type: "cellStyle", ...result });
}

export function handleGetCellStyleAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = JSON.parse(engine.getCellStyleAt(col, row));
    respond({ type: "cellStyle", ...result });
}

export function handleGetAvailableStyles(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = JSON.parse(engine.getAvailableStyles());
    respond({ type: "availableStyles", styles: result });
}

// ============================================================================
// Range Style Operations
// ============================================================================

export function handleSetRangeStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { startCol, startRow, endCol, endRow, styleJson } = params as {
        startCol: number;
        startRow: number;
        endCol: number;
        endRow: number;
        styleJson: string;
    };
    const result = JSON.parse(
        engine.setRangeStyle(startCol, startRow, endCol, endRow, styleJson),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rangeStyleSet", success: true, rangeId: result.rangeId, styleId: result.styleId });
    }
}

export function handleRemoveRangeStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = JSON.parse(engine.removeRangeStyle(col, row)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rangeStyleRemoved", success: true });
    }
}

export function handleSetRangeStyleOnSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { sheetIndex, startCol, startRow, endCol, endRow, styleJson } = params as {
        sheetIndex: number;
        startCol: number;
        startRow: number;
        endCol: number;
        endRow: number;
        styleJson: string;
    };
    const result = JSON.parse(
        engine.setRangeStyleOnSheet(sheetIndex, startCol, startRow, endCol, endRow, styleJson),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rangeStyleSet", success: true, rangeId: result.rangeId, styleId: result.styleId });
    }
}

// ============================================================================
// Effective Style Operations
// ============================================================================

export function handleGetEffectiveCellStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = JSON.parse(engine.getEffectiveCellStyle(col, row));
    respond({ type: "effectiveCellStyle", ...result });
}

export function handleGetEffectiveStyleForRange(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col1, row1, col2, row2 } = params as {
        col1: number;
        row1: number;
        col2: number;
        row2: number;
    };
    const result = JSON.parse(engine.getEffectiveStyleForRange(col1, row1, col2, row2));
    respond({ type: "effectiveStyleForRange", ...result });
}

// ============================================================================
// Axis Style Operations (entire column/row styles)
// ============================================================================

export function handleSetColumnStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colPosition, styleJson } = params as {
        colPosition: number;
        styleJson: string;
    };
    const result = JSON.parse(engine.setColumnStyle(colPosition, styleJson)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnStyleSet", success: true });
    }
}

export function handleSetRowStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowPosition, styleJson } = params as {
        rowPosition: number;
        styleJson: string;
    };
    const result = JSON.parse(engine.setRowStyle(rowPosition, styleJson)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowStyleSet", success: true });
    }
}

export function handleGetColumnStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colPosition } = params as { colPosition: number };
    const result = JSON.parse(engine.getColumnStyle(colPosition));
    respond({ type: "columnStyle", style: result });
}

export function handleGetRowStyle(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowPosition } = params as { rowPosition: number };
    const result = JSON.parse(engine.getRowStyle(rowPosition));
    respond({ type: "rowStyle", style: result });
}

// ============================================================================
// Axis Format Operations (column/row formats)
// ============================================================================

export function handleSetColumnFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colPosition, format } = params as {
        colPosition: number;
        format: Record<string, unknown>;
    };
    const formatJson = JSON.stringify(format);
    const result = JSON.parse(engine.setColumnFormat(colPosition, formatJson)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnFormatSet", success: true });
    }
}

export function handleSetRowFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowPosition, format } = params as {
        rowPosition: number;
        format: Record<string, unknown>;
    };
    const formatJson = JSON.stringify(format);
    const result = JSON.parse(engine.setRowFormat(rowPosition, formatJson)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowFormatSet", success: true });
    }
}

export function handleClearColumnFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colPosition } = params as { colPosition: number };
    const result = JSON.parse(engine.clearColumnFormat(colPosition)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnFormatCleared", success: true });
    }
}

export function handleClearRowFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowPosition } = params as { rowPosition: number };
    const result = JSON.parse(engine.clearRowFormat(rowPosition)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowFormatCleared", success: true });
    }
}

export function handleGetColumnFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colPosition } = params as { colPosition: number };
    const result = JSON.parse(engine.getColumnFormat(colPosition));
    respond({ type: "columnFormat", format: result });
}

export function handleGetRowFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowPosition } = params as { rowPosition: number };
    const result = JSON.parse(engine.getRowFormat(rowPosition));
    respond({ type: "rowFormat", format: result });
}

// ============================================================================
// Range Format Operations
// ============================================================================

export function handleSetRangeFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { startCol, startRow, endCol, endRow, format } = params as {
        startCol: number;
        startRow: number;
        endCol: number;
        endRow: number;
        format: Record<string, unknown>;
    };
    const formatJson = JSON.stringify(format);
    const result = JSON.parse(
        engine.setRangeFormat(startCol, startRow, endCol, endRow, formatJson),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rangeFormatSet", success: true, range_id: result.range_id });
    }
}

export function handleSetRangeFormatOnSheet(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { sheetIndex, startCol, startRow, endCol, endRow, format } = params as {
        sheetIndex: number;
        startCol: number;
        startRow: number;
        endCol: number;
        endRow: number;
        format: Record<string, unknown>;
    };
    const formatJson = JSON.stringify(format);
    const result = JSON.parse(
        engine.setRangeFormatOnSheet(sheetIndex, startCol, startRow, endCol, endRow, formatJson),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rangeFormatSet", success: true, range_id: result.range_id });
    }
}

export function handleRemoveRangeFormat(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = JSON.parse(engine.removeRangeFormat(col, row)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rangeFormatRemoved", success: true });
    }
}

// ============================================================================
// Column/Row Operations
// ============================================================================

export function handleResizeColumn(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colId, width } = params as { colId: string; width: number };
    const result = JSON.parse(engine.resizeColumn(colId, width)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnResized", success: true });
    }
}

export function handleResizeColumnByPos(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { pos, width } = params as { pos: number; width: number };
    const result = JSON.parse(
        engine.resizeColumnByPos(pos, width),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnResized", id: result.id, success: true });
    }
}

export function handleResizeRow(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowId, height } = params as { rowId: string; height: number };
    const result = JSON.parse(engine.resizeRow(rowId, height)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowResized", success: true });
    }
}

export function handleResizeRowByPos(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { pos, height } = params as { pos: number; height: number };
    const result = JSON.parse(engine.resizeRowByPos(pos, height)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowResized", id: result.id, success: true });
    }
}

export function handleRenameColumn(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colId, name } = params as { colId: string; name: string };
    const result = JSON.parse(engine.renameColumn(colId, name)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnRenamed", success: true });
    }
}

export function handleRenameColumnByPos(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { pos, name } = params as { pos: number; name: string };
    const result = JSON.parse(
        engine.renameColumnByPos(pos, name),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnRenamed", id: result.id, success: true });
    }
}

export function handleMoveColumn(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colId, targetPos } = params as {
        colId: string;
        targetPos: number;
    };
    const result = JSON.parse(
        engine.moveColumn(colId, targetPos),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnMoved", success: true });
    }
}

export function handleMoveRow(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowId, targetPos } = params as {
        rowId: string;
        targetPos: number;
    };
    const result = JSON.parse(engine.moveRow(rowId, targetPos)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowMoved", success: true });
    }
}

export function handleShiftColumnsForEmptyMove(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { sourcePos, targetPos } = params as {
        sourcePos: number;
        targetPos: number;
    };
    const result = JSON.parse(
        engine.shiftColumnsForEmptyMove(sourcePos, targetPos),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnsShifted", success: true });
    }
}

export function handleShiftRowsForEmptyMove(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { sourcePos, targetPos } = params as {
        sourcePos: number;
        targetPos: number;
    };
    const result = JSON.parse(
        engine.shiftRowsForEmptyMove(sourcePos, targetPos),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowsShifted", success: true });
    }
}

export function handleInsertColumnAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { position, insertBefore } = params as {
        position: number;
        insertBefore: boolean;
    };
    const result = JSON.parse(
        engine.insertColumnAt(position, insertBefore),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({
            type: "columnInserted",
            id: result.id,
            position: result.position,
            success: true,
        });
    }
}

export function handleInsertRowAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { position, insertBefore } = params as {
        position: number;
        insertBefore: boolean;
    };
    const result = JSON.parse(
        engine.insertRowAt(position, insertBefore),
    ) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({
            type: "rowInserted",
            id: result.id,
            position: result.position,
            success: true,
        });
    }
}

export function handleDeleteColumnById(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { colId } = params as { colId: string };
    const result = JSON.parse(engine.deleteColumnById(colId)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "columnDeleted", success: true });
    }
}

export function handleDeleteRowById(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { rowId } = params as { rowId: string };
    const result = JSON.parse(engine.deleteRowById(rowId)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "rowDeleted", success: true });
    }
}

export function handleFillRange(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const {
        sourceMinCol,
        sourceMinRow,
        sourceMaxCol,
        sourceMaxRow,
        targetMinCol,
        targetMinRow,
        targetMaxCol,
        targetMaxRow,
    } = params as {
        sourceMinCol: number;
        sourceMinRow: number;
        sourceMaxCol: number;
        sourceMaxRow: number;
        targetMinCol: number;
        targetMinRow: number;
        targetMaxCol: number;
        targetMaxRow: number;
    };
    const result = JSON.parse(
        engine.fillRange(
            sourceMinCol,
            sourceMinRow,
            sourceMaxCol,
            sourceMaxRow,
            targetMinCol,
            targetMinRow,
            targetMaxCol,
            targetMaxRow,
        ),
    ) as JsonResult & { cellsFilled?: number };
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({
            type: "rangeFilled",
            success: true,
            cellsFilled: result.cellsFilled ?? 0,
        });
    }
}

// ============================================================================
// Merge Cell Operations
// ============================================================================

export function handleAddMergeRange(
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
    const result = JSON.parse(
        engine.addMergeRange(startCol, startRow, endCol, endRow),
    ) as JsonResult & { colSpan?: number; rowSpan?: number };
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({
            type: "mergeRangeAdded",
            success: true,
            colSpan: result.colSpan ?? 0,
            rowSpan: result.rowSpan ?? 0,
        });
    }
}

export function handleRemoveMergeRange(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = JSON.parse(engine.removeMergeRange(col, row)) as JsonResult;
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "mergeRangeRemoved", success: true });
    }
}

// ============================================================================
// Workbook Operations
// ============================================================================

export function handleHasFormulas(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const hasFormulas = engine.hasFormulas();
    respond({ type: "hasFormulas", hasFormulas });
}

export function handleCreateEmpty(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.createEmptyWorkbook();
    respond({ type: "created", sheetCount: 1 });
}

export function handleSetWorkbookName(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { name } = params as { name: string };
    engine.setWorkbookName(name);
    respond({ type: "nameSet", success: true });
}

export function handleGetWorkbookName(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const name = engine.getWorkbookName();
    respond({ type: "workbookName", name });
}

// ============================================================================
// Theme Operations
// ============================================================================

export function handleGetTheme(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getTheme();
    const theme = JSON.parse(result);
    respond({ type: "theme", theme });
}

export function handleGetCellStylePresets(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getCellStylePresets();
    const presets = JSON.parse(result);
    respond({ type: "cellStylePresets", presets });
}

export function handleGetBuiltinThemes(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getBuiltinThemes();
    const themes = JSON.parse(result);
    respond({ type: "builtinThemes", themes });
}

export function handleSetTheme(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { themeJson } = params as { themeJson: string };
    const result = engine.setTheme(themeJson);
    const parsed = JSON.parse(result);
    respond({ type: "setTheme", ...parsed });
}

// ============================================================================
// Debug Operations
// ============================================================================

export function handleDebugParseFormula(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { formulaText } = params as { formulaText: string };
    const result = engine.debugParseFormula(formulaText);
    respond({ type: "formulaParsed", result });
}

// ============================================================================
// Viewport Pixel Queries
// ============================================================================

export function handleGetColumnPixelOffset(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { position } = params as { position: number };
    const offset = engine.getColumnPixelOffset(position);
    respond({ type: "pixelOffset", offset });
}

export function handleGetRowPixelOffset(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { position } = params as { position: number };
    const offset = engine.getRowPixelOffset(position);
    respond({ type: "pixelOffset", offset });
}

export function handleGetTotalWidth(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const width = engine.getTotalWidth();
    respond({ type: "totalSize", size: width });
}

export function handleGetTotalHeight(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const height = engine.getTotalHeight();
    respond({ type: "totalSize", size: height });
}

// ============================================================================
// Formula API
// ============================================================================

export function handleValidateFormula(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { formulaText } = params as { formulaText: string };
    const result = engine.validateFormula(formulaText);
    respond({ type: "formulaValidated", result });
}

export function handleGetFormulaDisplay(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = engine.getFormulaDisplay(cellId);
    respond({ type: "formulaDisplay", result });
}

export function handleGetCellDependencies(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = engine.getCellDependencies(cellId);
    respond({ type: "cellDependencies", result });
}

export function handleGetCellDependents(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = engine.getCellDependents(cellId);
    respond({ type: "cellDependents", result });
}

export function handleGetFormulaReferences(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { formulaText } = params as { formulaText: string };
    const result = engine.getFormulaReferences(formulaText);
    respond({ type: "formulaReferences", result });
}

export function handleGetReferencesFromPartial(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { formulaText } = params as { formulaText: string };
    const result = engine.getReferencesFromPartial(formulaText);
    respond({ type: "formulaReferences", result });
}

export function handleDetectCircularRef(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { cellId } = params as { cellId: string };
    const result = engine.detectCircularRef(cellId);
    respond({ type: "circularRef", result });
}

export function handleGetVolatileCells(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const result = engine.getVolatileCells();
    respond({ type: "volatileCells", result });
}

// ============================================================================
// Scripting (Luau)
// ============================================================================

export function handleExecuteScript(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { script } = params as { script: string };
    const result = engine.executeScript(script);
    respond({ type: "scriptExecuted", result });
}

export function handleTokenizeLuau(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { source } = params as { source: string };
    const result = engine.tokenizeLuau(source);
    respond({ type: "tokenized", result });
}

export function handleGetAutocomplete(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { source, line, column } = params as {
        source: string;
        line: number;
        column: number;
    };
    const result = engine.getAutocomplete(source, line, column);
    respond({ type: "autocomplete", result });
}

// ============================================================================
// Spill Range Queries
// ============================================================================

export function handleGetSpillRangeAt(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { col, row } = params as { col: number; row: number };
    const result = engine.getSpillRangeAt(col, row);
    respond({ type: "spillRange", result });
}

// ============================================================================
// AI Agent
// ============================================================================

export function handleInitAgent(
    engine: CellsEngine,
    params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const { serverUrl } = params as { serverUrl: string };
    engine.initAgent(serverUrl);
    respond({ type: "agentInitialized", success: true });
}

export function handleIsAgentInitialized(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const initialized = engine.isAgentInitialized();
    respond({ type: "agentStatus", initialized });
}

export function handleGetAgentConversationId(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const conversationId = engine.getAgentConversationId();
    respond({ type: "agentConversationId", conversationId });
}

export function handleClearAgentConversation(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.clearAgentConversation();
    respond({ type: "agentConversationCleared", success: true });
}

export function handleCancelAgent(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    engine.cancelAgent();
    respond({ type: "agentCancelled", success: true });
}

export function handleIsAgentProcessing(
    engine: CellsEngine,
    _params: Record<string, unknown>,
    respond: RespondFn,
): void {
    const processing = engine.isAgentProcessing();
    respond({ type: "agentProcessing", processing });
}
