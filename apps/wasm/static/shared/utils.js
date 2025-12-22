// Utilities Module
// Shared helper functions for the spreadsheet application

/**
 * Detect file format from filename and content
 * @param {string} filename - The filename to check
 * @param {ArrayBuffer} data - The file content
 * @returns {string} - Format: 'zcd', 'csv', or 'xlsx'
 */
export function detectFormat(filename, data) {
    const ext = filename.split('.').pop().toLowerCase();
    if (ext === 'zcd') return 'zcd';
    if (ext === 'csv' || ext === 'tsv') return 'csv';
    if (ext === 'xlsx') return 'xlsx';

    // Fallback: check magic bytes for XLSX (ZIP format)
    if (data instanceof ArrayBuffer) {
        const view = new Uint8Array(data.slice(0, 4));
        if (view[0] === 0x50 && view[1] === 0x4B) return 'xlsx';
    }
    return 'csv';
}

/**
 * Get base filename without extension
 * @param {string} filename - The filename
 * @returns {string} - Filename without extension
 */
export function getBaseName(filename) {
    const lastDot = filename.lastIndexOf('.');
    return lastDot > 0 ? filename.substring(0, lastDot) : filename;
}

/**
 * Convert column index to Excel-style letter (A, B, ..., Z, AA, AB, ...)
 * @param {number} col - Zero-based column index
 * @returns {string} - Column letter
 */
export function colToLetter(col) {
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
 * Convert Excel-style letter to column index
 * @param {string} letter - Column letter (A, B, ..., AA, etc.)
 * @returns {number} - Zero-based column index
 */
export function letterToCol(letter) {
    let col = 0;
    for (let i = 0; i < letter.length; i++) {
        col = col * 26 + (letter.charCodeAt(i) - 64);
    }
    return col - 1;
}

/**
 * Format a cell reference (e.g., "A1", "B5")
 * @param {number} col - Zero-based column index
 * @param {number} row - Zero-based row index
 * @returns {string} - Cell reference
 */
export function formatCellRef(col, row) {
    return colToLetter(col) + (row + 1);
}

/**
 * Parse a cell reference (e.g., "A1" -> { col: 0, row: 0 })
 * @param {string} ref - Cell reference
 * @returns {{ col: number, row: number } | null} - Parsed cell position or null
 */
export function parseCellRef(ref) {
    const match = ref.match(/^([A-Z]+)(\d+)$/i);
    if (!match) return null;
    return {
        col: letterToCol(match[1].toUpperCase()),
        row: parseInt(match[2], 10) - 1
    };
}

/**
 * Format a range reference (e.g., "A1:B5")
 * @param {number} startCol - Start column (zero-based)
 * @param {number} startRow - Start row (zero-based)
 * @param {number} endCol - End column (zero-based)
 * @param {number} endRow - End row (zero-based)
 * @returns {string} - Range reference
 */
export function formatRangeRef(startCol, startRow, endCol, endRow) {
    if (startCol === endCol && startRow === endRow) {
        return formatCellRef(startCol, startRow);
    }
    return formatCellRef(startCol, startRow) + ':' + formatCellRef(endCol, endRow);
}

/**
 * Create a debounced version of a function
 * @param {Function} fn - Function to debounce
 * @param {number} delay - Delay in milliseconds
 * @returns {Function} - Debounced function
 */
export function debounce(fn, delay) {
    let timeoutId;
    return function (...args) {
        clearTimeout(timeoutId);
        timeoutId = setTimeout(() => fn.apply(this, args), delay);
    };
}

/**
 * Create a throttled version of a function
 * @param {Function} fn - Function to throttle
 * @param {number} limit - Minimum time between calls in milliseconds
 * @returns {Function} - Throttled function
 */
export function throttle(fn, limit) {
    let inThrottle;
    return function (...args) {
        if (!inThrottle) {
            fn.apply(this, args);
            inThrottle = true;
            setTimeout(() => inThrottle = false, limit);
        }
    };
}

/**
 * Download a blob as a file
 * @param {Blob} blob - The blob to download
 * @param {string} filename - The filename
 */
export function downloadBlob(blob, filename) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

/**
 * Get MIME type for a file format
 * @param {string} format - File format ('xlsx', 'csv', 'zcd')
 * @returns {string} - MIME type
 */
export function getMimeType(format) {
    switch (format) {
        case 'xlsx':
            return 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet';
        case 'csv':
            return 'text/csv';
        case 'zcd':
        default:
            return 'text/plain';
    }
}

/**
 * Clamp a value between min and max
 * @param {number} value - Value to clamp
 * @param {number} min - Minimum value
 * @param {number} max - Maximum value
 * @returns {number} - Clamped value
 */
export function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
}
