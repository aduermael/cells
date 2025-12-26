#ifndef CELLS_FORMULA_EVAL_H_
#define CELLS_FORMULA_EVAL_H_

#include <cstdint>

#include <string>
#include <unordered_set>

#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct ASTNode;
struct Sheet;
struct Workbook;

// Result of evaluating a formula or sub-expression
struct EvalResult {
    enum class Type : std::uint8_t { NUMBER, STRING, BOOLEAN, ERROR, EMPTY };
    Type type{Type::EMPTY};
    double numberValue{0.0};
    std::string stringValue;
    bool boolValue{false};
    CellError error{CellError::NONE};

    // Default constructor creates an empty result
    EvalResult() = default;

    // Factory methods
    static EvalResult Number(double v) {
        EvalResult r;
        r.type = Type::NUMBER;
        r.numberValue = v;
        return r;
    }

    static EvalResult String(std::string v) {
        EvalResult r;
        r.type = Type::STRING;
        r.stringValue = std::move(v);
        return r;
    }

    static EvalResult Boolean(bool v) {
        EvalResult r;
        r.type = Type::BOOLEAN;
        r.boolValue = v;
        return r;
    }

    static EvalResult Error(CellError e) {
        EvalResult r;
        r.type = Type::ERROR;
        r.error = e;
        return r;
    }

    static EvalResult Empty() {
        EvalResult r;
        r.type = Type::EMPTY;
        return r;
    }

    // Type coercion methods
    // Converts to number:
    // - Number: returns as-is
    // - String: parses as number, returns VALUE error if invalid
    // - Boolean: true=1, false=0
    // - Error: propagates error
    // - Empty: returns 0
    [[nodiscard]] EvalResult toNumber() const {
        switch (type) {
            case Type::NUMBER:
                return *this;
            case Type::STRING: {
                if (stringValue.empty()) {
                    return Number(0.0);
                }
                // Try to parse as number
                try {
                    size_t pos = 0;
                    double val = std::stod(stringValue, &pos);
                    // Check if entire string was consumed
                    if (pos == stringValue.size()) {
                        return Number(val);
                    }
                    return Error(CellError::VALUE);
                } catch (...) {
                    return Error(CellError::VALUE);
                }
            }
            case Type::BOOLEAN:
                return Number(boolValue ? 1.0 : 0.0);
            case Type::ERROR:
                return *this;
            case Type::EMPTY:
                return Number(0.0);
        }
        return Error(CellError::VALUE);
    }

    // Converts to string:
    // - String: returns as-is
    // - Number: formats as string
    // - Boolean: "TRUE" or "FALSE"
    // - Error: error string
    // - Empty: empty string
    [[nodiscard]] EvalResult toString() const {
        switch (type) {
            case Type::STRING:
                return *this;
            case Type::NUMBER: {
                // Format number, avoiding unnecessary decimal places
                if (std::floor(numberValue) == numberValue && std::abs(numberValue) < 1e15) {
                    return String(std::to_string(static_cast<long long>(numberValue)));
                }
                std::string s = std::to_string(numberValue);
                // Remove trailing zeros after decimal point
                size_t dot = s.find('.');
                if (dot != std::string::npos) {
                    size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && last > dot) {
                        s = s.substr(0, last + 1);
                    } else if (last == dot) {
                        s = s.substr(0, dot);
                    }
                }
                return String(s);
            }
            case Type::BOOLEAN:
                return String(boolValue ? "TRUE" : "FALSE");
            case Type::ERROR:
                return String(errorToString(error));
            case Type::EMPTY:
                return String("");
        }
        return String("");
    }

    // Converts to boolean:
    // - Boolean: returns as-is
    // - Number: 0=false, non-zero=true
    // - String: not directly convertible, returns VALUE error
    // - Error: propagates error
    // - Empty: returns false
    [[nodiscard]] EvalResult toBoolean() const {
        switch (type) {
            case Type::BOOLEAN:
                return *this;
            case Type::NUMBER:
                return Boolean(numberValue != 0.0);
            case Type::STRING:
                // Strings don't implicitly convert to boolean
                return Error(CellError::VALUE);
            case Type::ERROR:
                return *this;
            case Type::EMPTY:
                return Boolean(false);
        }
        return Error(CellError::VALUE);
    }

    // Type checking
    [[nodiscard]] bool isError() const { return type == Type::ERROR; }
    [[nodiscard]] bool isNumber() const { return type == Type::NUMBER; }
    [[nodiscard]] bool isString() const { return type == Type::STRING; }
    [[nodiscard]] bool isBoolean() const { return type == Type::BOOLEAN; }
    [[nodiscard]] bool isEmpty() const { return type == Type::EMPTY; }

    // Get the number value (assumes type is NUMBER)
    [[nodiscard]] double getNumber() const { return numberValue; }

    // Get the string value (assumes type is STRING)
    [[nodiscard]] const std::string& getString() const { return stringValue; }

    // Get the boolean value (assumes type is BOOLEAN)
    [[nodiscard]] bool getBoolean() const { return boolValue; }

    // Get the error (assumes type is ERROR)
    [[nodiscard]] CellError getError() const { return error; }
};

// Context for evaluation (sheet access, cell positions, etc.)
struct EvalContext {
    Sheet* sheet{nullptr};
    Workbook* workbook{nullptr};
    ID currentCellId;  // For relative reference resolution
    int recursionDepth{0};
    static const int MAX_RECURSION = 1000;

    // Circular reference detection during evaluation
    std::unordered_set<ID>* evaluatingCells{nullptr};
};

// Main evaluation function (implemented in formula_eval.cc)
EvalResult evaluate(const ASTNode* node, EvalContext& ctx);

}  // namespace cells

#endif  // CELLS_FORMULA_EVAL_H_
