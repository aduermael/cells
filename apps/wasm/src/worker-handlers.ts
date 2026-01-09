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
        // data is ArrayBuffer, delimiter defaults to comma
        const content = new TextDecoder().decode(data as ArrayBuffer);
        const delimiter = (params.delimiter as string) || ",";
        const hasHeader = params.hasHeader !== false;
        result = JSON.parse(
            engine.loadFromCSV(content, delimiter.charCodeAt(0), hasHeader),
        ) as JsonResult;
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
        const binaryStr = engine.exportToXLSX();
        if (!binaryStr) {
            respond({
                type: "error",
                error: "XLSX export not available or failed",
            });
            return;
        }
        // Convert binary string to ArrayBuffer
        const bytes = new Uint8Array(binaryStr.length);
        for (let i = 0; i < binaryStr.length; i++) {
            bytes[i] = binaryStr.charCodeAt(i);
        }
        data = bytes.buffer;
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
    const { col, row, formatId } = params as {
        col: number;
        row: number;
        formatId: string;
    };
    const result = JSON.parse(
        engine.setCellFormatAt(col, row, formatId),
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
        formatId?: string;
        error?: string;
    };
    if (result.error) {
        respond({ type: "error", error: result.error });
    } else {
        respond({ type: "formatCreated", formatId: result.formatId });
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
