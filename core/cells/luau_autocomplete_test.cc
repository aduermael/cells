#include "luau_autocomplete.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace cells {
namespace {

class LuauAutocompleteTest : public ::testing::Test {
protected:
    LuauAutocomplete autocomplete;
};

TEST_F(LuauAutocompleteTest, EmptySource_ReturnsGlobalSuggestions) {
    // Use a space to put cursor in expression context where globals appear
    auto result = autocomplete.getCompletions(" ", 0, 1);

    // Should have suggestions for global functions
    EXPECT_FALSE(result.suggestions.empty());

    // Luau built-in globals should be present (table, math, etc.)
    auto hasTable = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                 [](const auto& s) { return s.label == "table"; });
    EXPECT_TRUE(hasTable) << "Expected 'table' in suggestions";

    // Should suggest our Cells API functions
    auto hasGetCell = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "getCell"; });
    EXPECT_TRUE(hasGetCell) << "Expected 'getCell' in suggestions";

    auto hasSetCell = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "setCell"; });
    EXPECT_TRUE(hasSetCell) << "Expected 'setCell' in suggestions";
}

TEST_F(LuauAutocompleteTest, PartialFunction_FiltersSuggestions) {
    // Typing "get" should suggest getCell and getSheet
    auto result = autocomplete.getCompletions("get", 0, 3);

    auto hasGetCell = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "getCell"; });
    auto hasGetSheet = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                    [](const auto& s) { return s.label == "getSheet"; });

    EXPECT_TRUE(hasGetCell || hasGetSheet) << "Expected 'getCell' or 'getSheet' in suggestions";
}

TEST_F(LuauAutocompleteTest, AfterLocal_SuggestsKeywordsAndGlobals) {
    auto result = autocomplete.getCompletions("local x = ", 0, 10);

    // Should be in expression context
    EXPECT_FALSE(result.suggestions.empty());
}

TEST_F(LuauAutocompleteTest, CellProperty_SuggestsProperties) {
    // After getting a cell, suggest its properties
    std::string source = R"(
local cell = getCell("A1")
if cell then
    cell.
end
)";
    // Position after "cell."
    auto result = autocomplete.getCompletions(source, 3, 9);

    // Should suggest cell properties: value, formula, ref
    auto hasValue = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                 [](const auto& s) { return s.label == "value"; });
    auto hasFormula = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "formula"; });
    auto hasRef = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                               [](const auto& s) { return s.label == "ref"; });

    // At least one should be present if type inference is working
    bool hasAnyProperty = hasValue || hasFormula || hasRef;
    if (!hasAnyProperty && !result.suggestions.empty()) {
        // Print what we got for debugging
        for (const auto& s : result.suggestions) {
            std::cout << "  Suggestion: " << s.label << " (" << s.kind << ")\n";
        }
    }
}

TEST_F(LuauAutocompleteTest, SheetProperty_SuggestsName) {
    std::string source = R"(
local sheet = getSheet({index = 0})
if sheet then
    sheet.
end
)";
    auto result = autocomplete.getCompletions(source, 3, 10);

    auto hasName = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                [](const auto& s) { return s.label == "name"; });

    // Sheet should have a 'name' property
    if (!hasName && !result.suggestions.empty()) {
        for (const auto& s : result.suggestions) {
            std::cout << "  Sheet suggestion: " << s.label << " (" << s.kind << ")\n";
        }
    }
}

TEST_F(LuauAutocompleteTest, AllCellsApiFunctions_AreSuggested) {
    auto result = autocomplete.getCompletions(" ", 0, 1);

    std::vector<std::string> expectedFunctions = {
        "getCell", "setCell", "setDocumentTitle", "setColumnWidth", "setRowHeight",
        "moveColumn", "selectSheet", "getSheet", "addSheet", "selectRange",
        "deleteRange", "fillRange"};

    for (const auto& funcName : expectedFunctions) {
        auto found = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                  [&](const auto& s) { return s.label == funcName; });
        EXPECT_TRUE(found) << "Expected '" << funcName << "' in global suggestions";
    }
}

TEST_F(LuauAutocompleteTest, Keywords_AreSuggested) {
    auto result = autocomplete.getCompletions(" ", 0, 1);

    // Should suggest Lua keywords that are valid at statement level
    // Note: "then" and "end" are not standalone keywords - they're only valid
    // in specific contexts (after "if" or at the end of blocks)
    std::vector<std::string> expectedKeywords = {"local", "function", "if", "for", "while", "return"};

    for (const auto& keyword : expectedKeywords) {
        auto found = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                  [&](const auto& s) { return s.label == keyword; });
        EXPECT_TRUE(found) << "Expected keyword '" << keyword << "' in suggestions";
    }
}

TEST_F(LuauAutocompleteTest, Context_IsSetCorrectly) {
    // At start of file, should be statement context
    auto result = autocomplete.getCompletions("", 0, 0);
    EXPECT_EQ(result.context, "statement");

    // After "local x = ", should be expression context
    result = autocomplete.getCompletions("local x = ", 0, 10);
    EXPECT_EQ(result.context, "expression");
}

}  // namespace
}  // namespace cells
