#ifndef CELLS_FORMULA_PARSER_H_
#define CELLS_FORMULA_PARSER_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_lexer.h"

namespace cells {

// Parser for Excel-style formulas
// Produces an AST from a formula string
//
// Grammar (EBNF):
//   formula     = "=" expression
//   expression  = comparison
//   comparison  = concat (("=" | "<>" | "<" | "<=" | ">" | ">=") concat)*
//   concat      = additive ("&" additive)*
//   additive    = multiplicative (("+" | "-") multiplicative)*
//   multiplicative = power (("*" | "/") power)*
//   power       = unary ("^" unary)*
//   unary       = ("-" | "+")? primary
//   primary     = literal | reference | function_call | "(" expression ")"
class FormulaParser {
public:
    explicit FormulaParser(std::string_view source);

    // Parse the formula and return the AST
    // Returns nullptr if the formula is completely invalid
    // Returns an ErrorNode if there are syntax errors (for error recovery)
    [[nodiscard]] std::unique_ptr<ASTNode> parse();

    // Get any error messages from parsing
    [[nodiscard]] const std::vector<std::string>& errors() const { return errors_; }

    // Check if parsing had errors
    [[nodiscard]] bool hasErrors() const { return !errors_.empty(); }

private:
    FormulaLexer lexer_;
    Token current_;
    std::vector<std::string> errors_;

    // Token handling
    void advance();
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] bool match(TokenType type);
    bool expect(TokenType type, const std::string& message);
    [[nodiscard]] Token previous() const { return previous_; }
    Token previous_;

    // Error handling
    void error(const std::string& message);
    std::unique_ptr<ASTNode> errorNode(const std::string& message);
    void synchronize();

    // Grammar rules
    std::unique_ptr<ASTNode> formula();
    std::unique_ptr<ASTNode> expression();
    std::unique_ptr<ASTNode> comparison();
    std::unique_ptr<ASTNode> concat();
    std::unique_ptr<ASTNode> additive();
    std::unique_ptr<ASTNode> multiplicative();
    std::unique_ptr<ASTNode> power();
    std::unique_ptr<ASTNode> unary();
    std::unique_ptr<ASTNode> primary();

    // Reference parsing
    std::unique_ptr<ASTNode> parseReference();
    std::unique_ptr<ASTNode> parseCellOrRangeRef(const std::string& sheetName);
    std::unique_ptr<ASTNode> parseRowRef(bool startAbsolute, int startRow,
                                         const std::string& sheetName);
    std::unique_ptr<ASTNode> parseFunctionCall(const std::string& name);

    // UUID reference parsing (for stored formula format)
    std::unique_ptr<ASTNode> parseUuidCellRef();
    std::unique_ptr<ASTNode> parseUuidColumnRef();
    std::unique_ptr<ASTNode> parseUuidRowRef();

    // Helper to check if identifier could be a column name
    [[nodiscard]] static bool isValidColumnName(const std::string& name);

    // Helper to parse cell reference components
    struct CellRefComponents {
        std::string column;
        int row{0};
        bool colAbsolute{false};
        bool rowAbsolute{false};
        bool valid{false};
    };
    [[nodiscard]] CellRefComponents parseCellRefComponents();
};

}  // namespace cells

#endif  // CELLS_FORMULA_PARSER_H_
