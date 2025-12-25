#include "core/cells/formula_lexer.h"

#include <gtest/gtest.h>

#include <cmath>

namespace cells {
namespace {

// ============================================================================
// Number Token Tests
// ============================================================================

TEST(FormulaLexerTest, NumberInteger) {
    FormulaLexer lexer("42");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "42");
    EXPECT_EQ(tok.numberValue(), 42.0);
}

TEST(FormulaLexerTest, NumberDecimal) {
    FormulaLexer lexer("3.14");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "3.14");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 3.14);
}

TEST(FormulaLexerTest, NumberLeadingDecimal) {
    FormulaLexer lexer(".5");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, ".5");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 0.5);
}

TEST(FormulaLexerTest, NumberScientificNotation) {
    FormulaLexer lexer("1.5e10");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "1.5e10");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 1.5e10);
}

TEST(FormulaLexerTest, NumberScientificNotationUpperE) {
    FormulaLexer lexer("1E5");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "1E5");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 1e5);
}

TEST(FormulaLexerTest, NumberScientificNotationNegativeExp) {
    FormulaLexer lexer("1e-5");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "1e-5");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 1e-5);
}

TEST(FormulaLexerTest, NumberScientificNotationPositiveExp) {
    FormulaLexer lexer("2.5e+3");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "2.5e+3");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 2500.0);
}

TEST(FormulaLexerTest, NumberZero) {
    FormulaLexer lexer("0");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "0");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 0.0);
}

TEST(FormulaLexerTest, NumberLarge) {
    FormulaLexer lexer("1234567890");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "1234567890");
    EXPECT_DOUBLE_EQ(tok.numberValue(), 1234567890.0);
}

// ============================================================================
// String Token Tests
// ============================================================================

TEST(FormulaLexerTest, StringSimple) {
    FormulaLexer lexer("\"Hello\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING);
    EXPECT_EQ(tok.text, "\"Hello\"");
    EXPECT_EQ(tok.stringValue(), "Hello");
}

TEST(FormulaLexerTest, StringEmpty) {
    FormulaLexer lexer("\"\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING);
    EXPECT_EQ(tok.text, "\"\"");
    EXPECT_EQ(tok.stringValue(), "");
}

TEST(FormulaLexerTest, StringWithSpaces) {
    FormulaLexer lexer("\"Hello World\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING);
    EXPECT_EQ(tok.stringValue(), "Hello World");
}

TEST(FormulaLexerTest, StringWithEscapedQuote) {
    FormulaLexer lexer("\"He said \"\"Hi\"\"\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING);
    EXPECT_EQ(tok.stringValue(), "He said \"Hi\"");
}

TEST(FormulaLexerTest, StringUnterminated) {
    FormulaLexer lexer("\"Hello");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ERROR);
    EXPECT_FALSE(tok.errorMessage.empty());
}

// ============================================================================
// Boolean Token Tests
// ============================================================================

TEST(FormulaLexerTest, BooleanTrue) {
    FormulaLexer lexer("TRUE");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::BOOLEAN);
    EXPECT_EQ(tok.text, "TRUE");
    EXPECT_TRUE(tok.booleanValue());
}

TEST(FormulaLexerTest, BooleanFalse) {
    FormulaLexer lexer("FALSE");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::BOOLEAN);
    EXPECT_EQ(tok.text, "FALSE");
    EXPECT_FALSE(tok.booleanValue());
}

TEST(FormulaLexerTest, BooleanTrueLowercase) {
    FormulaLexer lexer("true");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::BOOLEAN);
    EXPECT_EQ(tok.text, "true");
    EXPECT_TRUE(tok.booleanValue());
}

TEST(FormulaLexerTest, BooleanFalseLowercase) {
    FormulaLexer lexer("false");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::BOOLEAN);
    EXPECT_EQ(tok.text, "false");
    EXPECT_FALSE(tok.booleanValue());
}

TEST(FormulaLexerTest, BooleanMixedCase) {
    FormulaLexer lexer("True");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::BOOLEAN);
    EXPECT_TRUE(tok.booleanValue());
}

