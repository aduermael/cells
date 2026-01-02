#include "luau_autocomplete.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace cells {
namespace {

class LuauAutocompleteTest : public ::testing::Test {
protected:
    LuauAutocomplete autocomplete;
};

TEST_F(LuauAutocompleteTest, TwoCharPrefix_ReturnsGlobalSuggestions) {
    // Smart trigger: need 2+ chars to show suggestions
    auto result = autocomplete.getCompletions("ta", 0, 2);

    // Should have suggestions for global functions matching "ta"
    EXPECT_FALSE(result.suggestions.empty());

    // Luau built-in globals should be present (table matches "ta")
    auto hasTable = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                [](const auto& s) { return s.label == "table"; });
    EXPECT_TRUE(hasTable) << "Expected 'table' in suggestions";
}

TEST_F(LuauAutocompleteTest, GetPrefix_ReturnsCellsApi) {
    // Smart trigger: "ge" is 2 chars, should suggest getCell, getSheet
    auto result = autocomplete.getCompletions("ge", 0, 2);

    // Should suggest our Cells API functions
    auto hasGetCell = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                  [](const auto& s) { return s.label == "getCell"; });
    EXPECT_TRUE(hasGetCell) << "Expected 'getCell' in suggestions";

    auto hasGetSheet = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "getSheet"; });
    EXPECT_TRUE(hasGetSheet) << "Expected 'getSheet' in suggestions";
}

TEST_F(LuauAutocompleteTest, SetPrefix_ReturnsCellsApi) {
    // "se" prefix should suggest setCell, setDocumentTitle, etc.
    auto result = autocomplete.getCompletions("se", 0, 2);

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

TEST_F(LuauAutocompleteTest, AfterLocal_WithPrefix_SuggestsGlobals) {
    // After "local x = ge", should suggest getCell, getSheet
    auto result = autocomplete.getCompletions("local x = ge", 0, 12);

    // Should be in expression context with suggestions
    EXPECT_FALSE(result.suggestions.empty());

    auto hasGetCell = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                  [](const auto& s) { return s.label == "getCell"; });
    EXPECT_TRUE(hasGetCell) << "Expected 'getCell' in suggestions";
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

    // Type inference should provide all Cell properties
    EXPECT_TRUE(hasValue) << "Expected 'value' property on Cell";
    EXPECT_TRUE(hasFormula) << "Expected 'formula' property on Cell";
    EXPECT_TRUE(hasRef) << "Expected 'ref' property on Cell";
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

    // Type inference should provide Sheet.name property
    EXPECT_TRUE(hasName) << "Expected 'name' property on Sheet";
}

TEST_F(LuauAutocompleteTest, CellsApiFunctions_SuggestedByPrefix) {
    // Test that various Cells API functions are suggested with appropriate prefixes
    // "ge" -> getCell, getSheet
    auto result = autocomplete.getCompletions("ge", 0, 2);
    auto hasGetCell = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                  [](const auto& s) { return s.label == "getCell"; });
    auto hasGetSheet = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "getSheet"; });
    EXPECT_TRUE(hasGetCell) << "Expected 'getCell' with 'ge' prefix";
    EXPECT_TRUE(hasGetSheet) << "Expected 'getSheet' with 'ge' prefix";

    // "se" -> setCell, setDocumentTitle, setColumnWidth, setRowHeight, selectSheet, selectRange
    result = autocomplete.getCompletions("se", 0, 2);
    auto hasSetCell = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                  [](const auto& s) { return s.label == "setCell"; });
    auto hasSelectSheet = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                      [](const auto& s) { return s.label == "selectSheet"; });
    EXPECT_TRUE(hasSetCell) << "Expected 'setCell' with 'se' prefix";
    EXPECT_TRUE(hasSelectSheet) << "Expected 'selectSheet' with 'se' prefix";

    // "ad" -> addSheet
    result = autocomplete.getCompletions("ad", 0, 2);
    auto hasAddSheet = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "addSheet"; });
    EXPECT_TRUE(hasAddSheet) << "Expected 'addSheet' with 'ad' prefix";

    // "de" -> deleteRange
    result = autocomplete.getCompletions("de", 0, 2);
    auto hasDeleteRange = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                      [](const auto& s) { return s.label == "deleteRange"; });
    EXPECT_TRUE(hasDeleteRange) << "Expected 'deleteRange' with 'de' prefix";

    // "fi" -> fillRange
    result = autocomplete.getCompletions("fi", 0, 2);
    auto hasFillRange = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                    [](const auto& s) { return s.label == "fillRange"; });
    EXPECT_TRUE(hasFillRange) << "Expected 'fillRange' with 'fi' prefix";

    // "mo" -> moveColumn
    result = autocomplete.getCompletions("mo", 0, 2);
    auto hasMoveColumn = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                     [](const auto& s) { return s.label == "moveColumn"; });
    EXPECT_TRUE(hasMoveColumn) << "Expected 'moveColumn' with 'mo' prefix";
}

