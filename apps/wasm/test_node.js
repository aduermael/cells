// Node.js test for cells WASM module
const fs = require('fs');
const path = require('path');

// Load the Emscripten-generated JS file
const wasmDir = __dirname;
const jsPath = path.join(wasmDir, 'cells_wasm_bin.js');

// Check if files exist
if (!fs.existsSync(jsPath)) {
    console.error('ERROR: cells_wasm_bin.js not found');
    console.log('Run: cp bazel-bin/apps/wasm/cells_wasm/*.js apps/wasm/');
    process.exit(1);
}

// Load module
const createCellsModule = require(jsPath);

async function runTests() {
    console.log('Loading WASM module...');

    const Module = await createCellsModule({
        locateFile: (path) => {
            return require('path').join(wasmDir, path);
        }
    });

    console.log('WASM module loaded successfully!\n');

    let passed = 0;
    let failed = 0;

    function test(name, fn) {
        try {
            fn();
            console.log(`  ✓ ${name}`);
            passed++;
        } catch (e) {
            console.log(`  ✗ ${name}`);
            console.log(`    Error: ${e.message}`);
            failed++;
        }
    }

    function assert(condition, message) {
        if (!condition) {
            throw new Error(message);
        }
    }

    // Create engine
    const engine = new Module.CellsEngine();

    console.log('Testing CellsEngine:');

    test('Create empty workbook', () => {
        engine.createEmptyWorkbook();
        const count = engine.getSheetCount();
        assert(count === 1, `Expected 1 sheet, got ${count}`);
    });

    test('Get sheet name', () => {
        const name = engine.getSheetName(0);
        assert(name === 'Sheet1', `Expected "Sheet1", got "${name}"`);
    });

    test('Get sheet info', () => {
        const info = JSON.parse(engine.getSheetInfo());
        assert(info.name === 'Sheet1', `Expected name "Sheet1"`);
        assert(info.colCount >= 26, `Expected >= 26 columns`);
        assert(info.rowCount >= 100, `Expected >= 100 rows`);
    });

    test('Create cell with value', () => {
        const result = JSON.parse(engine.createCell(0, 0, 'Hello'));
        assert(result.success === true, 'createCell should succeed');
        assert(result.id && result.id.length === 8, 'Should return 8-char ID');
    });

    test('Query viewport', () => {
        const viewport = JSON.parse(engine.queryViewport(0, 0, 10, 10));
        assert(Array.isArray(viewport.cells), 'Should have cells array');
        assert(viewport.cells.length >= 1, 'Should have at least 1 cell');
    });

    test('Update cell', () => {
        const viewport1 = JSON.parse(engine.queryViewport(0, 0, 10, 10));
        const cellId = viewport1.cells[0].id;

        const result = JSON.parse(engine.updateCell(cellId, '42'));
        assert(result.success === true, 'updateCell should succeed');

        const viewport2 = JSON.parse(engine.queryViewport(0, 0, 10, 10));
        const cell = viewport2.cells.find(c => c.id === cellId);
        assert(cell.value === '42', `Expected "42", got "${cell.value}"`);
        assert(cell.type === 'n', `Expected type "n", got "${cell.type}"`);
    });

    test('Create cell and update with formula', () => {
        // createCell stores value as-is, use updateCell for formula parsing
        const result = JSON.parse(engine.createCell(1, 0, ''));
        assert(result.success === true, 'createCell should succeed');

        // Now update with formula - this triggers formula parsing
        const updateResult = JSON.parse(engine.updateCell(result.id, '=A1+10'));
        assert(updateResult.success === true, 'updateCell with formula should succeed');

        const viewport = JSON.parse(engine.queryViewport(0, 0, 10, 10));
        const cell = viewport.cells.find(c => c.id === result.id);
        assert(cell.type === 'f', `Expected type "f", got "${cell.type}"`);
    });

    test('Load from .cells format', () => {
        const content = `D TEST123 "Test"
S SHEET01 "Data"
C COL00001 0 w:100
R ROW00001 0 h:24
X CELL0001 COL00001 ROW00001 n 42`;

        const result = JSON.parse(engine.loadFromCells(content));
        assert(result.success === true, 'loadFromCells should succeed');
        assert(result.sheetCount === 1, 'Should have 1 sheet');
    });

    test('Load from CSV', () => {
        const csv = 'A,B,C\n1,2,3\n4,5,6';
        const result = JSON.parse(engine.loadFromCSV(csv, 44, true));
        assert(result.success === true, 'loadFromCSV should succeed');
    });

    test('Export to .cells', () => {
        const exported = engine.exportToCells();
        assert(exported.length > 0, 'Should return content');
        assert(exported.includes('D '), 'Should have document line');
        assert(exported.includes('S '), 'Should have sheet line');
    });

    test('Export to CSV', () => {
        const exported = engine.exportToCSV();
        assert(exported.length > 0, 'Should return content');
    });

    test('Set/get workbook name', () => {
        engine.setWorkbookName('TestBook');
        const name = engine.getWorkbookName();
        assert(name === 'TestBook', `Expected "TestBook", got "${name}"`);
    });

    test('Column resize by position', () => {
        engine.createEmptyWorkbook();
        const result = JSON.parse(engine.resizeColumnByPos(0, 150));
        assert(result.success === true, 'Should succeed');
        assert(result.id && result.id.length === 8, 'Should return column ID');
    });

    test('Column resize by ID', () => {
        const colResult = JSON.parse(engine.resizeColumnByPos(1, 100));
        const result = JSON.parse(engine.resizeColumn(colResult.id, 200));
        assert(result.success === true, 'Should succeed');
    });

    test('XLSX stub returns error', () => {
        const result = JSON.parse(engine.loadFromXLSXData('fake'));
        assert(result.error !== undefined, 'Should return error in no-XLSX build');
    });

    // Clean up
    engine.delete();

    console.log(`\n${passed} passed, ${failed} failed`);
    process.exit(failed > 0 ? 1 : 0);
}

runTests().catch(e => {
    console.error('Test failed:', e);
    process.exit(1);
});