// ============================================================================
// Cell Reference Token Tests
// ============================================================================

TEST(FormulaLexerTest, ColumnSingleLetter) {
    FormulaLexer lexer("A1");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COLUMN);
    EXPECT_EQ(tok.text, "A");
}

TEST(FormulaLexerTest, ColumnDoubleLetter) {
    // AA at end of input is IDENTIFIER (parser handles context)
    FormulaLexer lexer("AA");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.text, "AA");
}

TEST(FormulaLexerTest, ColumnDoubleLetterWithDigit) {
    // AA followed by digit is COLUMN
    FormulaLexer lexer("AA100");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COLUMN);
    EXPECT_EQ(tok.text, "AA");
}

TEST(FormulaLexerTest, ColumnDoubleLetterWithColon) {
    // AA: is recognized as column (for whole column refs like AA:BB)
    FormulaLexer lexer("AA:");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COLUMN);
    EXPECT_EQ(tok.text, "AA");
}

TEST(FormulaLexerTest, ColumnDoubleLetterAbsolute) {
    // AA$1 - AA followed by $ then digit is COLUMN
    FormulaLexer lexer("AA$1");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COLUMN);
    EXPECT_EQ(tok.text, "AA");
}

TEST(FormulaLexerTest, ColumnTripleLetter) {
    FormulaLexer lexer("XFD1");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COLUMN);
    EXPECT_EQ(tok.text, "XFD");
}

TEST(FormulaLexerTest, ColumnLowercase) {
    FormulaLexer lexer("b2");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COLUMN);
    EXPECT_EQ(tok.text, "b");
}

TEST(FormulaLexerTest, CellReferenceFullA1) {
    FormulaLexer lexer("A1");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 3u);  // COLUMN, ROW, END
    EXPECT_EQ(tokens[0].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[0].text, "A");
    EXPECT_EQ(tokens[1].type, TokenType::NUMBER);  // Row numbers are lexed as NUMBER
    EXPECT_EQ(tokens[1].text, "1");
}

TEST(FormulaLexerTest, CellReferenceC10) {
    FormulaLexer lexer("C10");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[0].text, "C");
    EXPECT_EQ(tokens[1].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[1].text, "10");
}

TEST(FormulaLexerTest, AbsoluteReference) {
    FormulaLexer lexer("$A$1");
    auto tokens = lexer.tokenizeAll();
    // $A$1 -> $, COLUMN (A followed by $digit), $, NUMBER, END
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].type, TokenType::DOLLAR);
    EXPECT_EQ(tokens[1].type, TokenType::COLUMN);  // A followed by $1
    EXPECT_EQ(tokens[2].type, TokenType::DOLLAR);
    EXPECT_EQ(tokens[3].type, TokenType::NUMBER);
}

TEST(FormulaLexerTest, MixedReference) {
    FormulaLexer lexer("$A1");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 4u);  // $, COLUMN, NUMBER, END
    EXPECT_EQ(tokens[0].type, TokenType::DOLLAR);
    EXPECT_EQ(tokens[1].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[2].type, TokenType::NUMBER);
}

// ============================================================================
// Operator Token Tests
// ============================================================================

TEST(FormulaLexerTest, OperatorPlus) {
    FormulaLexer lexer("+");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::PLUS);
    EXPECT_EQ(tok.text, "+");
}

TEST(FormulaLexerTest, OperatorMinus) {
    FormulaLexer lexer("-");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::MINUS);
    EXPECT_EQ(tok.text, "-");
}

TEST(FormulaLexerTest, OperatorStar) {
    FormulaLexer lexer("*");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STAR);
    EXPECT_EQ(tok.text, "*");
}

TEST(FormulaLexerTest, OperatorSlash) {
    FormulaLexer lexer("/");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::SLASH);
    EXPECT_EQ(tok.text, "/");
}

TEST(FormulaLexerTest, OperatorCaret) {
    FormulaLexer lexer("^");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::CARET);
    EXPECT_EQ(tok.text, "^");
}

TEST(FormulaLexerTest, OperatorAmpersand) {
    FormulaLexer lexer("&");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::AMPERSAND);
    EXPECT_EQ(tok.text, "&");
}

