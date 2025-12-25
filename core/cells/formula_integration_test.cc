#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_ast.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
#include "core/cells/parser.h"
#include "core/cells/serializer.h"

namespace cells {
namespace {

// Helper to create a basic test workbook with one sheet
std::unique_ptr<Workbook> createTestWorkbook() {
    auto wb = std::make_unique<Workbook>(ID("testWBId"), "TestWorkbook");
    auto sheet = std::make_unique<Sheet>(ID("testShId"), "Sheet1");

    // Create columns A, B, C (positions 0, 1, 2)
    auto colA = std::make_unique<Axis>(ID("colA0001"), true);
    colA->position = 0;
    auto colB = std::make_unique<Axis>(ID("colB0002"), true);
    colB->position = 1;
    auto colC = std::make_unique<Axis>(ID("colC0003"), true);
    colC->position = 2;

    // Create rows 1, 2, 3 (positions 0, 1, 2)
    auto row1 = std::make_unique<Axis>(ID("row10001"), false);
    row1->position = 0;
    auto row2 = std::make_unique<Axis>(ID("row20002"), false);
    row2->position = 1;
    auto row3 = std::make_unique<Axis>(ID("row30003"), false);
    row3->position = 2;

    // Create cells
    auto cellA1 = std::make_unique<Cell>(ID("cellA101"), ID("colA0001"), ID("row10001"));
    cellA1->value = CellValue(10.0);

    auto cellB1 = std::make_unique<Cell>(ID("cellB101"), ID("colB0002"), ID("row10001"));
    cellB1->value = CellValue(20.0);

    auto cellC1 = std::make_unique<Cell>(ID("cellC101"), ID("colC0003"), ID("row10001"));
    // C1 will be our formula cell

    auto cellA2 = std::make_unique<Cell>(ID("cellA201"), ID("colA0001"), ID("row20002"));
    cellA2->value = CellValue(30.0);

    // Add to sheet
    sheet->addColumn(std::move(colA));
    sheet->addColumn(std::move(colB));
    sheet->addColumn(std::move(colC));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    sheet->addRow(std::move(row3));
    sheet->addCell(std::move(cellA1));
    sheet->addCell(std::move(cellB1));
    sheet->addCell(std::move(cellC1));
    sheet->addCell(std::move(cellA2));

    wb->addSheet(std::move(sheet));
    return wb;
}

// ============================================================================
// Formula::parse() tests
// ============================================================================

TEST(FormulaIntegrationTest, FormulaParse) {
    Formula f("=1+2");
    EXPECT_TRUE(f.parse());
    EXPECT_NE(f.ast, nullptr);
    EXPECT_TRUE(f.isValid());
}

TEST(FormulaIntegrationTest, FormulaParseInvalid) {
    // Formula without '=' prefix - parse() returns false for empty text
    Formula f("");
    EXPECT_FALSE(f.parse());
}

TEST(FormulaIntegrationTest, FormulaParseWithError) {
    Formula f("=1+");        // Incomplete
    EXPECT_TRUE(f.parse());  // Parser creates error node
    EXPECT_NE(f.ast, nullptr);
    EXPECT_FALSE(f.isValid());  // Has errors
}

TEST(FormulaIntegrationTest, FormulaHasVolatile) {
    Formula f1("=NOW()");
    EXPECT_TRUE(f1.parse());
    EXPECT_TRUE(f1.hasVolatile());

    Formula f2("=SUM(1,2)");
    EXPECT_TRUE(f2.parse());
    EXPECT_FALSE(f2.hasVolatile());
}

TEST(FormulaIntegrationTest, FormulaHasVolatileNested) {
    Formula f("=IF(A1>0,NOW(),0)");
    EXPECT_TRUE(f.parse());
    EXPECT_TRUE(f.hasVolatile());
}

// ============================================================================
// Sheet formula management tests
// ============================================================================

TEST(FormulaIntegrationTest, SetCellFormulaUnresolved) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    Cell* cellC1 = sheet->getCell(ID("cellC101"));
    ASSERT_NE(cellC1, nullptr);

    auto result = sheet->setCellFormulaUnresolved(ID("cellC101"), "=A1+B1");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(cellC1->isFormula());

