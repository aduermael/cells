// Grid Renderer Module
// Handles all canvas rendering for the spreadsheet grid

// Grid constants
export const HEADER_HEIGHT = 24;
export const HEADER_WIDTH = 50;
export const DEFAULT_COL_WIDTH = 100;
export const DEFAULT_ROW_HEIGHT = 24;
export const CELL_PADDING = 4;

// Color palette
export const COLORS = {
    gridLine: '#f0f0f0',  // Subtle grid lines
    headerBg: '#f8f9fa',
    headerBorder: '#dee2e6',
    headerSeparator: 'rgba(0, 0, 0, 0.06)',  // Very subtle separators between header cells
    headerText: '#495057',
    cellText: '#212529',
    selectionBorder: '#0d6efd',
    selectionBg: 'rgba(13, 110, 253, 0.1)',
    cornerBg: '#e9ecef'
};

/**
 * GridRenderer handles all canvas drawing operations for the spreadsheet
 */
export class GridRenderer {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');

        // State references (set by setStateRefs)
        this.sheetInfo = null;
        this.cells = [];
        this.columns = [];
        this.rows = [];
        this.colWidths = new Map();
        this.rowHeights = new Map();
        this.colNames = new Map();
        this.scrollX = 0;
        this.scrollY = 0;
        this.selectedCell = null;
        this.selectedColumn = null;
        this.selectedRow = null;
        this.selectionStart = null;
        this.selectionEnd = null;

        // Drag state
        this.isDraggingColumn = false;
        this.isDraggingRow = false;
        this.dragSourceIndex = -1;
        this.dragTargetIndex = -1;
        this.dragMouseX = 0;
        this.dragMouseY = 0;

        // Resize state
        this.isResizing = false;
        this.resizePreviewX = 0;
        this.isResizingRow = false;
        this.resizePreviewY = 0;

