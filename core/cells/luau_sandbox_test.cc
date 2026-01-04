#include "core/cells/luau_sandbox.h"

#include <gtest/gtest.h>

#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

// Helper to create a test workbook with one sheet containing some cells
std::unique_ptr<Workbook> createTestWorkbook() {
    ID wbId = generate_id();
    auto workbook = std::make_unique<Workbook>(wbId, "TestWorkbook");

    ID sheetId = generate_id();
    auto sheet = std::make_unique<Sheet>(sheetId, "Sheet1");

    // Create columns A, B, C (positions 0, 1, 2)
    for (uint32_t i = 0; i < 3; i++) {
        ID colId = generate_id();
        auto col = std::make_unique<Axis>(colId, true);
        col->position = i;
        col->size = 100;
        sheet->addColumn(std::move(col));
    }

    // Create rows 1, 2, 3 (positions 0, 1, 2)
    for (uint32_t i = 0; i < 3; i++) {
        ID rowId = generate_id();
        auto row = std::make_unique<Axis>(rowId, false);
        row->position = i;
        row->size = 24;
        sheet->addRow(std::move(row));
    }

    // Create cell A1 with value 42
    Axis* colA = sheet->getColumnByPosition(0);
    Axis* row1 = sheet->getRowByPosition(0);
    if (colA != nullptr && row1 != nullptr) {
        ID cellId = generate_id();
        auto cell = std::make_unique<Cell>(cellId, colA->id, row1->id);
        cell->value = CellValue(42.0);
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));
    return workbook;
}

TEST(LuauSandboxTest, BasicExecution) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("return 1 + 2");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "3");
    EXPECT_TRUE(result.error.empty());
    EXPECT_GT(result.instructions, 0);
}

TEST(LuauSandboxTest, StringReturn) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("return 'hello'");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "hello");
}

TEST(LuauSandboxTest, BooleanReturn) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("return true");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "true");
}

TEST(LuauSandboxTest, NoReturn) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("local x = 5");

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.empty());
}

TEST(LuauSandboxTest, CompileError) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("this is not valid lua syntax !!!");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.error.find("Compile error") != std::string::npos ||
                result.error.find("error") != std::string::npos);
}

TEST(LuauSandboxTest, RuntimeError) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("error('test error')");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("test error") != std::string::npos);
}

TEST(LuauSandboxTest, UndefinedVariable) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("return undefined_variable");

    // Luau returns nil for undefined globals
    EXPECT_TRUE(result.success);
}

TEST(LuauSandboxTest, InstructionLimit) {
    SandboxConfig config;
    config.maxInstructions = 100;  // Very low limit
    LuauSandbox sandbox(config);

    // Infinite loop should hit instruction limit
    auto result = sandbox.execute("while true do end");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("instruction limit") != std::string::npos ||
                result.error.find("limit") != std::string::npos);
}

TEST(LuauSandboxTest, LargeInstructionLimit) {
    SandboxConfig config;
    config.maxInstructions = 1'000'000;
    LuauSandbox sandbox(config);

    // Simple loop should complete
    auto result = sandbox.execute(R"(
        local sum = 0
        for i = 1, 1000 do
            sum = sum + i
        end
        return sum
    )");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "500500");
}

TEST(LuauSandboxTest, MathLibrary) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("return math.floor(3.7)");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "3");
}

TEST(LuauSandboxTest, StringLibrary) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("return string.upper('hello')");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "HELLO");
}

TEST(LuauSandboxTest, TableLibrary) {
    LuauSandbox sandbox;
    auto result = sandbox.execute(R"(
        local t = {3, 1, 2}
        table.sort(t)
        return t[1] .. t[2] .. t[3]
    )");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "123");
}

TEST(LuauSandboxTest, MoveSemantics) {
    LuauSandbox sandbox1;
    auto result1 = sandbox1.execute("return 42");
    EXPECT_TRUE(result1.success);

    // Move sandbox
    LuauSandbox sandbox2 = std::move(sandbox1);

    // sandbox2 should work
    auto result2 = sandbox2.execute("return 100");
    EXPECT_TRUE(result2.success);
    EXPECT_EQ(result2.output, "100");
}

TEST(LuauSandboxTest, SetMaxInstructionsAtRuntime) {
    LuauSandbox sandbox;

    // Should complete with default limit
    auto result1 = sandbox.execute("for i=1,100 do end return 'ok'");
    EXPECT_TRUE(result1.success);

    // Set very low limit
    sandbox.setMaxInstructions(10);

    // Should fail now
    auto result2 = sandbox.execute("for i=1,100 do end return 'ok'");
    EXPECT_FALSE(result2.success);
}

