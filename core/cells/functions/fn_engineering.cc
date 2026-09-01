#include "core/cells/functions/fn_engineering.h"

#include <cctype>
#include <cmath>
#include <cstdint>

#include <algorithm>
#include <string>
#include <utility>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {
namespace {

constexpr std::int64_t kBitMax = 281474976710655LL;  // 2^48 - 1
constexpr int kBinBits = 10;
constexpr int kOctBits = 30;
constexpr int kHexBits = 40;

EvalResult requireBitNumber(const ASTNode* arg, EvalContext& ctx, std::int64_t& out) {
    const EvalResult n = evaluateAsNumber(arg, ctx);
    if (n.isError()) {
        return n;
    }
    const double v = std::trunc(n.getNumber());
    if (!std::isfinite(v) || v < 0.0 || v > static_cast<double>(kBitMax)) {
        return EvalResult::Error(CellError::NUM);
    }
    out = static_cast<std::int64_t>(v);
    return EvalResult::Empty();
}

int digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u >= 'A' && u <= 'Z') {
        return u - 'A' + 10;
    }
    return -1;
}

char digitChar(int d) {
    return static_cast<char>(d < 10 ? '0' + d : 'A' + (d - 10));
}

int bitsForBase(int base) {
    if (base == 2) {
        return kBinBits;
    }
    if (base == 8) {
        return kOctBits;
    }
    return kHexBits;
}

EvalResult parseTwos(const std::string& s, int base) {
    if (s.empty() || s.size() > 10) {
        return EvalResult::Error(CellError::NUM);
    }
    const int bits = bitsForBase(base);
    std::int64_t value = 0;
    for (char c : s) {
        const int d = digitValue(c);
        if (d < 0 || d >= base) {
            return EvalResult::Error(CellError::NUM);
        }
        value = value * base + d;
    }
    const std::int64_t signBit = 1LL << (bits - 1);
    const std::int64_t full = 1LL << bits;
    if (value >= signBit) {
        value -= full;
    }
    return EvalResult::Number(static_cast<double>(value));
}

EvalResult formatTwos(std::int64_t value, int base, int places) {
    const int bits = bitsForBase(base);
    const std::int64_t minV = -(1LL << (bits - 1));
    const std::int64_t maxV = (1LL << (bits - 1)) - 1;
    if (value < minV || value > maxV) {
        return EvalResult::Error(CellError::NUM);
    }
    std::uint64_t u = 0;
    if (value < 0) {
        u = static_cast<std::uint64_t>(value + (1LL << bits));
    } else {
        u = static_cast<std::uint64_t>(value);
    }
    std::string digits;
    if (u == 0) {
        digits = "0";
    } else {
        while (u > 0) {
            digits.push_back(digitChar(static_cast<int>(u % static_cast<unsigned>(base))));
            u /= static_cast<unsigned>(base);
        }
        std::reverse(digits.begin(), digits.end());
    }
    if (value < 0) {
        if (digits.size() < 10) {
            digits.insert(digits.begin(), 10 - digits.size(), '0');
        }
        return EvalResult::String(digits);
    }
    if (places >= 0) {
        if (static_cast<int>(digits.size()) > places) {
            return EvalResult::Error(CellError::NUM);
        }
        if (static_cast<int>(digits.size()) < places) {
            digits.insert(digits.begin(), static_cast<size_t>(places) - digits.size(), '0');
        }
    }
    return EvalResult::String(digits);
}

EvalResult evalTextArg(const ASTNode* arg, EvalContext& ctx, std::string& out) {
    const EvalResult r = evaluate(arg, ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isNumber()) {
        const double v = r.getNumber();
        if (std::floor(v) == v && std::abs(v) < 1e15) {
            out = std::to_string(static_cast<long long>(v));
        } else {
            const EvalResult s = r.toString();
            if (s.isError()) {
                return s;
            }
            out = s.getString();
        }
        return EvalResult::Empty();
    }
    const EvalResult s = r.toString();
    if (s.isError()) {
        return s;
    }
    out = s.getString();
    return EvalResult::Empty();
}