    std::string formulaText = sheet->getCellFormulaText(ID("cellC101"));
    EXPECT_EQ(formulaText, "=A1+B1");
}

TEST(FormulaIntegrationTest, SetCellFormulaUnresolvedInvalidFormula) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Missing '=' prefix
    auto result = sheet->setCellFormulaUnresolved(ID("cellC101"), "A1+B1");
    EXPECT_FALSE(result.success);
}

TEST(FormulaIntegrationTest, SetCellFormulaCellNotFound) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    auto result = sheet->setCellFormulaUnresolved(ID("nonexist"), "=1+2");
    EXPECT_FALSE(result.success);
}

TEST(FormulaIntegrationTest, ClearCellFormula) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    sheet->setCellFormulaUnresolved(ID("cellC101"), "=A1+B1");
    Cell* cellC1 = sheet->getCell(ID("cellC101"));
    EXPECT_TRUE(cellC1->isFormula());

    sheet->clearCellFormula(ID("cellC101"));
    EXPECT_FALSE(cellC1->isFormula());
}

TEST(FormulaIntegrationTest, GetCellFormulaTextNoFormula) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    std::string text = sheet->getCellFormulaText(ID("cellA101"));
    EXPECT_TRUE(text.empty());
}

// ============================================================================
// Dependency graph integration tests
// ============================================================================

TEST(FormulaIntegrationTest, DependencyGraphTracking) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse and resolve a formula to get proper AST
    FormulaParser parser("=A1+B1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    auto resolveResult = resolver.resolve(ast.get());
    EXPECT_TRUE(resolveResult.success);

    // Set formula with resolved AST
    auto result = sheet->setCellFormula(ID("cellC101"), "=A1+B1", ast.release());
    EXPECT_TRUE(result.success);

    // Check dependency graph
    DependencyGraph* depGraph = sheet->getDependencyGraph();
    ASSERT_NE(depGraph, nullptr);

    auto deps = depGraph->getDependencies(ID("cellC101"));
    EXPECT_EQ(deps.size(), 2u);  // A1 and B1
}

// ============================================================================
// UUID storage format tests (5f.4)
// ============================================================================

TEST(FormulaIntegrationTest, SetCellFormulaStoresUuidFormat) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse and resolve a formula
    FormulaParser parser("=A1+B1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    auto resolveResult = resolver.resolve(ast.get());
    EXPECT_TRUE(resolveResult.success);

    // Set formula with resolved AST - this should store UUID format
    auto result = sheet->setCellFormula(ID("cellC101"), "=A1+B1", ast.release());
    EXPECT_TRUE(result.success);

    // Get the stored formula text - it should be in UUID format
    std::string storedFormula = sheet->getCellFormulaText(ID("cellC101"));

    // The stored format should use ~~ prefix for relative refs
    // E.g., "=~~cellA101+~~cellB101" (exact IDs from test setup)
    EXPECT_TRUE(storedFormula.find("~~") != std::string::npos)
        << "Expected UUID format with ~~ prefix, got: " << storedFormula;

    // Should contain the cell IDs
    EXPECT_TRUE(storedFormula.find("cellA101") != std::string::npos)
        << "Expected cellA101 in formula, got: " << storedFormula;
    EXPECT_TRUE(storedFormula.find("cellB101") != std::string::npos)
        << "Expected cellB101 in formula, got: " << storedFormula;

    // Should NOT contain A1 notation
    EXPECT_TRUE(storedFormula.find("A1") == std::string::npos ||
                storedFormula.find("cellA1") != std::string::npos)
        << "Should not contain A1 notation, got: " << storedFormula;
}

TEST(FormulaIntegrationTest, SetCellFormulaAbsoluteRefUuidFormat) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse formula with absolute refs
    FormulaParser parser("=$A$1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    auto resolveResult = resolver.resolve(ast.get());
    EXPECT_TRUE(resolveResult.success);

    auto result = sheet->setCellFormula(ID("cellC101"), "=$A$1", ast.release());
    EXPECT_TRUE(result.success);

    std::string storedFormula = sheet->getCellFormulaText(ID("cellC101"));

    // Should use $$ prefix for both absolute
    EXPECT_TRUE(storedFormula.find("$$") != std::string::npos)
        << "Expected UUID format with $$ prefix for absolute ref, got: " << storedFormula;
    EXPECT_TRUE(storedFormula.find("cellA101") != std::string::npos)
        << "Expected cellA101 in formula, got: " << storedFormula;
}

