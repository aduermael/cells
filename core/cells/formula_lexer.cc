#include "core/cells/formula_lexer.h"

#include <cctype>
#include <cstdlib>

#include <algorithm>

namespace cells {

// ============================================================================
// Token methods
// ============================================================================

double Token::numberValue() const {
    if (type != TokenType::NUMBER) {
        return 0.0;
    }
    // Parse the number from the text view
    std::string s(text);
    return std::strtod(s.c_str(), nullptr);
}

bool Token::booleanValue() const {
    if (type != TokenType::BOOLEAN) {
        return false;
    }
    // Case-insensitive check for TRUE
    if (text.size() == 4) {
        return (text[0] == 'T' || text[0] == 't');
    }
    return false;
}

std::string Token::stringValue() const {
    if (type != TokenType::STRING || text.size() < 2) {
        return std::string(text);
    }
    // Remove surrounding quotes and process escape sequences
    std::string result;
    result.reserve(text.size() - 2);

    // Skip first and last quote
    for (size_t i = 1; i < text.size() - 1; ++i) {
        if (text[i] == '"' && i + 1 < text.size() - 1 && text[i + 1] == '"') {
            // Escaped quote "" -> "
            result += '"';
            ++i;  // Skip the second quote
        } else {
            result += text[i];
        }
    }
    return result;
}

// ============================================================================
// FormulaLexer implementation
// ============================================================================

FormulaLexer::FormulaLexer(std::string_view source) : source_(source) {}

Token FormulaLexer::nextToken() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peekedToken_;
    }
    return scanToken();
}

Token FormulaLexer::peekToken() {
    if (!hasPeeked_) {
        peekedToken_ = scanToken();
        hasPeeked_ = true;
    }
    return peekedToken_;
}

bool FormulaLexer::isAtEnd() const {
    return pos_ >= source_.size();
}

std::vector<Token> FormulaLexer::tokenizeAll() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = nextToken();
        tokens.push_back(tok);
        if (tok.isEnd()) {
            break;
        }
    }
    return tokens;
}

const char* FormulaLexer::tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::END_OF_INPUT:
            return "END_OF_INPUT";
        case TokenType::NUMBER:
            return "NUMBER";
        case TokenType::STRING:
            return "STRING";
        case TokenType::BOOLEAN:
            return "BOOLEAN";
        case TokenType::IDENTIFIER:
            return "IDENTIFIER";
        case TokenType::COLUMN:
            return "COLUMN";
        case TokenType::ROW:
            return "ROW";
        case TokenType::PLUS:
            return "PLUS";
        case TokenType::MINUS:
            return "MINUS";
        case TokenType::STAR:
            return "STAR";
        case TokenType::SLASH:
            return "SLASH";
        case TokenType::CARET:
            return "CARET";
        case TokenType::AMPERSAND:
            return "AMPERSAND";
        case TokenType::EQUAL:
            return "EQUAL";
        case TokenType::NOT_EQUAL:
            return "NOT_EQUAL";
        case TokenType::LESS:
            return "LESS";
        case TokenType::LESS_EQUAL:
            return "LESS_EQUAL";
        case TokenType::GREATER:
            return "GREATER";
        case TokenType::GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TokenType::LPAREN:
            return "LPAREN";
        case TokenType::RPAREN:
            return "RPAREN";
        case TokenType::COMMA:
            return "COMMA";
        case TokenType::COLON:
            return "COLON";
        case TokenType::BANG:
            return "BANG";
        case TokenType::DOLLAR:
            return "DOLLAR";
        case TokenType::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

// ============================================================================
// Private helpers
// ============================================================================

char FormulaLexer::peek() const {
    if (isAtEndInternal())
        return '\0';
    return source_[pos_];
}

char FormulaLexer::peekNext() const {
    if (pos_ + 1 >= source_.size())
        return '\0';
    return source_[pos_ + 1];
}

char FormulaLexer::advance() {
    if (isAtEndInternal())
        return '\0';
    return source_[pos_++];
}

bool FormulaLexer::isAtEndInternal() const {
    return pos_ >= source_.size();
}

Token FormulaLexer::makeToken(TokenType type, size_t start) const {
    Token tok;
    tok.type = type;
    tok.text = source_.substr(start, pos_ - start);
    tok.position = {start, pos_};
    return tok;
}

Token FormulaLexer::makeErrorToken(const std::string& message, size_t start) const {
    Token tok;
    tok.type = TokenType::ERROR;
    tok.text = source_.substr(start, pos_ - start);
    tok.position = {start, pos_};
    tok.errorMessage = message;
    return tok;
}

void FormulaLexer::skipWhitespace() {
    while (!isAtEndInternal()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else {
            break;
        }
    }
}

bool FormulaLexer::isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool FormulaLexer::isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool FormulaLexer::isAlphaNumeric(char c) {
    return isAlpha(c) || isDigit(c);
}

bool FormulaLexer::isColumnChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// ============================================================================
// Token scanning
// ============================================================================