TEST(FormulaLexerTest, OperatorEqual) {
    FormulaLexer lexer("=");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::EQUAL);
    EXPECT_EQ(tok.text, "=");
}

TEST(FormulaLexerTest, OperatorNotEqual) {
    FormulaLexer lexer("<>");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NOT_EQUAL);
    EXPECT_EQ(tok.text, "<>");
}

TEST(FormulaLexerTest, OperatorLess) {
    FormulaLexer lexer("<");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::LESS);
    EXPECT_EQ(tok.text, "<");
}

TEST(FormulaLexerTest, OperatorLessEqual) {
    FormulaLexer lexer("<=");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::LESS_EQUAL);
    EXPECT_EQ(tok.text, "<=");
}

TEST(FormulaLexerTest, OperatorGreater) {
    FormulaLexer lexer(">");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::GREATER);
    EXPECT_EQ(tok.text, ">");
}

TEST(FormulaLexerTest, OperatorGreaterEqual) {
    FormulaLexer lexer(">=");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::GREATER_EQUAL);
    EXPECT_EQ(tok.text, ">=");
}

// ============================================================================
// Punctuation Token Tests
// ============================================================================

TEST(FormulaLexerTest, PunctuationLParen) {
    FormulaLexer lexer("(");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::LPAREN);
}

TEST(FormulaLexerTest, PunctuationRParen) {
    FormulaLexer lexer(")");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::RPAREN);
}

TEST(FormulaLexerTest, PunctuationComma) {
    FormulaLexer lexer(",");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COMMA);
}

TEST(FormulaLexerTest, PunctuationColon) {
    FormulaLexer lexer(":");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::COLON);
}

TEST(FormulaLexerTest, PunctuationBang) {
    FormulaLexer lexer("!");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::BANG);
}

TEST(FormulaLexerTest, PunctuationDollar) {
    FormulaLexer lexer("$");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::DOLLAR);
}

// ============================================================================
// Identifier Token Tests
// ============================================================================

TEST(FormulaLexerTest, IdentifierSUM) {
    FormulaLexer lexer("SUM");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.text, "SUM");
}

TEST(FormulaLexerTest, IdentifierWithNumber) {
    FormulaLexer lexer("Sheet1");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.text, "Sheet1");
}

TEST(FormulaLexerTest, IdentifierWithUnderscore) {
    FormulaLexer lexer("my_range");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.text, "my_range");
}

TEST(FormulaLexerTest, IdentifierLowercase) {
    FormulaLexer lexer("sum");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.text, "sum");
}

// ============================================================================
// Position Tracking Tests
// ============================================================================

TEST(FormulaLexerTest, PositionTracking) {
    FormulaLexer lexer("A1+B2");
    auto tokens = lexer.tokenizeAll();

    // A (column) at position 0
    EXPECT_EQ(tokens[0].position.start, 0u);
    EXPECT_EQ(tokens[0].position.end, 1u);

    // 1 (row) at position 1
    EXPECT_EQ(tokens[1].position.start, 1u);
    EXPECT_EQ(tokens[1].position.end, 2u);

    // + at position 2
    EXPECT_EQ(tokens[2].position.start, 2u);
    EXPECT_EQ(tokens[2].position.end, 3u);

    // B (column) at position 3
    EXPECT_EQ(tokens[3].position.start, 3u);
    EXPECT_EQ(tokens[3].position.end, 4u);

    // 2 (row) at position 4
    EXPECT_EQ(tokens[4].position.start, 4u);
    EXPECT_EQ(tokens[4].position.end, 5u);
}

TEST(FormulaLexerTest, PositionWithWhitespace) {
    FormulaLexer lexer("A1 + B2");
    auto tokens = lexer.tokenizeAll();

    // A at position 0
    EXPECT_EQ(tokens[0].position.start, 0u);

    // + at position 3 (after space)
    EXPECT_EQ(tokens[2].position.start, 3u);

    // B at position 5 (after space)
    EXPECT_EQ(tokens[3].position.start, 5u);
}