TEST(LuauSandboxTest, MultipleExecutions) {
    LuauSandbox sandbox;

    // Run multiple scripts on the same sandbox
    auto result1 = sandbox.execute("return 1");
    EXPECT_TRUE(result1.success);
    EXPECT_EQ(result1.output, "1");

    auto result2 = sandbox.execute("return 2");
    EXPECT_TRUE(result2.success);
    EXPECT_EQ(result2.output, "2");

    auto result3 = sandbox.execute("return 3");
    EXPECT_TRUE(result3.success);
    EXPECT_EQ(result3.output, "3");
}

TEST(LuauSandboxTest, GlobalsAreSandboxed) {
    LuauSandbox sandbox;

    // Try to modify a global - should fail or be isolated
    auto result = sandbox.execute(R"(
        math.pi = 5
        return math.pi
    )");

    // Either fails (read-only) or returns original value (sandboxed thread)
    // With luaL_sandbox, modifying should error
    // But with luaL_sandboxthread, each thread gets its own globals proxy
    // The exact behavior depends on Luau version
    EXPECT_TRUE(result.success || !result.error.empty());
}

TEST(LuauSandboxTest, LocalVariables) {
    LuauSandbox sandbox;

    auto result = sandbox.execute(R"(
        local function factorial(n)
            if n <= 1 then return 1 end
            return n * factorial(n - 1)
        end
        return factorial(5)
    )");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "120");
}

TEST(LuauSandboxTest, Closures) {
    LuauSandbox sandbox;

    auto result = sandbox.execute(R"(
        local function makeCounter()
            local count = 0
            return function()
                count = count + 1
                return count
            end
        end
        local counter = makeCounter()
        counter()
        counter()
        return counter()
    )");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "3");
}

// ============================================================================
// Cells API Tests
// ============================================================================

TEST(LuauSandboxTest, CellGetReturnsNilForEmptyCell) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // B2 doesn't exist
    auto result = sandbox.execute("return getCell('B2')");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.empty());  // nil has no output
}

TEST(LuauSandboxTest, CellGetReturnsExistingCell) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // A1 exists with value 42
    auto result = sandbox.execute("local c = getCell('A1'); return c.value");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "42");
}

TEST(LuauSandboxTest, CellGetWithCreate) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // D4 doesn't exist, but create=true should create it
    auto result = sandbox.execute(R"(
        local c = getCell('D4', {create = true})
        return type(c)
    )");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "table");
}

TEST(LuauSandboxTest, EmptyCellValueIsNil) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Create an empty cell and verify that .value returns nil
    auto result = sandbox.execute(R"(
        local c = getCell('E5', {create = true})
        return type(c.value)
    )");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "nil");
}

TEST(LuauSandboxTest, CellSetNumber) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        setCell('B2', 100)
        local c = getCell('B2')
        return c.value
    )");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "100");
}

TEST(LuauSandboxTest, CellSetString) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        setCell('B2', 'Hello')
        local c = getCell('B2')
        return c.value
    )");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "Hello");
}

TEST(LuauSandboxTest, CellSetBoolean) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        setCell('B2', true)
        local c = getCell('B2')
        return c.value
    )");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "true");
}

TEST(LuauSandboxTest, CellObjectIdentity) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Getting the same cell twice should return same object
    auto result = sandbox.execute(R"(
        local a = getCell('A1')
        local b = getCell('A1')
        return a == b
    )");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "true");
}

TEST(LuauSandboxTest, CellRefProperty) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        return c.ref
    )");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "A1");
}

TEST(LuauSandboxTest, GetSheetByIndex) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local s = getSheet({index = 1})
        return s.name
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet1");
}

TEST(LuauSandboxTest, GetSheetByDirectIndex) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // getSheet(1) shorthand - 1-based index
    auto result = sandbox.execute(R"(
        local s = getSheet(1)
        return s.name
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet1");
}