EvalResult fromBaseFn(const std::vector<const ASTNode*>& args, EvalContext& ctx, int base) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::string text;
    const EvalResult e = evalTextArg(args[0], ctx, text);
    if (e.isError()) {
        return e;
    }
    return parseTwos(text, base);
}

EvalResult toBaseFn(const std::vector<const ASTNode*>& args, EvalContext& ctx, int base) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    const auto value = static_cast<std::int64_t>(std::trunc(n.getNumber()));
    int places = -1;
    if (args.size() == 2) {
        const EvalResult p = evaluateAsNumber(args[1], ctx);
        if (p.isError()) {
            return p;
        }
        places = static_cast<int>(std::floor(p.getNumber()));
        if (places < 1 || places > 10) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return formatTwos(value, base, places);
}

EvalResult convertBaseFn(const std::vector<const ASTNode*>& args, EvalContext& ctx, int fromBase,
                         int toBase) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::string text;
    const EvalResult e = evalTextArg(args[0], ctx, text);
    if (e.isError()) {
        return e;
    }
    const EvalResult parsed = parseTwos(text, fromBase);
    if (parsed.isError()) {
        return parsed;
    }
    const auto value = static_cast<std::int64_t>(parsed.getNumber());
    int places = -1;
    if (args.size() == 2) {
        const EvalResult p = evaluateAsNumber(args[1], ctx);
        if (p.isError()) {
            return p;
        }
        places = static_cast<int>(std::floor(p.getNumber()));
        if (places < 1 || places > 10) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return formatTwos(value, toBase, places);
}

std::string formatComplexPart(double v) {
    if (std::floor(v) == v && std::abs(v) < 1e15) {
        return std::to_string(static_cast<long long>(v));
    }
    std::string s = std::to_string(v);
    const size_t dot = s.find('.');
    if (dot != std::string::npos) {
        const size_t last = s.find_last_not_of('0');
        if (last != std::string::npos && last > dot) {
            s = s.substr(0, last + 1);
        } else if (last == dot) {
            s = s.substr(0, dot);
        }
    }
    return s;
}

}  // namespace

EvalResult fn_BITAND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::int64_t a = 0;
    std::int64_t b = 0;
    EvalResult e = requireBitNumber(args[0], ctx, a);
    if (e.isError()) {
        return e;
    }
    e = requireBitNumber(args[1], ctx, b);
    if (e.isError()) {
        return e;
    }
    return EvalResult::Number(static_cast<double>(a & b));
}

EvalResult fn_BITOR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::int64_t a = 0;
    std::int64_t b = 0;
    EvalResult e = requireBitNumber(args[0], ctx, a);
    if (e.isError()) {
        return e;
    }
    e = requireBitNumber(args[1], ctx, b);
    if (e.isError()) {
        return e;
    }
    return EvalResult::Number(static_cast<double>(a | b));
}

EvalResult fn_BITXOR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::int64_t a = 0;
    std::int64_t b = 0;
    EvalResult e = requireBitNumber(args[0], ctx, a);
    if (e.isError()) {
        return e;
    }
    e = requireBitNumber(args[1], ctx, b);
    if (e.isError()) {
        return e;
    }
    return EvalResult::Number(static_cast<double>(a ^ b));
}

EvalResult fn_BITLSHIFT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::int64_t n = 0;
    EvalResult e = requireBitNumber(args[0], ctx, n);
    if (e.isError()) {
        return e;
    }
    const EvalResult shRes = evaluateAsNumber(args[1], ctx);
    if (shRes.isError()) {
        return shRes;
    }
    const auto shift = static_cast<std::int64_t>(std::trunc(shRes.getNumber()));
    if (shift < -53 || shift > 53) {
        return EvalResult::Error(CellError::NUM);
    }
    std::uint64_t u = static_cast<std::uint64_t>(n);
    if (shift >= 0) {
        u <<= static_cast<unsigned>(shift);
    } else {
        u >>= static_cast<unsigned>(-shift);
    }
    if (u > static_cast<std::uint64_t>(kBitMax)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(static_cast<double>(u));
}