TEST(FormulaIntegrationTest, SetCellFormulaMixedAbsoluteRefUuidFormat) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse formula with mixed absolute ref ($A1 = col absolute, row relative)
    FormulaParser parser("=$A1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    auto resolveResult = resolver.resolve(ast.get());
    EXPECT_TRUE(resolveResult.success);

    auto result = sheet->setCellFormula(ID("cellC101"), "=$A1", ast.release());
    EXPECT_TRUE(result.success);

    std::string storedFormula = sheet->getCellFormulaText(ID("cellC101"));

    // Should use $~ prefix for col absolute, row relative
    EXPECT_TRUE(storedFormula.find("$~") != std::string::npos)
        << "Expected UUID format with $~ prefix, got: " << storedFormula;
}

// ============================================================================
// Display conversion tests (5f.5) - UUID to A1
// ============================================================================

TEST(FormulaIntegrationTest, DisplayConversionSimple) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse and resolve a formula
    FormulaParser parser("=A1+B1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    // Use FormulaDisplayConverter to convert back to A1
    FormulaDisplayConverter converter(*sheet);
    std::string display = converter.toDisplayString(ast.get());

    // Should display as A1 notation
    EXPECT_EQ(display, "=A1+B1");
}

TEST(FormulaIntegrationTest, DisplayConversionAbsolute) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    FormulaParser parser("=$A$1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=$A$1");
}

TEST(FormulaIntegrationTest, DisplayConversionRoundTrip) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse and resolve
    FormulaParser parser("=A1+B1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    // Store the resolved AST (now in UUID format)
    auto result = sheet->setCellFormula(ID("cellC101"), "=A1+B1", ast.release());
    EXPECT_TRUE(result.success);

    // Get the cell and its formula
    Cell* cell = sheet->getCell(ID("cellC101"));
    ASSERT_NE(cell, nullptr);
    ASSERT_NE(cell->getFormula(), nullptr);
    ASSERT_NE(cell->getFormula()->ast, nullptr);

    // Convert the stored AST to display format
    FormulaDisplayConverter converter(*sheet);
    std::string display = converter.toDisplayString(cell->getFormula()->ast);

    // Should display as A1 notation even though stored as UUID
    EXPECT_EQ(display, "=A1+B1");
}

TEST(FormulaIntegrationTest, DisplayConversionFunctionCall) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    FormulaParser parser("=SUM(A1,B1)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=SUM(A1,B1)");
}

TEST(FormulaIntegrationTest, DependencyGraphRemoval) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    FormulaParser parser("=A1+B1");
    auto ast = parser.parse();
    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    sheet->setCellFormula(ID("cellC101"), "=A1+B1", ast.release());

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    EXPECT_EQ(depGraph->size(), 1u);

    sheet->clearCellFormula(ID("cellC101"));
    EXPECT_EQ(depGraph->size(), 0u);
}

TEST(FormulaIntegrationTest, VolatileFunctionTracking) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    FormulaParser parser("=NOW()");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    sheet->setCellFormula(ID("cellC101"), "=NOW()", ast.release());

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    EXPECT_TRUE(depGraph->isVolatile(ID("cellC101")));

    auto volatileCells = depGraph->getVolatileCells();
    EXPECT_EQ(volatileCells.size(), 1u);
}

// ============================================================================
// Named ranges integration tests
// ============================================================================

TEST(FormulaIntegrationTest, NamedRangeRegistry) {
    auto wb = createTestWorkbook();
    NamedRangeRegistry* registry = wb->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    // Define a workbook-scoped named range
    bool defined = registry->defineWorkbook("TotalSales", NamedRangeTarget::cell(ID("cellA101")));
    EXPECT_TRUE(defined);

    // Resolve it
    Sheet* sheet = wb->getSheetByIndex(0);
    const NamedRange* resolved = registry->resolve("TotalSales", sheet->id);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->name, "TotalSales");
}

TEST(FormulaIntegrationTest, SheetScopedNamedRange) {
    auto wb = createTestWorkbook();
    NamedRangeRegistry* registry = wb->getNamedRanges();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Define a sheet-scoped named range
    bool defined =
        registry->defineSheet("LocalName", sheet->id, NamedRangeTarget::cell(ID("cellA101")));
    EXPECT_TRUE(defined);

    // Define a workbook-scoped with same name
    bool workbookDef =
        registry->defineWorkbook("LocalName", NamedRangeTarget::cell(ID("cellB101")));
    EXPECT_TRUE(workbookDef);

    // Sheet-scoped should shadow workbook-scoped
    const NamedRange* resolved = registry->resolve("LocalName", sheet->id);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->scope, NamedRangeScope::SHEET);
}