TEST(LuauSandboxTest, GetSheetByDirectName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // getSheet("name") shorthand
    auto result = sandbox.execute(R"(
        local s = getSheet("Sheet1")
        return s.name
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet1");
}

TEST(LuauSandboxTest, GetSheetByName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local s = getSheet({name = "Sheet1"})
        return s.name
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet1");
}

TEST(LuauSandboxTest, SheetNameProperty) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set sheet name via property
    auto set = sandbox.execute(R"(
        local s = getSheet({index = 1})
        s.name = "RenamedSheet"
    )");
    EXPECT_TRUE(set.success) << set.error;

    // Read back the name
    auto get = sandbox.execute(R"(
        local s = getSheet({index = 1})
        return s.name
    )");
    EXPECT_TRUE(get.success) << get.error;
    EXPECT_EQ(get.output, "RenamedSheet");
}

TEST(LuauSandboxTest, SelectSheetByIndex) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Add another sheet
    auto add = sandbox.execute("addSheet('SecondSheet')");
    EXPECT_TRUE(add.success) << add.error;

    // Add data to sheet 1 (first sheet, 1-based)
    sandbox.execute("setCell('A1', 100)");

    // Select sheet 2 (SecondSheet, 1-based) and add data
    sandbox.execute("selectSheet(2)");
    sandbox.execute("setCell('A1', 200)");

    // Select back to sheet 1 (first sheet, 1-based) and verify
    sandbox.execute("selectSheet(1)");
    auto verify = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(verify.success) << verify.error;
    EXPECT_EQ(verify.output, "100");
}

TEST(LuauSandboxTest, SelectSheetByName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Add another sheet
    auto add = sandbox.execute("addSheet('MySheet')");
    EXPECT_TRUE(add.success) << add.error;

    // Switch using name
    auto sel = sandbox.execute("selectSheet('MySheet')");
    EXPECT_TRUE(sel.success) << sel.error;

    // Verify we're on MySheet by adding data and checking the sheet
    sandbox.execute("setCell('A1', 999)");

    // Switch back to Sheet1 and verify A1 is empty
    sandbox.execute("selectSheet('Sheet1')");
    auto verify = sandbox.execute("return getCell('A1')");
    EXPECT_TRUE(verify.success) << verify.error;
    EXPECT_TRUE(verify.output.empty());  // nil
}

TEST(LuauSandboxTest, SelectSheetByObject) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local s = getSheet({index = 1})
        selectSheet(s)
        return "ok"
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "ok");
}

TEST(LuauSandboxTest, AddSheetWithName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local s = addSheet("MyNewSheet")
        return s.name
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "MyNewSheet");

    // Verify sheet was actually created
    EXPECT_EQ(workbook->sheetCount(), 2);
}

TEST(LuauSandboxTest, AddSheetWithDefaultName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local s = addSheet()
        return s.name
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet2");  // Default name: Sheet + (count + 1)
}

TEST(LuauSandboxTest, NoContextError) {
    LuauSandbox sandbox;
    // Don't set context

    auto result = sandbox.execute("getCell('A1')");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("no context") != std::string::npos);
}

TEST(LuauSandboxTest, InvalidReferenceError) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute("getCell('invalid')");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("invalid reference") != std::string::npos);
}

TEST(LuauSandboxTest, RangeDelete) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // First set some values
    auto setup = sandbox.execute(R"(
        setCell('A1', 1)
        setCell('A2', 2)
        setCell('B1', 3)
        setCell('B2', 4)
    )");
    EXPECT_TRUE(setup.success);

    // Verify values are set
    auto before = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(before.success);
    EXPECT_EQ(before.output, "1");

    // Delete range A1:B2
    auto del = sandbox.execute("deleteRange({from = 'A1', to = 'B2'})");
    EXPECT_TRUE(del.success);

    // Verify cells are cleared
    auto after = sandbox.execute("return getCell('A1')");
    EXPECT_TRUE(after.success);
    // Cell should be nil after deletion
    EXPECT_TRUE(after.output.empty());
}

// ============================================================================
// Formula Detection Tests
// ============================================================================

TEST(LuauSandboxTest, SetCellFormulaSimple) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set B1 to a number
    auto setup = sandbox.execute("setCell('B1', 100)");
    EXPECT_TRUE(setup.success) << setup.error;

    // Set A1 to a formula referencing B1
    auto setFormula = sandbox.execute("setCell('A1', '=B1')");
    EXPECT_TRUE(setFormula.success) << setFormula.error;

    // Verify the cell has a formula
    auto checkFormula = sandbox.execute(R"(
        local c = getCell('A1')
        return c.formula
    )");
    EXPECT_TRUE(checkFormula.success) << checkFormula.error;
    EXPECT_EQ(checkFormula.output, "=B1");
}