// ============================================================================
// Whitespace Handling Tests
// ============================================================================

TEST(FormulaLexerTest, WhitespaceSkipped) {
    FormulaLexer lexer("  42  ");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NUMBER);
    EXPECT_EQ(tok.text, "42");
}

TEST(FormulaLexerTest, WhitespaceBetweenTokens) {
    FormulaLexer lexer("1 + 2");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 4u);  // NUMBER, PLUS, NUMBER, END
    EXPECT_EQ(tokens[0].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[1].type, TokenType::PLUS);
    EXPECT_EQ(tokens[2].type, TokenType::NUMBER);
}

TEST(FormulaLexerTest, TabsAndNewlines) {
    FormulaLexer lexer("1\t+\n2");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[1].type, TokenType::PLUS);
    EXPECT_EQ(tokens[2].type, TokenType::NUMBER);
}

// ============================================================================
// Error Token Tests
// ============================================================================

TEST(FormulaLexerTest, ErrorUnknownCharacter) {
    FormulaLexer lexer("@");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ERROR);
    EXPECT_FALSE(tok.errorMessage.empty());
}

TEST(FormulaLexerTest, ErrorUnterminatedString) {
    FormulaLexer lexer("\"hello");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ERROR);
    EXPECT_TRUE(tok.errorMessage.find("Unterminated") != std::string::npos);
}

// ============================================================================
// Complete Formula Tests
// ============================================================================

TEST(FormulaLexerTest, SimpleFormula) {
    FormulaLexer lexer("=A1+B2");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 7u);  // =, A, 1, +, B, 2, END
    EXPECT_EQ(tokens[0].type, TokenType::EQUAL);
    EXPECT_EQ(tokens[1].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[2].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[3].type, TokenType::PLUS);
    EXPECT_EQ(tokens[4].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[5].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[6].type, TokenType::END_OF_INPUT);
}

TEST(FormulaLexerTest, FunctionCall) {
    FormulaLexer lexer("=SUM(A1:B2)");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 10u);  // =, SUM, (, A, 1, :, B, 2, ), END
    EXPECT_EQ(tokens[0].type, TokenType::EQUAL);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].text, "SUM");
    EXPECT_EQ(tokens[2].type, TokenType::LPAREN);
    EXPECT_EQ(tokens[3].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[4].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[5].type, TokenType::COLON);
    EXPECT_EQ(tokens[6].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[7].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[8].type, TokenType::RPAREN);
    EXPECT_EQ(tokens[9].type, TokenType::END_OF_INPUT);
}

TEST(FormulaLexerTest, ComplexFormula) {
    FormulaLexer lexer("=IF(A1>0,B1*2,C1/3)");
    auto tokens = lexer.tokenizeAll();
    // =, IF, (, A, 1, >, 0, ,, B, 1, *, 2, ,, C, 1, /, 3, ), END
    EXPECT_EQ(tokens[0].type, TokenType::EQUAL);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].text, "IF");
    EXPECT_EQ(tokens[2].type, TokenType::LPAREN);
}

TEST(FormulaLexerTest, CrossSheetReference) {
    FormulaLexer lexer("=Sheet2!A1");
    auto tokens = lexer.tokenizeAll();
    // =, Sheet2, !, A, 1, END
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::EQUAL);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].text, "Sheet2");
    EXPECT_EQ(tokens[2].type, TokenType::BANG);
    EXPECT_EQ(tokens[3].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[4].type, TokenType::NUMBER);
}

TEST(FormulaLexerTest, StringConcatenation) {
    FormulaLexer lexer("=\"Hello\"&\" \"&\"World\"");
    auto tokens = lexer.tokenizeAll();
    // =, "Hello", &, " ", &, "World", END
    ASSERT_EQ(tokens.size(), 7u);
    EXPECT_EQ(tokens[0].type, TokenType::EQUAL);
    EXPECT_EQ(tokens[1].type, TokenType::STRING);
    EXPECT_EQ(tokens[2].type, TokenType::AMPERSAND);
    EXPECT_EQ(tokens[3].type, TokenType::STRING);
    EXPECT_EQ(tokens[4].type, TokenType::AMPERSAND);
    EXPECT_EQ(tokens[5].type, TokenType::STRING);
}

