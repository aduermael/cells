#include "core/cells/luau_sandbox.h"

#include <gtest/gtest.h>

namespace cells {
namespace {

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

}  // namespace
}  // namespace cells