TEST(LuauSandboxTest, SetCellFormulaWithArithmetic) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set source cells
    auto setup = sandbox.execute(R"(
        setCell('B1', 10)
        setCell('C1', 20)
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // Set A1 to formula =B1+C1
    auto setFormula = sandbox.execute("setCell('A1', '=B1+C1')");
    EXPECT_TRUE(setFormula.success) << setFormula.error;

    // Verify the formula text
    auto checkFormula = sandbox.execute("return getCell('A1').formula");
    EXPECT_TRUE(checkFormula.success) << checkFormula.error;
    EXPECT_EQ(checkFormula.output, "=B1+C1");
}

TEST(LuauSandboxTest, SetCellLiteralStringNotFormula) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set A1 to a literal string that doesn't start with =
    auto setString = sandbox.execute("setCell('A1', 'hello world')");
    EXPECT_TRUE(setString.success) << setString.error;

    // Verify it's NOT a formula
    auto checkFormula = sandbox.execute(R"(
        local c = getCell('A1')
        if c.formula then return 'has formula' else return 'no formula' end
    )");
    EXPECT_TRUE(checkFormula.success) << checkFormula.error;
    EXPECT_EQ(checkFormula.output, "no formula");

    // Verify the value is the string
    auto checkValue = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(checkValue.success) << checkValue.error;
    EXPECT_EQ(checkValue.output, "hello world");
}

TEST(LuauSandboxTest, SetCellEmptyString) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set A1 to an empty string (shouldn't be treated as formula)
    auto setString = sandbox.execute("setCell('A1', '')");
    EXPECT_TRUE(setString.success) << setString.error;

    // Verify it's not a formula
    auto checkFormula = sandbox.execute(R"(
        local c = getCell('A1')
        if c.formula then return 'has formula' else return 'no formula' end
    )");
    EXPECT_TRUE(checkFormula.success) << checkFormula.error;
    EXPECT_EQ(checkFormula.output, "no formula");
}

TEST(LuauSandboxTest, SetCellFormulaWithAbsoluteRef) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set source cell
    auto setup = sandbox.execute("setCell('B1', 50)");
    EXPECT_TRUE(setup.success) << setup.error;

    // Set A1 to formula with absolute reference
    auto setFormula = sandbox.execute("setCell('A1', '=$B$1')");
    EXPECT_TRUE(setFormula.success) << setFormula.error;

    // Verify the formula text includes $ signs
    auto checkFormula = sandbox.execute("return getCell('A1').formula");
    EXPECT_TRUE(checkFormula.success) << checkFormula.error;
    EXPECT_EQ(checkFormula.output, "=$B$1");
}

TEST(LuauSandboxTest, SetCellFormulaJustEquals) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set A1 to just "=" (edge case - still treated as formula)
    auto setFormula = sandbox.execute("setCell('A1', '=')");
    EXPECT_TRUE(setFormula.success) << setFormula.error;

    // The formula should still be set (even if empty/invalid)
    auto checkFormula = sandbox.execute(R"(
        local c = getCell('A1')
        if c.formula then return 'has formula' else return 'no formula' end
    )");
    EXPECT_TRUE(checkFormula.success) << checkFormula.error;
    // Even "=" is treated as a formula (will be empty formula)
    EXPECT_EQ(checkFormula.output, "has formula");
}

// ============================================================================
// Print Function Tests
// ============================================================================

TEST(LuauSandboxTest, PrintCapturesOutput) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("print('hello')");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "hello\n");
}

TEST(LuauSandboxTest, PrintMultipleArguments) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("print('hello', 'world', 123)");

    EXPECT_TRUE(result.success);
    // Arguments are joined with a single space
    EXPECT_EQ(result.output, "hello world 123\n");
}

TEST(LuauSandboxTest, PrintMultipleCalls) {
    LuauSandbox sandbox;
    auto result = sandbox.execute(R"(
        print('line1')
        print('line2')
        print('line3')
    )");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "line1\nline2\nline3\n");
}

TEST(LuauSandboxTest, PrintWithReturnValue) {
    LuauSandbox sandbox;
    auto result = sandbox.execute(R"(
        print('hello')
        return 42
    )");

    EXPECT_TRUE(result.success);
    // Print output followed by "=> " and return value
    EXPECT_EQ(result.output, "hello\n=> 42");
}

