#ifndef CELLS_FORMULA_LEXER_H_
#define CELLS_FORMULA_LEXER_H_

#include <cstddef>
#include <cstdint>

#include <string>
#include <string_view>
#include <vector>

namespace cells {

// Token types for formula lexing
enum class TokenType : std::uint8_t {
    // End of input
    END_OF_INPUT,

    // Literals
    NUMBER,   // 42, 3.14, 1.5e10, -100
    STRING,   // "Hello", "with ""quotes"""
    BOOLEAN,  // TRUE, FALSE (case-insensitive)

    // Identifiers and references
    IDENTIFIER,  // Function names, named ranges (SUM, myRange)
    COLUMN,      // Column letters (A, AA, XFD)
    ROW,         // Row number (1, 100, 1048576)

    // UUID-based references (for internal storage format)
    // Cell ref: $$, $~, ~$, ~~ followed by 8-char cell UUID
    UUID_CELL_REF,
    // Column ref: @$ or @~ followed by 8-char column UUID
    UUID_COLUMN_REF,
    // Row ref: #$ or #~ followed by 8-char row UUID
    UUID_ROW_REF,

    // Operators
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /
    CARET,          // ^
    AMPERSAND,      // &
    EQUAL,          // =
    NOT_EQUAL,      // <>
    LESS,           // <
    LESS_EQUAL,     // <=
    GREATER,        // >
    GREATER_EQUAL,  // >=

    // Punctuation
    LPAREN,  // (
    RPAREN,  // )
    COMMA,   // ,
    COLON,   // :
    BANG,    // !
    DOLLAR,  // $

    // Error token (for invalid input)
    ERROR,
};

// Source position for error reporting
struct SourcePosition {
    size_t start{0};  // Start offset in source string
    size_t end{0};    // End offset (exclusive)

    [[nodiscard]] size_t length() const { return end - start; }
};

// A single token from the lexer
struct Token {
    TokenType type{TokenType::END_OF_INPUT};
    std::string_view text;  // View into source string
    SourcePosition position;
    std::string errorMessage;  // Only set for ERROR tokens

    Token() = default;
    Token(TokenType t, std::string_view txt, SourcePosition pos)
        : type(t), text(txt), position(pos) {}

    [[nodiscard]] bool isError() const { return type == TokenType::ERROR; }
    [[nodiscard]] bool isEnd() const { return type == TokenType::END_OF_INPUT; }

    // Get the numeric value (for NUMBER tokens)
    [[nodiscard]] double numberValue() const;

    // Get the boolean value (for BOOLEAN tokens)
    [[nodiscard]] bool booleanValue() const;

    // Get the string value with quotes removed and escapes processed (for STRING tokens)
    [[nodiscard]] std::string stringValue() const;
};

// Lexer for Excel-style formulas
// Usage:
//   FormulaLexer lexer("=A1+B2");
//   while (true) {
//       Token tok = lexer.nextToken();
//       if (tok.isEnd()) break;
//       // process token
//   }
class FormulaLexer {
public:
    explicit FormulaLexer(std::string_view source);

    // Get next token
    Token nextToken();

    // Peek at next token without consuming it
    [[nodiscard]] Token peekToken();

    // Check if at end of input
    [[nodiscard]] bool isAtEnd() const;

    // Get current position
    [[nodiscard]] size_t currentPosition() const { return pos_; }

    // Get source string
    [[nodiscard]] std::string_view source() const { return source_; }

    // Tokenize entire source and return all tokens
    [[nodiscard]] std::vector<Token> tokenizeAll();

    // Convert token type to string for debugging
    [[nodiscard]] static const char* tokenTypeName(TokenType type);

private:
    std::string_view source_;
    size_t pos_{0};
    Token peekedToken_;
    bool hasPeeked_{false};

    // Character helpers
    [[nodiscard]] char peek() const;
    [[nodiscard]] char peekNext() const;
    char advance();
    [[nodiscard]] bool isAtEndInternal() const;

    // Token scanning methods
    Token scanToken();
    Token scanNumber();
    Token scanString();
    Token scanIdentifierOrColumn();
    Token scanRowNumber();
    Token scanUuidCellRef(size_t start, bool colAbsolute, bool rowAbsolute);
    Token scanUuidColumnRef(size_t start, bool absolute);
    Token scanUuidRowRef(size_t start, bool absolute);

    // Helper to create token
    [[nodiscard]] Token makeToken(TokenType type, size_t start) const;
    [[nodiscard]] Token makeErrorToken(const std::string& message, size_t start) const;

    // Skip whitespace
    void skipWhitespace();

    // Character classification
    [[nodiscard]] static bool isDigit(char c);
    [[nodiscard]] static bool isAlpha(char c);
    [[nodiscard]] static bool isAlphaNumeric(char c);
    [[nodiscard]] static bool isColumnChar(char c);
};

}  // namespace cells

#endif  // CELLS_FORMULA_LEXER_H_
