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
    const std::string s(text);
    return std::strtod(s.c_str(), nullptr);
}

double Token::percentValue() const {
    if (type != TokenType::PERCENT_LITERAL) {
        return 0.0;
    }
    // Parse the number from the text view (excluding trailing %)
    // text is like "15%" or "12.5%"
    std::string s(text);
    if (!s.empty() && s.back() == '%') {
        s.pop_back();
    }
    return std::strtod(s.c_str(), nullptr) / 100.0;
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

std::string Token::quotedSheetNameValue() const {
    if (type != TokenType::QUOTED_SHEET_NAME || text.size() < 2) {
        return std::string(text);
    }
    // Remove surrounding single quotes and process escape sequences
    // 'Sheet Name' -> "Sheet Name", 'It''s here' -> "It's here"
    std::string result;
    result.reserve(text.size() - 2);

    // Skip first and last quote
    for (size_t i = 1; i < text.size() - 1; ++i) {
        if (text[i] == '\'' && i + 1 < text.size() - 1 && text[i + 1] == '\'') {
            // Escaped quote '' -> '
            result += '\'';
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
        const Token tok = nextToken();
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
        case TokenType::PERCENT_LITERAL:
            return "PERCENT_LITERAL";
        case TokenType::STRING:
            return "STRING";
        case TokenType::BOOLEAN:
            return "BOOLEAN";
        case TokenType::ERROR_LITERAL:
            return "ERROR_LITERAL";
        case TokenType::IDENTIFIER:
            return "IDENTIFIER";
        case TokenType::COLUMN:
            return "COLUMN";
        case TokenType::ROW:
            return "ROW";
        case TokenType::QUOTED_SHEET_NAME:
            return "QUOTED_SHEET_NAME";
        case TokenType::UUID_SHEET_REF:
            return "UUID_SHEET_REF";
        case TokenType::UUID_CELL_REF:
            return "UUID_CELL_REF";
        case TokenType::UUID_COLUMN_REF:
            return "UUID_COLUMN_REF";
        case TokenType::UUID_ROW_REF:
            return "UUID_ROW_REF";
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
        case TokenType::HASH:
            return "HASH";
        case TokenType::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

// ============================================================================
// Private helpers
// ============================================================================

char FormulaLexer::peek() const {
    if (isAtEndInternal()) {
        return '\0';
    }
    return source_[pos_];
}

char FormulaLexer::peekNext() const {
    if (pos_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + 1];
}

char FormulaLexer::advance() {
    if (isAtEndInternal()) {
        return '\0';
    }
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
        const char c = peek();
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

    const size_t start = pos_;
    const char c = advance();

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
            // Check for UUID sheet ref: ! followed by exactly 8 alphanumeric chars
            // We need to look ahead 8 chars to distinguish from A1 notation like Sheet2!A1
            if (pos_ + 8 <= source_.size()) {
                bool isSheetUuid = true;
                for (size_t j = 0; j < 8 && isSheetUuid; ++j) {
                    if (!isAlphaNumeric(source_[pos_ + j])) {
                        isSheetUuid = false;
                    }
                }
                // Also check that it's followed by a cell/column/row UUID prefix or end
                // This ensures we don't match things like !SEQUENCE( as a sheet UUID
                if (isSheetUuid && pos_ + 8 < source_.size()) {
                    const char nextChar = source_[pos_ + 8];
                    // Valid follows: $, ~, @, # (UUID prefixes), or non-alphanumeric (operators,
                    // etc)
                    if (isAlphaNumeric(nextChar) && nextChar != '$' && nextChar != '~' &&
                        nextChar != '@' && nextChar != '#') {
                        isSheetUuid = false;
                    }
                }
                if (isSheetUuid) {
                    return scanUuidSheetRef(start);
                }
            }
            return makeToken(TokenType::BANG, start);
        case '$':
            // Check for UUID cell ref: $$ or $~
            if (peek() == '$' || peek() == '~') {
                const bool rowAbsolute = (peek() == '$');
                advance();  // Consume second char
                return scanUuidCellRef(start, true, rowAbsolute);
            }
            return makeToken(TokenType::DOLLAR, start);
        case '~':
            // Check for UUID cell ref: ~$ or ~~
            if (peek() == '$' || peek() == '~') {
                const bool rowAbsolute = (peek() == '$');
                advance();  // Consume second char
                return scanUuidCellRef(start, false, rowAbsolute);
            }
            return makeErrorToken("Unexpected character '~'", start);
        case '@':
            // Check for UUID column ref: @$ or @~
            if (peek() == '$' || peek() == '~') {
                const bool absolute = (peek() == '$');
                advance();  // Consume second char
                return scanUuidColumnRef(start, absolute);
            }
            return makeErrorToken("Unexpected character '@'", start);
        case '#':
            // Check for UUID row ref: #$ or #~
            if (peek() == '$' || peek() == '~') {
                const bool absolute = (peek() == '$');
                advance();  // Consume second char
                return scanUuidRowRef(start, absolute);
            }
            // Check for error literals: #REF!, #VALUE!, #DIV/0!, etc.
            if (isAlpha(peek())) {
                return scanErrorLiteral(start);
            }
            // Otherwise it's the spill range operator (e.g., A1#)
            return makeToken(TokenType::HASH, start);
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
        case '\'':
            // Quoted sheet name: 'Sheet Name' (for cross-sheet references with spaces)
            pos_ = start;  // Rewind to include the quote
            return scanQuotedSheetName(start);
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
    const size_t start = pos_;

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
        const char next = peekNext();
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

    // Check for percentage suffix
    if (peek() == '%') {
        advance();  // Consume '%'
        return makeToken(TokenType::PERCENT_LITERAL, start);
    }

    return makeToken(TokenType::NUMBER, start);
}

Token FormulaLexer::scanString() {
    const size_t start = pos_;
    advance();  // Consume opening "

    while (!isAtEndInternal()) {
        const char c = peek();
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

Token FormulaLexer::scanQuotedSheetName(size_t start) {
    advance();  // Consume opening '

    while (!isAtEndInternal()) {
        const char c = peek();
        if (c == '\'') {
            // Check for escaped quote ''
            if (peekNext() == '\'') {
                advance();  // Consume first '
                advance();  // Consume second '
            } else {
                advance();  // Consume closing '
                return makeToken(TokenType::QUOTED_SHEET_NAME, start);
            }
        } else {
            advance();
        }
    }

    return makeErrorToken("Unterminated quoted sheet name", start);
}

Token FormulaLexer::scanIdentifierOrColumn() {
    const size_t start = pos_;

    // Consume all letters first (only A-Z, a-z, not underscore)
    while (isColumnChar(peek())) {
        advance();
    }

    const std::string_view letters = source_.substr(start, pos_ - start);

    // Dotted Excel names (RANK.EQ, MODE.SNGL, CEILING.MATH) when followed by '('.
    if (peek() == '.') {
        size_t look = pos_;
        bool dottedFn = false;
        while (look < source_.size() && source_[look] == '.') {
            ++look;
            if (look >= source_.size() || !isAlpha(source_[look])) {
                dottedFn = false;
                break;
            }
            while (look < source_.size() && isAlphaNumeric(source_[look])) {
                ++look;
            }
            if (look < source_.size() && source_[look] == '(') {
                dottedFn = true;
                break;
            }
            if (look < source_.size() && source_[look] == '.') {
                continue;
            }
            dottedFn = false;
            break;
        }
        if (dottedFn) {
            while (pos_ < look) {
                advance();
            }
            return makeToken(TokenType::IDENTIFIER, start);
        }
    }

    // Check if we have underscore (making it definitely an identifier)
    if (peek() == '_') {
        // Continue consuming for identifier
        while (isAlphaNumeric(peek())) {
            advance();
        }
        const std::string_view identifier = source_.substr(start, pos_ - start);

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
    const bool couldBeColumn = (!letters.empty() && letters.size() <= 3);

    // Function names may contain digits (LOG10, BIN2DEC, HEX2OCT). If the
    // remaining alphanumeric run is followed by '(', this is a function call
    // rather than a cell/column reference.
    if (isAlphaNumeric(peek())) {
        size_t look = pos_;
        while (look < source_.size() && isAlphaNumeric(source_[look])) {
            ++look;
        }
        if (look < source_.size() && source_[look] == '(') {
            while (pos_ < look) {
                advance();
            }
            return makeToken(TokenType::IDENTIFIER, start);
        }
    }

    // Followed by digits: cell reference (A1, AA100)
    if (couldBeColumn && isDigit(peek())) {
        return makeToken(TokenType::COLUMN, start);
    }

    // Check if it's followed by $ then digit (absolute row reference: $A$1)
    // In $A$1, after consuming A, we see $ then 1
    if (couldBeColumn && peek() == '$') {
        // Look ahead past the $
        const size_t lookahead = pos_ + 1;
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
    const size_t start = pos_;

    while (isDigit(peek())) {
        advance();
    }

    if (pos_ == start) {
        return makeErrorToken("Expected row number", start);
    }

    return makeToken(TokenType::ROW, start);
}

Token FormulaLexer::scanUuidSheetRef(size_t start) {
    // After '!', consume 8 alphanumeric chars for sheet UUID
    constexpr size_t UUID_LENGTH = 8;
    size_t count = 0;

    while (count < UUID_LENGTH && !isAtEndInternal()) {
        const char c = peek();
        if (isAlphaNumeric(c)) {
            advance();
            ++count;
        } else {
            break;
        }
    }

    if (count != UUID_LENGTH) {
        return makeErrorToken("Invalid UUID sheet reference: expected 8 alphanumeric characters",
                              start);
    }

    return makeToken(TokenType::UUID_SHEET_REF, start);
}

Token FormulaLexer::scanUuidCellRef(size_t start, bool /*colAbsolute*/, bool /*rowAbsolute*/) {
    // After prefix ($$, $~, ~$, or ~~), consume 8 alphanumeric chars
    constexpr size_t UUID_LENGTH = 8;
    size_t count = 0;

    while (count < UUID_LENGTH && !isAtEndInternal()) {
        const char c = peek();
        if (isAlphaNumeric(c)) {
            advance();
            ++count;
        } else {
            break;
        }
    }

    if (count != UUID_LENGTH) {
        return makeErrorToken("Invalid UUID cell reference: expected 8 alphanumeric characters",
                              start);
    }

    return makeToken(TokenType::UUID_CELL_REF, start);
}

Token FormulaLexer::scanUuidColumnRef(size_t start, bool /*absolute*/) {
    // After prefix (@$ or @~), consume 8 alphanumeric chars
    constexpr size_t UUID_LENGTH = 8;
    size_t count = 0;

    while (count < UUID_LENGTH && !isAtEndInternal()) {
        const char c = peek();
        if (isAlphaNumeric(c)) {
            advance();
            ++count;
        } else {
            break;
        }
    }

    if (count != UUID_LENGTH) {
        return makeErrorToken("Invalid UUID column reference: expected 8 alphanumeric characters",
                              start);
    }

    return makeToken(TokenType::UUID_COLUMN_REF, start);
}

Token FormulaLexer::scanUuidRowRef(size_t start, bool /*absolute*/) {
    // After prefix (#$ or #~), consume 8 alphanumeric chars
    constexpr size_t UUID_LENGTH = 8;
    size_t count = 0;

    while (count < UUID_LENGTH && !isAtEndInternal()) {
        const char c = peek();
        if (isAlphaNumeric(c)) {
            advance();
            ++count;
        } else {
            break;
        }
    }

    if (count != UUID_LENGTH) {
        return makeErrorToken("Invalid UUID row reference: expected 8 alphanumeric characters",
                              start);
    }

    return makeToken(TokenType::UUID_ROW_REF, start);
}

Token FormulaLexer::scanErrorLiteral(size_t start) {
    // We've already consumed '#' and confirmed peek() is a letter
    // Scan letters (and special chars like / for #DIV/0!)
    while (!isAtEndInternal()) {
        const char c = peek();
        if (isAlpha(c) || c == '/') {
            advance();
        } else {
            break;
        }
    }

    // Check for trailing ! or ? (e.g., #REF!, #NAME?)
    if (!isAtEndInternal() && (peek() == '!' || peek() == '?')) {
        advance();
    }

    // Get the error text (including the leading #)
    const std::string_view errorText = source_.substr(start, pos_ - start);

    // Validate that this is a known error type
    // Valid errors: #REF!, #VALUE!, #DIV/0!, #NAME?, #N/A, #NULL!, #NUM!, #SPILL!, #CALC!
    std::string upper(errorText);
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    if (upper == "#REF!" || upper == "#VALUE!" || upper == "#DIV/0!" || upper == "#NAME?" ||
        upper == "#N/A" || upper == "#NULL!" || upper == "#NUM!" || upper == "#SPILL!" ||
        upper == "#CALC!") {
        return makeToken(TokenType::ERROR_LITERAL, start);
    }

    // Not a recognized error - return it as HASH followed by letters
    // Rewind to just after the # and let the parser handle it
    pos_ = start + 1;
    return makeToken(TokenType::HASH, start);
}

}  // namespace cells