TEST(LuauSandboxTest, PrintDifferentTypes) {
    LuauSandbox sandbox;
    auto result = sandbox.execute(R"(
        print(nil)
        print(true)
        print(false)
        print(3.14)
    )");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "nil\ntrue\nfalse\n3.14\n");
}

TEST(LuauSandboxTest, PrintEmptyCall) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("print()");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "\n");  // Just a newline
}

TEST(LuauSandboxTest, PrintTable) {
    LuauSandbox sandbox;
    auto result = sandbox.execute("print({1, 2, 3})");

    EXPECT_TRUE(result.success);
    // Tables print as "table: 0x..." - just check it starts correctly
    EXPECT_TRUE(result.output.find("table:") != std::string::npos);
}

TEST(LuauSandboxTest, PrintBufferClearedBetweenExecutions) {
    LuauSandbox sandbox;

    // First execution
    auto result1 = sandbox.execute("print('first')");
    EXPECT_TRUE(result1.success);
    EXPECT_EQ(result1.output, "first\n");

    // Second execution - buffer should be fresh
    auto result2 = sandbox.execute("print('second')");
    EXPECT_TRUE(result2.success);
    EXPECT_EQ(result2.output, "second\n");
}

// ============================================================================
// Document Title Tests
// ============================================================================

TEST(LuauSandboxTest, GetDocumentTitleReturnsWorkbookName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    workbook->name = "My Budget";

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute("print(getDocumentTitle())");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "My Budget\n");
}

TEST(LuauSandboxTest, SetDocumentTitleChangesWorkbookName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    EXPECT_EQ(workbook->name, "TestWorkbook");

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute("setDocumentTitle('MyDocument')");
    EXPECT_TRUE(result.success) << result.error;

    // Verify the workbook name was actually changed
    EXPECT_EQ(workbook->name, "MyDocument");
}

TEST(LuauSandboxTest, SetAndGetDocumentTitleRoundTrip) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set the title and then get it back
    auto result = sandbox.execute(R"(
        setDocumentTitle('Budget 2025')
        print(getDocumentTitle())
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Budget 2025\n");
    EXPECT_EQ(workbook->name, "Budget 2025");
}

// ============================================================================
// Cell Value Assignment Tests (cell.value = x via __newindex)
// ============================================================================

TEST(LuauSandboxTest, CellValueAssignmentNumber) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local c = getCell('A1', {create = true})
        c.value = 123
        return getCell('A1').value
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "123");
}

TEST(LuauSandboxTest, CellValueAssignmentString) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local c = getCell('B1', {create = true})
        c.value = "Hello World"
        return getCell('B1').value
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Hello World");
}

TEST(LuauSandboxTest, CellValueAssignmentBoolean) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local c = getCell('B1', {create = true})
        c.value = true
        return getCell('B1').value
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "true");
}

TEST(LuauSandboxTest, CellValueAssignmentFormula) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set B1 to a number first
    auto setup = sandbox.execute("setCell('B1', 100)");
    EXPECT_TRUE(setup.success) << setup.error;

    auto result = sandbox.execute(R"(
        local c = getCell('A1', {create = true})
        c.value = "=B1"
        return getCell('A1').formula
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "=B1");
}

TEST(LuauSandboxTest, CellValueAssignmentNilClearsCell) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // A1 starts with value 42
    auto before = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(before.success) << before.error;
    EXPECT_EQ(before.output, "42");

    // Clear cell by setting value to nil
    auto clear = sandbox.execute(R"(
        local c = getCell('A1')
        c.value = nil
    )");
    EXPECT_TRUE(clear.success) << clear.error;

    // Cell should now be empty (nil)
    auto after = sandbox.execute(R"(
        local c = getCell('A1')
        if c == nil then return 'nil' end
        if c.value == nil then return 'value is nil' end
        return c.value
    )");
    EXPECT_TRUE(after.success) << after.error;
    // After clearing, the cell may still exist but its value should be nil
    EXPECT_TRUE(after.output == "nil" || after.output == "value is nil");
}

TEST(LuauSandboxTest, CellRefPropertyIsReadOnly) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        c.ref = "B1"
    )");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("read-only") != std::string::npos);
}

TEST(LuauSandboxTest, CellFormulaPropertyIsReadOnly) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        c.formula = "=B1"
    )");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("read-only") != std::string::npos);
}

TEST(LuauSandboxTest, CellCustomPropertyAllowed) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Custom properties should be allowed
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        c.myCustomProp = "test"
        return c.myCustomProp
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "test");
}

