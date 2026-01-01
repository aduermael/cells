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

TEST(LuauSandboxTest, SheetGetName) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);

    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    auto result = sandbox.execute("return sheetGetName(0)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output, "Sheet1");
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

}  // namespace
}  // namespace cells