// ============================================================================
// Serialization round-trip tests
// ============================================================================

TEST(FormulaIntegrationTest, SerializationRoundTrip) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Set a formula
    sheet->setCellFormulaUnresolved(ID("cellC101"), "=A1+B1");

    // Serialize
    std::string serialized = serialize(*wb);
    EXPECT_FALSE(serialized.empty());

    // Parse back
    ParseResult result = parse(serialized);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    // Verify formula preserved
    Sheet* loadedSheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(loadedSheet, nullptr);

    Cell* loadedCell = loadedSheet->getCell(ID("cellC101"));
    ASSERT_NE(loadedCell, nullptr);
    EXPECT_TRUE(loadedCell->isFormula());

    Formula* loadedFormula = loadedCell->getFormula();
    ASSERT_NE(loadedFormula, nullptr);
    EXPECT_STREQ(loadedFormula->text, "=A1+B1");
}

TEST(FormulaIntegrationTest, SerializationWithMultipleFormulas) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Add another cell for formula
    auto cellB2 = std::make_unique<Cell>(ID("cellB201"), ID("colB0002"), ID("row20002"));
    sheet->addCell(std::move(cellB2));

    // Set formulas
    sheet->setCellFormulaUnresolved(ID("cellC101"), "=A1+B1");
    sheet->setCellFormulaUnresolved(ID("cellB201"), "=SUM(A1:A2)");

    // Serialize and parse
    std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    EXPECT_TRUE(result.ok());

    // Verify both formulas
    Sheet* loadedSheet = result.workbook->getSheetByIndex(0);
    Cell* c1 = loadedSheet->getCell(ID("cellC101"));
    Cell* b2 = loadedSheet->getCell(ID("cellB201"));

    ASSERT_NE(c1, nullptr);
    ASSERT_NE(b2, nullptr);
    EXPECT_TRUE(c1->isFormula());
    EXPECT_TRUE(b2->isFormula());
}

// ============================================================================
// UUID format load/save round-trip tests (5f.6)
// ============================================================================

TEST(FormulaIntegrationTest, UuidFormatSerializationRoundTrip) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse and resolve a formula to get UUID-format AST
    FormulaParser parser("=A1+B1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    auto resolveResult = resolver.resolve(ast.get());
    EXPECT_TRUE(resolveResult.success);

    // Set formula with resolved AST - this stores UUID format
    auto result = sheet->setCellFormula(ID("cellC101"), "=A1+B1", ast.release());
    EXPECT_TRUE(result.success);

    // Verify stored text is UUID format
    std::string storedBefore = sheet->getCellFormulaText(ID("cellC101"));
    EXPECT_TRUE(storedBefore.find("~~") != std::string::npos)
        << "Pre-serialize: expected UUID format, got: " << storedBefore;

    // Serialize to .zcd format
    std::string serialized = serialize(*wb);
    EXPECT_FALSE(serialized.empty());

    // Verify serialized format contains UUID refs
    EXPECT_TRUE(serialized.find("~~") != std::string::npos)
        << "Serialized: expected UUID format in file, got: " << serialized;

    // Parse back from .zcd
    ParseResult parseResult = parse(serialized);
    EXPECT_TRUE(parseResult.ok()) << "Parse failed: "
                                  << (parseResult.error ? parseResult.error->toString()
                                                        : "unknown");
    ASSERT_NE(parseResult.workbook, nullptr);

    // Get the loaded formula
    Sheet* loadedSheet = parseResult.workbook->getSheetByIndex(0);
    ASSERT_NE(loadedSheet, nullptr);

    Cell* loadedCell = loadedSheet->getCell(ID("cellC101"));
    ASSERT_NE(loadedCell, nullptr);
    EXPECT_TRUE(loadedCell->isFormula());

    Formula* loadedFormula = loadedCell->getFormula();
    ASSERT_NE(loadedFormula, nullptr);

    // Verify loaded formula text is still UUID format
    EXPECT_TRUE(std::string(loadedFormula->text).find("~~") != std::string::npos)
        << "Loaded formula should be UUID format, got: " << loadedFormula->text;

    // Parse the loaded formula to get AST
    EXPECT_TRUE(loadedFormula->parse());
    ASSERT_NE(loadedFormula->ast, nullptr);

    // Convert the loaded AST to display format - should show A1 notation
    FormulaDisplayConverter converter(*loadedSheet);
    std::string display = converter.toDisplayString(loadedFormula->ast);
    EXPECT_EQ(display, "=A1+B1") << "Display should be A1 notation, got: " << display;
}