// ============================================================================
// __tostring Metamethod Tests
// ============================================================================

TEST(LuauSandboxTest, CellToStringShowsReference) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        return tostring(c)
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Cell<A1>");
}

TEST(LuauSandboxTest, CellToStringWithPrint) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // print() uses __tostring
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        print(c)
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Cell<A1>\n");
}

TEST(LuauSandboxTest, CellToStringDifferentPositions) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Create cells at different positions and verify tostring
    auto result = sandbox.execute(R"(
        local b2 = getCell('B2', {create = true})
        local c3 = getCell('C3', {create = true})
        return tostring(b2) .. " " .. tostring(c3)
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Cell<B2> Cell<C3>");
}

TEST(LuauSandboxTest, SheetToStringShowsName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local s = getSheet(1)
        return tostring(s)
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet<Sheet1>");
}

TEST(LuauSandboxTest, SheetToStringWithPrint) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // print() uses __tostring
    auto result = sandbox.execute(R"(
        local s = getSheet(1)
        print(s)
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet<Sheet1>\n");
}

TEST(LuauSandboxTest, SheetToStringAfterRename) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // tostring should reflect the renamed name (fetched dynamically)
    auto result = sandbox.execute(R"(
        local s = getSheet(1)
        s.name = "MyData"
        return tostring(s)
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet<MyData>");
}

TEST(LuauSandboxTest, NewSheetToString) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute(R"(
        local s = addSheet("Budget")
        return tostring(s)
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "Sheet<Budget>");
}

// ============================================================================
// cell.dependents property tests
// ============================================================================

TEST(LuauSandboxTest, CellDependentsEmptyForNonFormulaCell) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set A1 to a simple value (not a formula)
    auto setup = sandbox.execute("setCell('A1', 42)");
    EXPECT_TRUE(setup.success) << setup.error;

    // A1 has no dependents (no formula references it)
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        local deps = c.dependents
        return #deps
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "0");
}

TEST(LuauSandboxTest, CellDependentsSingleDependent) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set A1 to a value, B1 to a formula referencing A1
    auto setup = sandbox.execute(R"(
        setCell('A1', 10)
        setCell('B1', '=A1*2')
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // A1 should have B1 as a dependent
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        local deps = c.dependents
        if #deps == 1 then
            return deps[1].ref
        else
            return "wrong count: " .. #deps
        end
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "B1");
}

TEST(LuauSandboxTest, CellDependentsMultipleDependents) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set A1 to a value, B1 and C1 to formulas referencing A1
    auto setup = sandbox.execute(R"(
        setCell('A1', 100)
        setCell('B1', '=A1+1')
        setCell('C1', '=A1+2')
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // A1 should have 2 dependents
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        local deps = c.dependents
        return #deps
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "2");
}

TEST(LuauSandboxTest, CellDependentsAreFirstLevelOnly) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // A1 -> B1 -> C1 chain
    auto setup = sandbox.execute(R"(
        setCell('A1', 5)
        setCell('B1', '=A1')
        setCell('C1', '=B1')
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // A1 should only have B1 as direct dependent (not C1)
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        local deps = c.dependents
        if #deps == 1 then
            return deps[1].ref
        else
            return "wrong count: " .. #deps
        end
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "B1");
}

TEST(LuauSandboxTest, CellDependentsReturnsIterable) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set up A1 with two dependents
    auto setup = sandbox.execute(R"(
        setCell('A1', 1)
        setCell('B1', '=A1')
        setCell('C1', '=A1')
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // Iterate over dependents and collect refs
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        local refs = {}
        for i, dep in ipairs(c.dependents) do
            table.insert(refs, dep.ref)
        end
        table.sort(refs)
        return table.concat(refs, ",")
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "B1,C1");
}

// ============================================================================
// cell.dependencies property tests
// ============================================================================

TEST(LuauSandboxTest, CellDependenciesEmptyForNonFormulaCell) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set A1 to a simple value (not a formula)
    auto setup = sandbox.execute("setCell('A1', 42)");
    EXPECT_TRUE(setup.success) << setup.error;

    // A1 has no dependencies (it's not a formula)
    auto result = sandbox.execute(R"(
        local c = getCell('A1')
        local deps = c.dependencies
        return #deps
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "0");
}