TEST_F(LuauAutocompleteTest, Keywords_SuggestedByPrefix) {
    // Smart trigger: need 2+ chars to show suggestions
    // Test keywords with appropriate prefixes

    // "lo" -> local
    auto result = autocomplete.getCompletions("lo", 0, 2);
    auto hasLocal = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                [](const auto& s) { return s.label == "local"; });
    EXPECT_TRUE(hasLocal) << "Expected 'local' with 'lo' prefix";

    // "fu" -> function
    result = autocomplete.getCompletions("fu", 0, 2);
    auto hasFunction = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                   [](const auto& s) { return s.label == "function"; });
    EXPECT_TRUE(hasFunction) << "Expected 'function' with 'fu' prefix";

    // "re" -> return, repeat
    result = autocomplete.getCompletions("re", 0, 2);
    auto hasReturn = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                 [](const auto& s) { return s.label == "return"; });
    EXPECT_TRUE(hasReturn) << "Expected 'return' with 're' prefix";

    // "wh" -> while
    result = autocomplete.getCompletions("wh", 0, 2);
    auto hasWhile = std::any_of(result.suggestions.begin(), result.suggestions.end(),
                                [](const auto& s) { return s.label == "while"; });
    EXPECT_TRUE(hasWhile) << "Expected 'while' with 'wh' prefix";
}

TEST_F(LuauAutocompleteTest, Context_IsSetCorrectly) {
    // With 2+ char prefix at start of file, should be statement context
    auto result = autocomplete.getCompletions("lo", 0, 2);
    EXPECT_EQ(result.context, "statement");

    // After "local x = ge", should be expression context (ge is 2 chars)
    result = autocomplete.getCompletions("local x = ge", 0, 12);
    EXPECT_EQ(result.context, "expression");
}

TEST_F(LuauAutocompleteTest, SmartTrigger_FilteredWhenTooShort) {
    // Single char should return "filtered" context (not enough chars)
    auto result = autocomplete.getCompletions("g", 0, 1);
    EXPECT_EQ(result.context, "filtered");
    EXPECT_TRUE(result.suggestions.empty());

    // Empty should also be filtered
    result = autocomplete.getCompletions("", 0, 0);
    EXPECT_EQ(result.context, "filtered");
    EXPECT_TRUE(result.suggestions.empty());
}

TEST_F(LuauAutocompleteTest, SmartTrigger_AfterDotTriggersImmediately) {
    // After "." should trigger immediately (no 2-char requirement)
    std::string source = R"(
local cell = getCell("A1")
if cell then
    cell.
end
)";
    // Position after "cell." - property context
    auto result = autocomplete.getCompletions(source, 3, 9);
    EXPECT_NE(result.context, "filtered");
}

TEST_F(LuauAutocompleteTest, SmartTrigger_InsideString_NoSuggestions) {
    // Inside a string, should not show suggestions
    auto result = autocomplete.getCompletions("local x = \"ge", 0, 13);
    EXPECT_EQ(result.context, "string");
    EXPECT_TRUE(result.suggestions.empty());
}

TEST_F(LuauAutocompleteTest, SmartTrigger_AfterKeyword_NoSuggestions) {
    // After keywords like "end", "then", etc., should not suggest
    auto result = autocomplete.getCompletions("end xy", 0, 6);
    EXPECT_EQ(result.context, "filtered");
    EXPECT_TRUE(result.suggestions.empty());

    result = autocomplete.getCompletions("then xy", 0, 7);
    EXPECT_EQ(result.context, "filtered");
    EXPECT_TRUE(result.suggestions.empty());
}

TEST_F(LuauAutocompleteTest, SmartTrigger_InsideComment_NoSuggestions) {
    // Inside a comment, should not show suggestions
    auto result = autocomplete.getCompletions("-- ge", 0, 5);
    EXPECT_EQ(result.context, "comment");
    EXPECT_TRUE(result.suggestions.empty());

    // Comment after code
    result = autocomplete.getCompletions("local x = 1 -- ge", 0, 17);
    EXPECT_EQ(result.context, "comment");
    EXPECT_TRUE(result.suggestions.empty());
}

TEST_F(LuauAutocompleteTest, SmartTrigger_AfterColon_TriggersImmediately) {
    // After ":" (method access) should trigger immediately
    std::string source = "local t = {}\nt:";
    auto result = autocomplete.getCompletions(source, 1, 2);
    // Should not be filtered (colon triggers immediately)
    EXPECT_NE(result.context, "filtered");
}

}  // namespace
}  // namespace cells