TEST(FormulaIntegrationTest, UuidFormatAbsoluteRefRoundTrip) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse formula with absolute refs
    FormulaParser parser("=$A$1+B$2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    sheet->setCellFormula(ID("cellC101"), "=$A$1+B$2", ast.release());

    // Verify serialized contains various absolute markers
    std::string serialized = serialize(*wb);
    // $$ for both absolute, ~$ for row absolute
    EXPECT_TRUE(serialized.find("$$") != std::string::npos)
        << "Expected $$ for absolute ref, got: " << serialized;

    // Parse back and display
    ParseResult parseResult = parse(serialized);
    EXPECT_TRUE(parseResult.ok());

    Sheet* loadedSheet = parseResult.workbook->getSheetByIndex(0);
    Cell* loadedCell = loadedSheet->getCell(ID("cellC101"));
    ASSERT_NE(loadedCell, nullptr);

    Formula* loadedFormula = loadedCell->getFormula();
    EXPECT_TRUE(loadedFormula->parse());

    FormulaDisplayConverter converter(*loadedSheet);
    std::string display = converter.toDisplayString(loadedFormula->ast);
    EXPECT_EQ(display, "=$A$1+B$2");
}

TEST(FormulaIntegrationTest, UuidFormatRangeRefRoundTrip) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse formula with range ref
    FormulaParser parser("=SUM(A1:B2)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    sheet->setCellFormula(ID("cellC101"), "=SUM(A1:B2)", ast.release());

    // Serialize and parse back
    std::string serialized = serialize(*wb);
    ParseResult parseResult = parse(serialized);
    EXPECT_TRUE(parseResult.ok());

    Sheet* loadedSheet = parseResult.workbook->getSheetByIndex(0);
    Cell* loadedCell = loadedSheet->getCell(ID("cellC101"));
    ASSERT_NE(loadedCell, nullptr);

    Formula* loadedFormula = loadedCell->getFormula();
    EXPECT_TRUE(loadedFormula->parse());

    FormulaDisplayConverter converter(*loadedSheet);
    std::string display = converter.toDisplayString(loadedFormula->ast);
    EXPECT_EQ(display, "=SUM(A1:B2)");
}

TEST(FormulaIntegrationTest, UuidFormatFunctionCallRoundTrip) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Parse formula with nested function call
    FormulaParser parser("=IF(A1>0,B1,0)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*wb, *sheet, wb->getNamedRanges());
    resolver.resolve(ast.get());

    sheet->setCellFormula(ID("cellC101"), "=IF(A1>0,B1,0)", ast.release());

    std::string serialized = serialize(*wb);
    ParseResult parseResult = parse(serialized);
    EXPECT_TRUE(parseResult.ok());

    Sheet* loadedSheet = parseResult.workbook->getSheetByIndex(0);
    Cell* loadedCell = loadedSheet->getCell(ID("cellC101"));
    Formula* loadedFormula = loadedCell->getFormula();
    EXPECT_TRUE(loadedFormula->parse());

    FormulaDisplayConverter converter(*loadedSheet);
    std::string display = converter.toDisplayString(loadedFormula->ast);
    EXPECT_EQ(display, "=IF(A1>0,B1,0)");
}

// ============================================================================
// Workbook::getSheetByName tests
// ============================================================================

TEST(FormulaIntegrationTest, GetSheetByName) {
    auto wb = createTestWorkbook();

    Sheet* found = wb->getSheetByName("Sheet1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "Sheet1");

    Sheet* notFound = wb->getSheetByName("NonExistent");
    EXPECT_EQ(notFound, nullptr);
}

TEST(FormulaIntegrationTest, GetSheetByNameConst) {
    auto wb = createTestWorkbook();
    const Workbook* constWb = wb.get();

    const Sheet* found = constWb->getSheetByName("Sheet1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "Sheet1");
}

}  // namespace
}  // namespace cells