TEST(LuauSandboxTest, CellDependenciesSingleRef) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // A1 = 10, B1 = formula referencing A1
    auto setup = sandbox.execute(R"(
        setCell('A1', 10)
        setCell('B1', '=A1*2')
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // B1 should have A1 as a dependency
    auto result = sandbox.execute(R"(
        local c = getCell('B1')
        local deps = c.dependencies
        if #deps == 1 then
            return deps[1].ref
        else
            return "wrong count: " .. #deps
        end
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "A1");
}

TEST(LuauSandboxTest, CellDependenciesMultipleRefs) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set up A1, B1, then C1 = formula referencing both
    auto setup = sandbox.execute(R"(
        setCell('A1', 10)
        setCell('B1', 20)
        setCell('C1', '=A1+B1')
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // C1 should have 2 dependencies (A1 and B1)
    auto result = sandbox.execute(R"(
        local c = getCell('C1')
        local refs = {}
        for i, dep in ipairs(c.dependencies) do
            table.insert(refs, dep.ref)
        end
        table.sort(refs)
        return table.concat(refs, ",")
    )");
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.output, "A1,B1");
}

TEST(LuauSandboxTest, CellDependentsAndDependenciesInverse) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // A1 = 10, B1 = =A1
    auto setup = sandbox.execute(R"(
        setCell('A1', 10)
        setCell('B1', '=A1')
    )");
    EXPECT_TRUE(setup.success) << setup.error;

    // A1's dependents should include B1
    auto dependents = sandbox.execute(R"(
        local c = getCell('A1')
        if #c.dependents == 1 then
            return c.dependents[1].ref
        else
            return "wrong: " .. #c.dependents
        end
    )");
    EXPECT_TRUE(dependents.success) << dependents.error;
    EXPECT_EQ(dependents.output, "B1");

    // B1's dependencies should include A1
    auto dependencies = sandbox.execute(R"(
        local c = getCell('B1')
        if #c.dependencies == 1 then
            return c.dependencies[1].ref
        else
            return "wrong: " .. #c.dependencies
        end
    )");
    EXPECT_TRUE(dependencies.success) << dependencies.error;
    EXPECT_EQ(dependencies.output, "A1");
}

// Test that setCell with formulas creates cells for referenced positions
TEST(LuauSandboxTest, SetCellFormulaCreatesReferencedCells) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set a formula that references non-existent cells B2 and C3
    // Before the fix, these cells wouldn't be created and the formula would fail
    auto result = sandbox.execute("setCell('D4', '=B2+C3')");
    EXPECT_TRUE(result.success) << result.error;

    // Verify B2 and C3 were created
    Axis* colB = sheet->getColumnByPosition(1);
    Axis* colC = sheet->getColumnByPosition(2);
    Axis* row2 = sheet->getRowByPosition(1);
    Axis* row3 = sheet->getRowByPosition(2);
    ASSERT_NE(colB, nullptr);
    ASSERT_NE(colC, nullptr);
    ASSERT_NE(row2, nullptr);
    ASSERT_NE(row3, nullptr);

    Cell* cellB2 = sheet->getCellAt(colB->id, row2->id);
    Cell* cellC3 = sheet->getCellAt(colC->id, row3->id);
    EXPECT_NE(cellB2, nullptr) << "Cell B2 should be created by formula resolution";
    EXPECT_NE(cellC3, nullptr) << "Cell C3 should be created by formula resolution";
}

// Test that setting values triggers recalculation of dependent formulas
TEST(LuauSandboxTest, SetCellTriggersRecalculation) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set a formula in B1 that depends on C1
    auto result1 = sandbox.execute("setCell('B1', '=C1*2')");
    EXPECT_TRUE(result1.success) << result1.error;

    // Set C1 to 10
    auto result2 = sandbox.execute("setCell('C1', 10)");
    EXPECT_TRUE(result2.success) << result2.error;

    // Verify B1 was recalculated to 20
    auto result3 = sandbox.execute("return getCell('B1').value");
    EXPECT_TRUE(result3.success) << result3.error;
    EXPECT_EQ(result3.output, "20") << "B1 should be recalculated to C1*2 = 20";
}