EvalResult fn_BITRSHIFT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::int64_t n = 0;
    EvalResult e = requireBitNumber(args[0], ctx, n);
    if (e.isError()) {
        return e;
    }
    const EvalResult shRes = evaluateAsNumber(args[1], ctx);
    if (shRes.isError()) {
        return shRes;
    }
    const auto shift = static_cast<std::int64_t>(std::trunc(shRes.getNumber()));
    if (shift < -53 || shift > 53) {
        return EvalResult::Error(CellError::NUM);
    }
    std::uint64_t u = static_cast<std::uint64_t>(n);
    if (shift >= 0) {
        u >>= static_cast<unsigned>(shift);
    } else {
        u <<= static_cast<unsigned>(-shift);
    }
    if (u > static_cast<std::uint64_t>(kBitMax)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(static_cast<double>(u));
}

EvalResult fn_BIN2DEC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fromBaseFn(args, ctx, 2);
}
EvalResult fn_OCT2DEC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fromBaseFn(args, ctx, 8);
}
EvalResult fn_HEX2DEC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fromBaseFn(args, ctx, 16);
}

EvalResult fn_DEC2BIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return toBaseFn(args, ctx, 2);
}
EvalResult fn_DEC2OCT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return toBaseFn(args, ctx, 8);
}
EvalResult fn_DEC2HEX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return toBaseFn(args, ctx, 16);
}

EvalResult fn_BIN2OCT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return convertBaseFn(args, ctx, 2, 8);
}
EvalResult fn_BIN2HEX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return convertBaseFn(args, ctx, 2, 16);
}
EvalResult fn_OCT2BIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return convertBaseFn(args, ctx, 8, 2);
}
EvalResult fn_OCT2HEX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return convertBaseFn(args, ctx, 8, 16);
}
EvalResult fn_HEX2BIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return convertBaseFn(args, ctx, 16, 2);
}
EvalResult fn_HEX2OCT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return convertBaseFn(args, ctx, 16, 8);
}

EvalResult fn_DELTA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n1 = evaluateAsNumber(args[0], ctx);
    if (n1.isError()) {
        return n1;
    }
    double n2 = 0.0;
    if (args.size() == 2) {
        const EvalResult r = evaluateAsNumber(args[1], ctx);
        if (r.isError()) {
            return r;
        }
        n2 = r.getNumber();
    }
    return EvalResult::Number(n1.getNumber() == n2 ? 1.0 : 0.0);
}

EvalResult fn_GESTEP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    double step = 0.0;
    if (args.size() == 2) {
        const EvalResult r = evaluateAsNumber(args[1], ctx);
        if (r.isError()) {
            return r;
        }
        step = r.getNumber();
    }
    return EvalResult::Number(n.getNumber() >= step ? 1.0 : 0.0);
}

EvalResult fn_ERF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult lower = evaluateAsNumber(args[0], ctx);
    if (lower.isError()) {
        return lower;
    }
    if (args.size() == 1) {
        return EvalResult::Number(excelNormalize(std::erf(lower.getNumber())));
    }
    const EvalResult upper = evaluateAsNumber(args[1], ctx);
    if (upper.isError()) {
        return upper;
    }
    return EvalResult::Number(
        excelNormalize(std::erf(upper.getNumber()) - std::erf(lower.getNumber())));
}

EvalResult fn_ERFC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    return EvalResult::Number(excelNormalize(std::erfc(n.getNumber())));
}

EvalResult fn_ERF_PRECISE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    return fn_ERF(args, ctx);
}

EvalResult fn_ERFC_PRECISE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_ERFC(args, ctx);
}

EvalResult fn_COMPLEX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult realRes = evaluateAsNumber(args[0], ctx);
    if (realRes.isError()) {
        return realRes;
    }
    const EvalResult imagRes = evaluateAsNumber(args[1], ctx);
    if (imagRes.isError()) {
        return imagRes;
    }
    std::string suffix = "i";
    if (args.size() == 3) {
        const EvalResult s = evaluateAsString(args[2], ctx);
        if (s.isError()) {
            return s;
        }
        suffix = s.getString();
        if (suffix != "i" && suffix != "j") {
            return EvalResult::Error(CellError::VALUE);
        }
    }
    const double real = realRes.getNumber();
    const double imag = imagRes.getNumber();
    if (real == 0.0 && imag == 0.0) {
        return EvalResult::String("0");
    }
    if (imag == 0.0) {
        return EvalResult::String(formatComplexPart(real));
    }
    std::string out;
    if (real != 0.0) {
        out += formatComplexPart(real);
    }
    if (imag < 0.0) {
        if (imag == -1.0) {
            out += "-";
        } else {
            out += formatComplexPart(imag);
        }
    } else {
        if (real != 0.0) {
            out += "+";
        }
        if (imag != 1.0) {
            out += formatComplexPart(imag);
        }
    }
    out += suffix;
    return EvalResult::String(out);
}