Token FormulaLexer::scanToken() {
    skipWhitespace();

    if (isAtEndInternal()) {
        return makeToken(TokenType::END_OF_INPUT, pos_);
    }

    size_t start = pos_;
    char c = advance();

    // Single-character tokens
    switch (c) {
        case '+':
            return makeToken(TokenType::PLUS, start);
        case '-':
            return makeToken(TokenType::MINUS, start);
        case '*':
            return makeToken(TokenType::STAR, start);
        case '/':
            return makeToken(TokenType::SLASH, start);
        case '^':
            return makeToken(TokenType::CARET, start);
        case '&':
            return makeToken(TokenType::AMPERSAND, start);
        case '(':
            return makeToken(TokenType::LPAREN, start);
        case ')':
            return makeToken(TokenType::RPAREN, start);
        case ',':
            return makeToken(TokenType::COMMA, start);
        case ':':
            return makeToken(TokenType::COLON, start);
        case '!':
            return makeToken(TokenType::BANG, start);
        case '$':
            return makeToken(TokenType::DOLLAR, start);
        case '=':
            return makeToken(TokenType::EQUAL, start);
        case '<':
            if (peek() == '>') {
                advance();
                return makeToken(TokenType::NOT_EQUAL, start);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenType::LESS_EQUAL, start);
            }
            return makeToken(TokenType::LESS, start);
        case '>':
            if (peek() == '=') {
                advance();
                return makeToken(TokenType::GREATER_EQUAL, start);
            }
            return makeToken(TokenType::GREATER, start);
        case '"':
            pos_ = start;  // Rewind to include the quote
            return scanString();
        default:
            break;
    }

    // Numbers
    if (isDigit(c) || (c == '.' && isDigit(peek()))) {
        pos_ = start;  // Rewind
        return scanNumber();
    }

    // Identifiers, columns, or booleans
    if (isAlpha(c)) {
        pos_ = start;  // Rewind
        return scanIdentifierOrColumn();
    }

    // Unknown character
    return makeErrorToken("Unexpected character", start);
}

Token FormulaLexer::scanNumber() {
    size_t start = pos_;

    // Integer part
    while (isDigit(peek())) {
        advance();
    }

    // Decimal part
    if (peek() == '.' && isDigit(peekNext())) {
        advance();  // Consume '.'
        while (isDigit(peek())) {
            advance();
        }
    }

    // Scientific notation (e.g., 1.5e10, 1E-5)
    if (peek() == 'e' || peek() == 'E') {
        char next = peekNext();
        if (isDigit(next) || next == '+' || next == '-') {
            advance();  // Consume 'e' or 'E'
            if (peek() == '+' || peek() == '-') {
                advance();  // Consume sign
            }
            if (!isDigit(peek())) {
                return makeErrorToken("Invalid number: expected digit after exponent", start);
            }
            while (isDigit(peek())) {
                advance();
            }
        }
    }

    // Check we got at least one digit
    if (pos_ == start) {
        advance();  // Consume the problematic character
        return makeErrorToken("Invalid number", start);
    }

    return makeToken(TokenType::NUMBER, start);
}

Token FormulaLexer::scanString() {
    size_t start = pos_;
    advance();  // Consume opening "

    while (!isAtEndInternal()) {
        char c = peek();
        if (c == '"') {
            // Check for escaped quote ""
            if (peekNext() == '"') {
                advance();  // Consume first "
                advance();  // Consume second "
            } else {
                advance();  // Consume closing "
                return makeToken(TokenType::STRING, start);
            }
        } else {
            advance();
        }
    }

    return makeErrorToken("Unterminated string", start);
}

Token FormulaLexer::scanIdentifierOrColumn() {
    size_t start = pos_;

    // Consume all letters first (only A-Z, a-z, not underscore)
    while (isColumnChar(peek())) {
        advance();
    }

    std::string_view letters = source_.substr(start, pos_ - start);

    // Check if we have underscore (making it definitely an identifier)
    if (peek() == '_') {
        // Continue consuming for identifier
        while (isAlphaNumeric(peek())) {
            advance();
        }
        std::string_view identifier = source_.substr(start, pos_ - start);

        // Check for booleans (though unlikely with underscore)
        std::string upper(identifier);
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper == "TRUE" || upper == "FALSE") {
            return makeToken(TokenType::BOOLEAN, start);
        }
        return makeToken(TokenType::IDENTIFIER, start);
    }

    // Check for booleans first
    std::string upperLetters(letters);
    std::transform(upperLetters.begin(), upperLetters.end(), upperLetters.begin(), ::toupper);
    if (upperLetters == "TRUE" || upperLetters == "FALSE") {
        return makeToken(TokenType::BOOLEAN, start);
    }

    // Check if this could be a valid column name (1-3 letters)
    bool couldBeColumn = (letters.size() >= 1 && letters.size() <= 3);

    // Check if it's followed by digits (making it a cell reference: A1, AA100)
    if (couldBeColumn && isDigit(peek())) {
        return makeToken(TokenType::COLUMN, start);
    }

    // Check if it's followed by $ then digit (absolute row reference: $A$1)
    // In $A$1, after consuming A, we see $ then 1
    if (couldBeColumn && peek() == '$') {
        // Look ahead past the $
        size_t lookahead = pos_ + 1;
        if (lookahead < source_.size() && isDigit(source_[lookahead])) {
            return makeToken(TokenType::COLUMN, start);
        }
    }

    // Check if it's followed by ( - definitely a function call
    if (peek() == '(') {
        return makeToken(TokenType::IDENTIFIER, start);
    }

    // Check if it's followed by : (whole column reference: A:A, AA:BB)
    if (couldBeColumn && peek() == ':') {
        return makeToken(TokenType::COLUMN, start);
    }

    // Not a clear column context - consume any remaining alphanumeric for identifiers
    // The parser will handle ambiguous cases like standalone "A" or "AA"
    while (isAlphaNumeric(peek())) {
        advance();
    }

    // It's an identifier (function name, sheet name, or named range)
    // The parser can reinterpret 1-3 letter identifiers as columns based on context
    return makeToken(TokenType::IDENTIFIER, start);
}

Token FormulaLexer::scanRowNumber() {
    size_t start = pos_;

    while (isDigit(peek())) {
        advance();
    }

    if (pos_ == start) {
        return makeErrorToken("Expected row number", start);
    }

    return makeToken(TokenType::ROW, start);
}

}  // namespace cells