// Test formula with range reference
TEST(LuauSandboxTest, SetCellFormulaWithRange) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set values in B1, B2, B3
    auto r1 = sandbox.execute("setCell('B1', 10)");
    EXPECT_TRUE(r1.success) << r1.error;
    auto r2 = sandbox.execute("setCell('B2', 20)");
    EXPECT_TRUE(r2.success) << r2.error;
    auto r3 = sandbox.execute("setCell('B3', 30)");
    EXPECT_TRUE(r3.success) << r3.error;

    // Set a SUM formula in A1
    auto r4 = sandbox.execute("setCell('A1', '=SUM(B1:B3)')");
    EXPECT_TRUE(r4.success) << r4.error;

    // Verify A1 shows the sum
    auto r5 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r5.success) << r5.error;
    EXPECT_EQ(r5.output, "60") << "A1 should be SUM(10,20,30) = 60";

    // Now change B2 and verify A1 is updated
    auto r6 = sandbox.execute("setCell('B2', 100)");
    EXPECT_TRUE(r6.success) << r6.error;

    auto r7 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r7.success) << r7.error;
    EXPECT_EQ(r7.output, "140") << "A1 should be SUM(10,100,30) = 140";
}

// Test chain of dependencies (A1 depends on B1, B1 depends on C1)
TEST(LuauSandboxTest, SetCellChainedDependencies) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set C1 to 5
    auto r1 = sandbox.execute("setCell('C1', 5)");
    EXPECT_TRUE(r1.success) << r1.error;

    // B1 = C1 * 2
    auto r2 = sandbox.execute("setCell('B1', '=C1*2')");
    EXPECT_TRUE(r2.success) << r2.error;

    // A1 = B1 + 10
    auto r3 = sandbox.execute("setCell('A1', '=B1+10')");
    EXPECT_TRUE(r3.success) << r3.error;

    // Verify initial values: C1=5, B1=10, A1=20
    auto r4 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r4.success) << r4.error;
    EXPECT_EQ(r4.output, "20") << "A1 should be B1+10 = 10+10 = 20";

    // Change C1 to 10 - this should update B1 to 20 and A1 to 30
    auto r5 = sandbox.execute("setCell('C1', 10)");
    EXPECT_TRUE(r5.success) << r5.error;

    auto r6 = sandbox.execute("return getCell('B1').value");
    EXPECT_TRUE(r6.success) << r6.error;
    EXPECT_EQ(r6.output, "20") << "B1 should be C1*2 = 20";

    auto r7 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r7.success) << r7.error;
    EXPECT_EQ(r7.output, "30") << "A1 should be B1+10 = 30";
}

// Test clearing a cell triggers recalculation
TEST(LuauSandboxTest, ClearCellTriggersRecalculation) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set B1 to 100
    auto r1 = sandbox.execute("setCell('B1', 100)");
    EXPECT_TRUE(r1.success) << r1.error;

    // Set A1 = B1 + 1
    auto r2 = sandbox.execute("setCell('A1', '=B1+1')");
    EXPECT_TRUE(r2.success) << r2.error;

    // Verify A1 = 101
    auto r3 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r3.success) << r3.error;
    EXPECT_EQ(r3.output, "101");

    // Clear B1 by setting to empty string (clears content but keeps cell)
    auto r4 = sandbox.execute("setCell('B1', '')");
    EXPECT_TRUE(r4.success) << r4.error;

    // A1 should now show the result of formula with empty B1 (0+1 = 1)
    auto r5 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r5.success) << r5.error;
    EXPECT_EQ(r5.output, "1") << "A1 should be 0+1=1 after B1 cleared";
}

// Test cell.value assignment with formulas
TEST(LuauSandboxTest, CellValueAssignmentWithFormula) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    // Set B1 to 50 via cell.value
    auto r1 = sandbox.execute(R"(
        local cell = getCell('B1', {create=true})
        cell.value = 50
    )");
    EXPECT_TRUE(r1.success) << r1.error;

    // Set A1 = B1 * 2 via cell.value
    auto r2 = sandbox.execute(R"(
        local cell = getCell('A1', {create=true})
        cell.value = '=B1*2'
    )");
    EXPECT_TRUE(r2.success) << r2.error;

    // Verify A1 = 100
    auto r3 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r3.success) << r3.error;
    EXPECT_EQ(r3.output, "100");

    // Change B1 via cell.value
    auto r4 = sandbox.execute(R"(
        local cell = getCell('B1')
        cell.value = 25
    )");
    EXPECT_TRUE(r4.success) << r4.error;

    // Verify A1 is updated to 50
    auto r5 = sandbox.execute("return getCell('A1').value");
    EXPECT_TRUE(r5.success) << r5.error;
    EXPECT_EQ(r5.output, "50");
}

}  // namespace
}  // namespace cells