        // Editing state
        this.editingColumnIndex = -1;
    }

    /**
     * Update state references from the main application
     */
    setStateRefs(state) {
        Object.assign(this, state);
    }

    /**
     * Resize the canvas to fit its container
     */
    resizeCanvas() {
        const container = this.canvas.parentElement;
        const dpr = window.devicePixelRatio || 1;
        this.canvas.width = container.clientWidth * dpr;
        this.canvas.height = container.clientHeight * dpr;
        this.canvas.style.width = container.clientWidth + 'px';
        this.canvas.style.height = container.clientHeight + 'px';
        this.ctx.scale(dpr, dpr);
        this.render();
    }

    /**
     * Convert column index to Excel-style letter (A, B, ..., Z, AA, AB, ...)
     */
    colToLetter(col) {
        let s = '';
        let n = col + 1;
        while (n > 0) {
            n--;
            s = String.fromCharCode(65 + (n % 26)) + s;
            n = Math.floor(n / 26);
        }
        return s;
    }

    /**
     * Get display text for a column header (custom name or default letter)
     */
    getColumnHeaderText(col) {
        const customName = this.colNames.get(col);
        return customName || this.colToLetter(col);
    }

    /**
     * Get normalized range selection (min/max coordinates)
     */
    getNormalizedRange() {
        if (!this.selectionStart || !this.selectionEnd) return null;
        return {
            minCol: Math.min(this.selectionStart.col, this.selectionEnd.col),
            maxCol: Math.max(this.selectionStart.col, this.selectionEnd.col),
            minRow: Math.min(this.selectionStart.row, this.selectionEnd.row),
            maxRow: Math.max(this.selectionStart.row, this.selectionEnd.row)
        };
    }

    /**
     * Check if we have a multi-cell range selection
     */
    hasRangeSelection() {
        if (!this.selectionStart || !this.selectionEnd) return false;
        return this.selectionStart.col !== this.selectionEnd.col ||
               this.selectionStart.row !== this.selectionEnd.row;
    }

    /**
     * Get the visual X position for a column during drag operations.
     * The placeholder gap should match the width of the dragged column.
     */
    getDragAdjustedColX(col) {
        const colHasMoved = this.isDraggingColumn &&
            this.dragTargetIndex !== this.dragSourceIndex &&
            this.dragTargetIndex !== this.dragSourceIndex + 1;

        if (!colHasMoved) {
            let x = HEADER_WIDTH - this.scrollX;
            for (let i = 0; i < col; i++) {
                x += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            return x;
        }

        const sourceW = this.colWidths.get(this.dragSourceIndex) || DEFAULT_COL_WIDTH;
        let x = HEADER_WIDTH - this.scrollX;

        // When moving left (target < source): columns at target and after shift right
        // When moving right (target > source + 1): columns between source+1 and target-1 shift left
        if (this.dragTargetIndex < this.dragSourceIndex) {
            // Moving left: add gap at target position
            for (let i = 0; i < col; i++) {
                if (i === this.dragSourceIndex) continue;
                x += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            // If this column is at or after the target, add the gap
            if (col >= this.dragTargetIndex && col !== this.dragSourceIndex) {
                x += sourceW;
            }
        } else {
            // Moving right: add gap after target-1
            for (let i = 0; i < col; i++) {
                if (i === this.dragSourceIndex) continue;
                x += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
                if (i === this.dragTargetIndex - 1) {
                    x += sourceW;
                }
            }
        }
        return x;
    }

    /**
     * Get the visual Y position for a row during drag operations.
     * The placeholder gap should match the height of the dragged row.
     */
    getDragAdjustedRowY(row) {
        const rowHasMoved = this.isDraggingRow &&
            this.dragTargetIndex !== this.dragSourceIndex &&
            this.dragTargetIndex !== this.dragSourceIndex + 1;

        if (!rowHasMoved) {
            let y = HEADER_HEIGHT - this.scrollY;
            for (let i = 0; i < row; i++) {
                y += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }
            return y;
        }

        const sourceH = this.rowHeights.get(this.dragSourceIndex) || DEFAULT_ROW_HEIGHT;
        let y = HEADER_HEIGHT - this.scrollY;

        // When moving up (target < source): rows at target and after shift down
        // When moving down (target > source + 1): rows between source+1 and target-1 shift up
        if (this.dragTargetIndex < this.dragSourceIndex) {
            // Moving up: add gap at target position
            for (let i = 0; i < row; i++) {
                if (i === this.dragSourceIndex) continue;
                y += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }
            // If this row is at or after the target, add the gap
            if (row >= this.dragTargetIndex && row !== this.dragSourceIndex) {
                y += sourceH;
            }
        } else {
            // Moving down: add gap after target-1
            for (let i = 0; i < row; i++) {
                if (i === this.dragSourceIndex) continue;
                y += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
                if (i === this.dragTargetIndex - 1) {
                    y += sourceH;
                }
            }
        }
        return y;
    }

    /**
     * Main render function - draws the entire grid
     */
    render() {
        if (!this.sheetInfo) return;

        const container = this.canvas.parentElement;
        const viewWidth = container.clientWidth;
        const viewHeight = container.clientHeight;
        const ctx = this.ctx;

        ctx.clearRect(0, 0, viewWidth, viewHeight);

        // Draw cells area (clipped)
        ctx.save();
        ctx.beginPath();
        ctx.rect(HEADER_WIDTH, HEADER_HEIGHT, viewWidth - HEADER_WIDTH, viewHeight - HEADER_HEIGHT);
        ctx.clip();

        const colHasMoved = this.isDraggingColumn &&
            this.dragTargetIndex !== this.dragSourceIndex &&
            this.dragTargetIndex !== this.dragSourceIndex + 1;
        const rowHasMoved = this.isDraggingRow &&
            this.dragTargetIndex !== this.dragSourceIndex &&
            this.dragTargetIndex !== this.dragSourceIndex + 1;

        // Grid lines
        ctx.strokeStyle = COLORS.gridLine;
        ctx.lineWidth = 1;

        // Vertical lines
        for (let col = 0; col <= this.sheetInfo.colCount; col++) {
            if (colHasMoved && col === this.dragSourceIndex) continue;
            const lineX = this.getDragAdjustedColX(col) + 0.5;
            if (lineX >= HEADER_WIDTH && lineX < viewWidth) {
                ctx.beginPath();
                ctx.moveTo(lineX, HEADER_HEIGHT);
                ctx.lineTo(lineX, viewHeight);
                ctx.stroke();
            }
        }

        // Horizontal lines
        for (let row = 0; row <= this.sheetInfo.rowCount; row++) {
            if (rowHasMoved && row === this.dragSourceIndex) continue;
            const lineY = this.getDragAdjustedRowY(row) + 0.5;
            if (lineY >= HEADER_HEIGHT && lineY < viewHeight) {
                ctx.beginPath();
                ctx.moveTo(HEADER_WIDTH, lineY);
                ctx.lineTo(viewWidth, lineY);
                ctx.stroke();
            }
        }

        // Cell values
        ctx.fillStyle = COLORS.cellText;
        ctx.font = '13px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';

        for (const cell of this.cells) {
            if (colHasMoved && cell.col === this.dragSourceIndex) continue;
            if (rowHasMoved && cell.row === this.dragSourceIndex) continue;

            const colWidth = this.colWidths.get(cell.col) || DEFAULT_COL_WIDTH;
            const rowHeight = this.rowHeights.get(cell.row) || DEFAULT_ROW_HEIGHT;
            const cellX = this.getDragAdjustedColX(cell.col);
            const cellY = this.getDragAdjustedRowY(cell.row);

            if (cellX + colWidth < HEADER_WIDTH || cellX > viewWidth) continue;
            if (cellY + rowHeight < HEADER_HEIGHT || cellY > viewHeight) continue;

            const displayValue = cell.display || cell.value || '';
            ctx.save();
            ctx.beginPath();
            ctx.rect(cellX + 1, cellY + 1, colWidth - 2, rowHeight - 2);
            ctx.clip();
            ctx.fillText(displayValue, cellX + CELL_PADDING, cellY + rowHeight / 2);
            ctx.restore();
        }

        // Column selection (highlight entire column)
        if (this.selectedColumn !== null && !this.isDraggingColumn) {
            let selX = HEADER_WIDTH - this.scrollX;
            for (let i = 0; i < this.selectedColumn; i++) {
                selX += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            const selW = this.colWidths.get(this.selectedColumn) || DEFAULT_COL_WIDTH;

            if (selX + selW > HEADER_WIDTH && selX < viewWidth) {
                ctx.fillStyle = COLORS.selectionBg;
                ctx.fillRect(Math.max(HEADER_WIDTH, selX), HEADER_HEIGHT,
                    Math.min(selW, selX + selW - HEADER_WIDTH), viewHeight - HEADER_HEIGHT);
            }
        }

        // Row selection (highlight entire row)
        if (this.selectedRow !== null && !this.isDraggingRow) {
            let selY = HEADER_HEIGHT - this.scrollY;
            for (let i = 0; i < this.selectedRow; i++) {
                selY += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }
            const selH = this.rowHeights.get(this.selectedRow) || DEFAULT_ROW_HEIGHT;

            if (selY + selH > HEADER_HEIGHT && selY < viewHeight) {
                ctx.fillStyle = COLORS.selectionBg;
                ctx.fillRect(HEADER_WIDTH, Math.max(HEADER_HEIGHT, selY),
                    viewWidth - HEADER_WIDTH, Math.min(selH, selY + selH - HEADER_HEIGHT));
            }
        }

        // Cell/Range selection
        const range = this.getNormalizedRange();
        if (range) {
            // Calculate range bounds
            let rangeX = HEADER_WIDTH - this.scrollX;
            for (let i = 0; i < range.minCol; i++) {
                rangeX += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            let rangeY = HEADER_HEIGHT - this.scrollY;
            for (let i = 0; i < range.minRow; i++) {
                rangeY += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }

            // Calculate total width and height of range
            let rangeW = 0;
            for (let i = range.minCol; i <= range.maxCol; i++) {
                rangeW += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            let rangeH = 0;
            for (let i = range.minRow; i <= range.maxRow; i++) {
                rangeH += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }

            // Draw range fill
            if (rangeX + rangeW > HEADER_WIDTH && rangeX < viewWidth &&
                rangeY + rangeH > HEADER_HEIGHT && rangeY < viewHeight) {
                ctx.fillStyle = COLORS.selectionBg;
                ctx.fillRect(
                    Math.max(HEADER_WIDTH, rangeX),
                    Math.max(HEADER_HEIGHT, rangeY),
                    Math.min(rangeW, rangeX + rangeW - Math.max(HEADER_WIDTH, rangeX)),
                    Math.min(rangeH, rangeY + rangeH - Math.max(HEADER_HEIGHT, rangeY))
                );

                // Draw range border
                ctx.strokeStyle = COLORS.selectionBorder;
                ctx.lineWidth = 2;
                ctx.strokeRect(
                    Math.max(HEADER_WIDTH, rangeX) + 1,
                    Math.max(HEADER_HEIGHT, rangeY) + 1,
                    Math.min(rangeW, rangeX + rangeW - Math.max(HEADER_WIDTH, rangeX)) - 2,
                    Math.min(rangeH, rangeY + rangeH - Math.max(HEADER_HEIGHT, rangeY)) - 2
                );
            }

            // Draw anchor cell highlight (the cell where selection started)
            // This is only needed for multi-cell ranges to show the "active" cell
            if (this.hasRangeSelection() && this.selectionStart) {
                let anchorX = HEADER_WIDTH - this.scrollX;
                for (let i = 0; i < this.selectionStart.col; i++) {
                    anchorX += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
                }
                let anchorY = HEADER_HEIGHT - this.scrollY;
                for (let i = 0; i < this.selectionStart.row; i++) {
                    anchorY += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
                }
                const anchorW = this.colWidths.get(this.selectionStart.col) || DEFAULT_COL_WIDTH;
                const anchorH = this.rowHeights.get(this.selectionStart.row) || DEFAULT_ROW_HEIGHT;

                // Draw anchor cell with white background and thicker border
                if (anchorX + anchorW > HEADER_WIDTH && anchorX < viewWidth &&
                    anchorY + anchorH > HEADER_HEIGHT && anchorY < viewHeight) {
                    const clipX = Math.max(HEADER_WIDTH, anchorX);
                    const clipY = Math.max(HEADER_HEIGHT, anchorY);
                    const clipW = Math.min(anchorW, anchorX + anchorW - clipX);
                    const clipH = Math.min(anchorH, anchorY + anchorH - clipY);

                    // White background
                    ctx.fillStyle = '#ffffff';
                    ctx.fillRect(clipX + 1, clipY + 1, clipW - 2, clipH - 2);

                    // Thicker border for anchor cell
                    ctx.strokeStyle = COLORS.selectionBorder;
                    ctx.lineWidth = 3;
                    ctx.strokeRect(clipX + 1.5, clipY + 1.5, clipW - 3, clipH - 3);
                }
            }
        } else if (this.selectedCell) {
            // Fallback for single cell selection without range state
            let selX = HEADER_WIDTH - this.scrollX;
            for (let i = 0; i < this.selectedCell.col; i++) {
                selX += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            let selY = HEADER_HEIGHT - this.scrollY;
            for (let i = 0; i < this.selectedCell.row; i++) {
                selY += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }
            const selW = this.colWidths.get(this.selectedCell.col) || DEFAULT_COL_WIDTH;
            const selH = this.rowHeights.get(this.selectedCell.row) || DEFAULT_ROW_HEIGHT;

            if (selX + selW > HEADER_WIDTH && selX < viewWidth &&
                selY + selH > HEADER_HEIGHT && selY < viewHeight) {
                ctx.fillStyle = COLORS.selectionBg;
                ctx.fillRect(selX, selY, selW, selH);
                ctx.strokeStyle = COLORS.selectionBorder;
                ctx.lineWidth = 2;
                ctx.strokeRect(selX + 1, selY + 1, selW - 2, selH - 2);
            }
        }

        ctx.restore();

        // Column headers
        ctx.fillStyle = COLORS.headerBg;
        ctx.fillRect(HEADER_WIDTH, 0, viewWidth - HEADER_WIDTH, HEADER_HEIGHT);

        ctx.font = '12px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';

        for (let col = 0; col < this.sheetInfo.colCount; col++) {
            if (colHasMoved && col === this.dragSourceIndex) continue;
            const colW = this.colWidths.get(col) || DEFAULT_COL_WIDTH;
            const headerX = this.getDragAdjustedColX(col);
            if (headerX >= viewWidth || headerX + colW < HEADER_WIDTH) continue;

            // Check if column is in selection range or is selected column
            let isSelected = this.selectedColumn === col;
            if (!isSelected && this.selectedCell && this.selectedCell.col === col) {
                isSelected = true;
            }
            if (!isSelected && range && col >= range.minCol && col <= range.maxCol) {
                isSelected = true;
            }

            if (isSelected && !this.isDraggingColumn) {
                ctx.fillStyle = COLORS.selectionBorder;
                ctx.fillRect(Math.max(HEADER_WIDTH, headerX), 0, Math.min(colW, headerX + colW - HEADER_WIDTH), HEADER_HEIGHT);
                ctx.fillStyle = '#fff';
            } else {
                ctx.fillStyle = COLORS.headerText;
            }
            // Skip drawing header text if this column is being edited (editor covers it)
            if (col !== this.editingColumnIndex) {
                ctx.fillText(this.getColumnHeaderText(col), headerX + colW / 2, HEADER_HEIGHT / 2);
            }
        }

        // Column header separators (vertical lines between A, B, C...)
        ctx.strokeStyle = COLORS.headerSeparator;
        ctx.lineWidth = 1;
        for (let col = 0; col < this.sheetInfo.colCount; col++) {
            if (colHasMoved && col === this.dragSourceIndex) continue;
            const lineX = this.getDragAdjustedColX(col) + 0.5;
            if (lineX > HEADER_WIDTH && lineX < viewWidth) {
                ctx.beginPath();
                ctx.moveTo(lineX, 0);
                ctx.lineTo(lineX, HEADER_HEIGHT);
                ctx.stroke();
            }
        }

        // Row headers
        ctx.fillStyle = COLORS.headerBg;
        ctx.fillRect(0, HEADER_HEIGHT, HEADER_WIDTH, viewHeight - HEADER_HEIGHT);

        for (let row = 0; row < this.sheetInfo.rowCount; row++) {
            if (rowHasMoved && row === this.dragSourceIndex) continue;
            const rowH = this.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
            const headerY = this.getDragAdjustedRowY(row);
            if (headerY >= viewHeight || headerY + rowH < HEADER_HEIGHT) continue;

            // Check if row is in selection range or is selected row
            let isSelected = this.selectedRow === row;
            if (!isSelected && this.selectedCell && this.selectedCell.row === row) {
                isSelected = true;
            }
            if (!isSelected && range && row >= range.minRow && row <= range.maxRow) {
                isSelected = true;
            }

            if (isSelected && !this.isDraggingRow) {
                ctx.fillStyle = COLORS.selectionBorder;
                ctx.fillRect(0, Math.max(HEADER_HEIGHT, headerY), HEADER_WIDTH, Math.min(rowH, headerY + rowH - HEADER_HEIGHT));
                ctx.fillStyle = '#fff';
            } else {
                ctx.fillStyle = COLORS.headerText;
            }
            ctx.fillText(String(row + 1), HEADER_WIDTH / 2, headerY + rowH / 2);
        }

        // Row header separators (horizontal lines between 1, 2, 3...)
        ctx.strokeStyle = COLORS.headerSeparator;
        ctx.lineWidth = 1;
        for (let row = 0; row < this.sheetInfo.rowCount; row++) {
            if (rowHasMoved && row === this.dragSourceIndex) continue;
            const lineY = this.getDragAdjustedRowY(row) + 0.5;
            if (lineY > HEADER_HEIGHT && lineY < viewHeight) {
                ctx.beginPath();
                ctx.moveTo(0, lineY);
                ctx.lineTo(HEADER_WIDTH, lineY);
                ctx.stroke();
            }
        }

        // Corner
        ctx.fillStyle = COLORS.cornerBg;
        ctx.fillRect(0, 0, HEADER_WIDTH, HEADER_HEIGHT);

        // Header borders
        ctx.strokeStyle = COLORS.headerBorder;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, HEADER_HEIGHT + 0.5);
        ctx.lineTo(viewWidth, HEADER_HEIGHT + 0.5);
        ctx.moveTo(HEADER_WIDTH + 0.5, 0);
        ctx.lineTo(HEADER_WIDTH + 0.5, viewHeight);
        ctx.stroke();
    }

    /**
     * Draw the resize preview line
     */
    drawResizePreview() {
        const container = this.canvas.parentElement;
        const viewWidth = container.clientWidth;
        const viewHeight = container.clientHeight;
        const ctx = this.ctx;

        ctx.save();
        ctx.strokeStyle = COLORS.selectionBorder;
        ctx.lineWidth = 2;
        ctx.setLineDash([4, 4]);

        if (this.isResizing) {
            ctx.beginPath();
            ctx.moveTo(this.resizePreviewX + 0.5, 0);
            ctx.lineTo(this.resizePreviewX + 0.5, viewHeight);
            ctx.stroke();
        }

        if (this.isResizingRow) {
            ctx.beginPath();
            ctx.moveTo(0, this.resizePreviewY + 0.5);
            ctx.lineTo(viewWidth, this.resizePreviewY + 0.5);
            ctx.stroke();
        }

        ctx.setLineDash([]);
        ctx.restore();
    }

    /**
     * Draw the drag ghost for column/row reordering
     */
    drawDragGhost() {
        if (!this.isDraggingColumn && !this.isDraggingRow) return;

        const container = this.canvas.parentElement;
        const viewWidth = container.clientWidth;
        const viewHeight = container.clientHeight;
        const ctx = this.ctx;

        ctx.save();
        ctx.globalAlpha = 0.6;

        if (this.isDraggingColumn) {
            const colW = this.colWidths.get(this.dragSourceIndex) || DEFAULT_COL_WIDTH;
            const ghostX = this.dragMouseX - colW / 2;

            ctx.fillStyle = COLORS.selectionBorder;
            ctx.fillRect(ghostX, 0, colW, HEADER_HEIGHT);

            ctx.fillStyle = '#fff';
            ctx.font = '12px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText(this.getColumnHeaderText(this.dragSourceIndex), ghostX + colW / 2, HEADER_HEIGHT / 2);

            ctx.fillStyle = COLORS.selectionBg;
            ctx.fillRect(ghostX, HEADER_HEIGHT, colW, viewHeight - HEADER_HEIGHT);

            ctx.strokeStyle = COLORS.selectionBorder;
            ctx.lineWidth = 2;
            ctx.strokeRect(ghostX, 0, colW, viewHeight);
        } else if (this.isDraggingRow) {
            const rowH = this.rowHeights.get(this.dragSourceIndex) || DEFAULT_ROW_HEIGHT;
            const ghostY = this.dragMouseY - rowH / 2;

            ctx.fillStyle = COLORS.selectionBorder;
            ctx.fillRect(0, ghostY, HEADER_WIDTH, rowH);

            ctx.fillStyle = '#fff';
            ctx.font = '12px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText(String(this.dragSourceIndex + 1), HEADER_WIDTH / 2, ghostY + rowH / 2);

            ctx.fillStyle = COLORS.selectionBg;
            ctx.fillRect(HEADER_WIDTH, ghostY, viewWidth - HEADER_WIDTH, rowH);

            ctx.strokeStyle = COLORS.selectionBorder;
            ctx.lineWidth = 2;
            ctx.strokeRect(0, ghostY, viewWidth, rowH);
        }

        ctx.restore();
    }
}