void registerEngineeringFunctions(FunctionRegistry& registry) {
    registry.registerFunction("BITAND", fn_BITAND, "(number1, number2)", "Bitwise AND",
                              "Engineering");
    registry.registerFunction("BITOR", fn_BITOR, "(number1, number2)", "Bitwise OR", "Engineering");
    registry.registerFunction("BITXOR", fn_BITXOR, "(number1, number2)", "Bitwise XOR",
                              "Engineering");
    registry.registerFunction("BITLSHIFT", fn_BITLSHIFT, "(number, shift_amount)",
                              "Bitwise left shift", "Engineering");
    registry.registerFunction("BITRSHIFT", fn_BITRSHIFT, "(number, shift_amount)",
                              "Bitwise right shift", "Engineering");

    registry.registerFunction("BIN2DEC", fn_BIN2DEC, "(number)", "Binary to decimal",
                              "Engineering");
    registry.registerFunction("BIN2HEX", fn_BIN2HEX, "(number, [places])", "Binary to hexadecimal",
                              "Engineering");
    registry.registerFunction("BIN2OCT", fn_BIN2OCT, "(number, [places])", "Binary to octal",
                              "Engineering");
    registry.registerFunction("DEC2BIN", fn_DEC2BIN, "(number, [places])", "Decimal to binary",
                              "Engineering");
    registry.registerFunction("DEC2HEX", fn_DEC2HEX, "(number, [places])", "Decimal to hexadecimal",
                              "Engineering");
    registry.registerFunction("DEC2OCT", fn_DEC2OCT, "(number, [places])", "Decimal to octal",
                              "Engineering");
    registry.registerFunction("HEX2BIN", fn_HEX2BIN, "(number, [places])", "Hexadecimal to binary",
                              "Engineering");
    registry.registerFunction("HEX2DEC", fn_HEX2DEC, "(number)", "Hexadecimal to decimal",
                              "Engineering");
    registry.registerFunction("HEX2OCT", fn_HEX2OCT, "(number, [places])", "Hexadecimal to octal",
                              "Engineering");
    registry.registerFunction("OCT2BIN", fn_OCT2BIN, "(number, [places])", "Octal to binary",
                              "Engineering");
    registry.registerFunction("OCT2DEC", fn_OCT2DEC, "(number)", "Octal to decimal", "Engineering");
    registry.registerFunction("OCT2HEX", fn_OCT2HEX, "(number, [places])", "Octal to hexadecimal",
                              "Engineering");

    registry.registerFunction("DELTA", fn_DELTA, "(number1, [number2])",
                              "Kronecker delta (1 if equal)", "Engineering");
    registry.registerFunction("GESTEP", fn_GESTEP, "(number, [step])",
                              "1 if number >= step, else 0", "Engineering");
    registry.registerFunction("ERF", fn_ERF, "(lower, [upper])", "Error function", "Engineering");
    registry.registerFunction("ERFC", fn_ERFC, "(x)", "Complementary error function",
                              "Engineering");
    registry.registerFunction("ERF.PRECISE", fn_ERF_PRECISE, "(x)", "Error function",
                              "Engineering");
    registry.registerAlias("ERF_PRECISE", "ERF.PRECISE");
    registry.registerFunction("ERFC.PRECISE", fn_ERFC_PRECISE, "(x)",
                              "Complementary error function", "Engineering");
    registry.registerAlias("ERFC_PRECISE", "ERFC.PRECISE");
    registry.registerFunction("COMPLEX", fn_COMPLEX, "(real, imaginary, [suffix])",
                              "Converts real and imaginary coefficients to a complex number",
                              "Engineering");
}

}  // namespace cells