TEST(FormulaLexerTest, BooleanInFormula) {
    FormulaLexer lexer("=IF(TRUE,1,0)");
    auto tokens = lexer.tokenizeAll();
    // =, IF, (, TRUE, ,, 1, ,, 0, ), END
    EXPECT_EQ(tokens[3].type, TokenType::BOOLEAN);
    EXPECT_TRUE(tokens[3].booleanValue());
}

// ============================================================================
// Peek Tests
// ============================================================================

TEST(FormulaLexerTest, PeekDoesNotConsume) {
    FormulaLexer lexer("1+2");
    Token peeked = lexer.peekToken();
    Token next = lexer.nextToken();
    EXPECT_EQ(peeked.type, next.type);
    EXPECT_EQ(peeked.text, next.text);
}

TEST(FormulaLexerTest, MultiplePeeks) {
    FormulaLexer lexer("1+2");
    Token peek1 = lexer.peekToken();
    Token peek2 = lexer.peekToken();
    EXPECT_EQ(peek1.type, peek2.type);
    EXPECT_EQ(peek1.text, peek2.text);
}

// ============================================================================
// Token Type Name Tests
// ============================================================================

TEST(FormulaLexerTest, TokenTypeName) {
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::NUMBER), "NUMBER");
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::STRING), "STRING");
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::BOOLEAN), "BOOLEAN");
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::IDENTIFIER), "IDENTIFIER");
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::COLUMN), "COLUMN");
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::PLUS), "PLUS");
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::END_OF_INPUT), "END_OF_INPUT");
    EXPECT_STREQ(FormulaLexer::tokenTypeName(TokenType::ERROR), "ERROR");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(FormulaLexerTest, EmptyInput) {
    FormulaLexer lexer("");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::END_OF_INPUT);
}

TEST(FormulaLexerTest, OnlyWhitespace) {
    FormulaLexer lexer("   \t\n  ");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::END_OF_INPUT);
}

TEST(FormulaLexerTest, MultipleOperators) {
    FormulaLexer lexer("+-*/^");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::PLUS);
    EXPECT_EQ(tokens[1].type, TokenType::MINUS);
    EXPECT_EQ(tokens[2].type, TokenType::STAR);
    EXPECT_EQ(tokens[3].type, TokenType::SLASH);
    EXPECT_EQ(tokens[4].type, TokenType::CARET);
}

TEST(FormulaLexerTest, ComparisonChain) {
    FormulaLexer lexer("< <= <> > >=");
    auto tokens = lexer.tokenizeAll();
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::LESS);
    EXPECT_EQ(tokens[1].type, TokenType::LESS_EQUAL);
    EXPECT_EQ(tokens[2].type, TokenType::NOT_EQUAL);
    EXPECT_EQ(tokens[3].type, TokenType::GREATER);
    EXPECT_EQ(tokens[4].type, TokenType::GREATER_EQUAL);
}

TEST(FormulaLexerTest, RangeReference) {
    FormulaLexer lexer("A1:C10");
    auto tokens = lexer.tokenizeAll();
    // A, 1, :, C, 10, END
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[1].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[2].type, TokenType::COLON);
    EXPECT_EQ(tokens[3].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[4].type, TokenType::NUMBER);
}

TEST(FormulaLexerTest, WholeColumnReference) {
    FormulaLexer lexer("A:A");
    auto tokens = lexer.tokenizeAll();
    // A (COLUMN due to :), :, A (IDENTIFIER - parser handles context), END
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::COLUMN);
    EXPECT_EQ(tokens[1].type, TokenType::COLON);
    EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);  // Parser handles this
}

TEST(FormulaLexerTest, WholeRowReference) {
    FormulaLexer lexer("1:1");
    auto tokens = lexer.tokenizeAll();
    // 1, :, 1, END
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[1].type, TokenType::COLON);
    EXPECT_EQ(tokens[2].type, TokenType::NUMBER);
}

}  // namespace
}  // namespace cells
