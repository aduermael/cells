#include "core/cells/functions/fn_financial.h"

#include <cmath>

#include <utility>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {
namespace {

EvalResult requireNumber(const ASTNode* arg, EvalContext& ctx, double& out) {
    const EvalResult r = evaluateAsNumber(arg, ctx);
    if (r.isError()) {
        return r;
    }
    out = r.getNumber();
    return EvalResult::Empty();
}

EvalResult optionalNumber(const std::vector<const ASTNode*>& args, size_t index, EvalContext& ctx,
                          double fallback, double& out) {
    if (args.size() <= index) {
        out = fallback;
        return EvalResult::Empty();
    }
    return requireNumber(args[index], ctx, out);
}

int paymentType(double type) {
    return static_cast<int>(std::trunc(type));
}

bool validPaymentType(int type) {
    return type == 0 || type == 1;
}

double pow1p(double rate, double nper) {
    if (rate == 0.0) {
        return 1.0;
    }
    return std::pow(1.0 + rate, nper);
}

// pv*(1+r)^n + pmt*(1+r*type)*((1+r)^n-1)/r + fv = 0
EvalResult annuityPv(double rate, double nper, double pmt, double fv, int type) {
    if (rate == 0.0) {
        return EvalResult::Number(excelNormalize(-(fv + pmt * nper)));
    }
    const double p = pow1p(rate, nper);
    if (!std::isfinite(p) || p == 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double annuity = pmt * (1.0 + rate * static_cast<double>(type)) * (p - 1.0) / rate;
    return EvalResult::Number(excelNormalize(-(fv + annuity) / p));
}

EvalResult annuityFv(double rate, double nper, double pmt, double pv, int type) {
    if (rate == 0.0) {
        return EvalResult::Number(excelNormalize(-(pv + pmt * nper)));
    }
    const double p = pow1p(rate, nper);
    if (!std::isfinite(p)) {
        return EvalResult::Error(CellError::NUM);
    }
    const double annuity = pmt * (1.0 + rate * static_cast<double>(type)) * (p - 1.0) / rate;
    return EvalResult::Number(excelNormalize(-(pv * p + annuity)));
}

EvalResult annuityPmt(double rate, double nper, double pv, double fv, int type) {
    if (nper == 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    if (rate == 0.0) {
        return EvalResult::Number(excelNormalize(-(pv + fv) / nper));
    }
    const double p = pow1p(rate, nper);
    if (!std::isfinite(p)) {
        return EvalResult::Error(CellError::NUM);
    }
    const double denom = (1.0 + rate * static_cast<double>(type)) * (p - 1.0) / rate;
    if (denom == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(-(fv + pv * p) / denom));
}

EvalResult annuityNper(double rate, double pmt, double pv, double fv, int type) {
    if (rate == 0.0) {
        if (pmt == 0.0) {
            return EvalResult::Error(CellError::DIV);
        }
        return EvalResult::Number(excelNormalize(-(pv + fv) / pmt));
    }
    if (rate <= -1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double rtype = 1.0 + rate * static_cast<double>(type);
    const double term = pmt * rtype / rate;
    const double num = term - fv;
    const double den = pv + term;
    if (num == 0.0 || den == 0.0 || (num / den) <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double nper = std::log(num / den) / std::log(1.0 + rate);
    if (!std::isfinite(nper)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(nper));
}

EvalResult parseAnnuityType(const std::vector<const ASTNode*>& args, size_t index, EvalContext& ctx,
                            int& type) {
    double typeVal = 0.0;
    const EvalResult e = optionalNumber(args, index, ctx, 0.0, typeVal);
    if (e.isError()) {
        return e;
    }
    type = paymentType(typeVal);
    if (!validPaymentType(type)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Empty();
}

int dollarFractionPlaces(double fraction) {
    if (fraction <= 0.0) {
        return -1;
    }
    const double log10f = std::log10(fraction);
    int n = static_cast<int>(std::ceil(log10f));
    if (n < 0) {
        n = 0;
    }
    return n;
}

double roundTo3(double x) {
    if (x >= 0.0) {
        return std::floor(x * 1000.0 + 0.5) / 1000.0;
    }
    return std::ceil(x * 1000.0 - 0.5) / 1000.0;
}

EvalResult ipmtAt(double rate, double per, double nper, double pv, double fv, int type) {
    if (per < 1.0 || per >= nper + 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const EvalResult pmtRes = annuityPmt(rate, nper, pv, fv, type);
    if (pmtRes.isError()) {
        return pmtRes;
    }
    const double pmt = pmtRes.getNumber();
    const EvalResult fvRes = annuityFv(rate, per - 1.0, pmt, pv, type);
    if (fvRes.isError()) {
        return fvRes;
    }
    double ipmt = fvRes.getNumber() * rate;
    if (type == 1) {
        ipmt /= (1.0 + rate);
        if (per == 1.0) {
            ipmt = 0.0;
        }
    }
    return EvalResult::Number(excelNormalize(ipmt));
}

EvalResult tBillDsm(double settlement, double maturity, double& dsm) {
    dsm = maturity - settlement;
    if (dsm <= 0.0 || dsm > 365.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Empty();
}

}  // namespace

EvalResult fn_SLN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    double cost = 0.0;
    double salvage = 0.0;
    double life = 0.0;
    EvalResult e = requireNumber(args[0], ctx, cost);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, salvage);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, life);
    if (e.isError()) {
        return e;
    }
    if (life == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize((cost - salvage) / life));
}

EvalResult fn_SYD(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    double cost = 0.0;
    double salvage = 0.0;
    double life = 0.0;
    double per = 0.0;
    EvalResult e = requireNumber(args[0], ctx, cost);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, salvage);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, life);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, per);
    if (e.isError()) {
        return e;
    }
    if (life <= 0.0 || per <= 0.0 || per > life) {
        return EvalResult::Error(CellError::NUM);
    }
    const double denom = life * (life + 1.0);
    if (denom == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize((cost - salvage) * (life - per + 1.0) * 2.0 / denom));
}

EvalResult fn_PV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3 || args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double nper = 0.0;
    double pmt = 0.0;
    double fv = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, pmt);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 3, ctx, 0.0, fv);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 4, ctx, type);
    if (e.isError()) {
        return e;
    }
    return annuityPv(rate, nper, pmt, fv, type);
}

EvalResult fn_FV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3 || args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double nper = 0.0;
    double pmt = 0.0;
    double pv = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, pmt);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 3, ctx, 0.0, pv);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 4, ctx, type);
    if (e.isError()) {
        return e;
    }
    return annuityFv(rate, nper, pmt, pv, type);
}

EvalResult fn_PMT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3 || args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double nper = 0.0;
    double pv = 0.0;
    double fv = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 3, ctx, 0.0, fv);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 4, ctx, type);
    if (e.isError()) {
        return e;
    }
    return annuityPmt(rate, nper, pv, fv, type);
}

EvalResult fn_NPER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3 || args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double pmt = 0.0;
    double pv = 0.0;
    double fv = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, pmt);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 3, ctx, 0.0, fv);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 4, ctx, type);
    if (e.isError()) {
        return e;
    }
    return annuityNper(rate, pmt, pv, fv, type);
}

EvalResult fn_NPV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    const EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    std::vector<const ASTNode*> valueArgs(args.begin() + 1, args.end());
    auto [values, err] = collectNumericValues(valueArgs, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Number(0.0);
    }
    if (rate == -1.0) {
        return EvalResult::Error(CellError::DIV);
    }
    double npv = 0.0;
    for (size_t i = 0; i < values.size(); ++i) {
        const double denom = pow1p(rate, static_cast<double>(i + 1));
        if (denom == 0.0 || !std::isfinite(denom)) {
            return EvalResult::Error(CellError::DIV);
        }
        npv += values[i] / denom;
    }
    if (!std::isfinite(npv)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(npv));
}

EvalResult fn_EFFECT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    double nominal = 0.0;
    double npery = 0.0;
    EvalResult e = requireNumber(args[0], ctx, nominal);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, npery);
    if (e.isError()) {
        return e;
    }
    npery = std::trunc(npery);
    if (nominal <= 0.0 || npery < 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = std::pow(1.0 + nominal / npery, npery) - 1.0;
    if (!std::isfinite(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_NOMINAL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    double effect = 0.0;
    double npery = 0.0;
    EvalResult e = requireNumber(args[0], ctx, effect);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, npery);
    if (e.isError()) {
        return e;
    }
    npery = std::trunc(npery);
    if (effect <= 0.0 || npery < 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = npery * (std::pow(1.0 + effect, 1.0 / npery) - 1.0);
    if (!std::isfinite(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_DOLLARDE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    double dollar = 0.0;
    double fraction = 0.0;
    EvalResult e = requireNumber(args[0], ctx, dollar);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, fraction);
    if (e.isError()) {
        return e;
    }
    fraction = std::trunc(fraction);
    const int places = dollarFractionPlaces(fraction);
    if (places < 0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double whole = std::trunc(dollar);
    const double frac = dollar - whole;
    const double scale = std::pow(10.0, static_cast<double>(places));
    return EvalResult::Number(excelNormalize(whole + frac * scale / fraction));
}

EvalResult fn_DOLLARFR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    double dollar = 0.0;
    double fraction = 0.0;
    EvalResult e = requireNumber(args[0], ctx, dollar);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, fraction);
    if (e.isError()) {
        return e;
    }
    fraction = std::trunc(fraction);
    const int places = dollarFractionPlaces(fraction);
    if (places < 0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double whole = std::trunc(dollar);
    const double frac = dollar - whole;
    const double scale = std::pow(10.0, static_cast<double>(places));
    return EvalResult::Number(excelNormalize(whole + frac * fraction / scale));
}

EvalResult fn_FVSCHEDULE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    double principal = 0.0;
    const EvalResult e = requireNumber(args[0], ctx, principal);
    if (e.isError()) {
        return e;
    }
    auto [rates, err] = collectNumericValues({args[1]}, ctx);
    if (err.isError()) {
        return err;
    }
    double result = principal;
    for (const double r : rates) {
        result *= (1.0 + r);
        if (!std::isfinite(result)) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_PDURATION(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double pv = 0.0;
    double fv = 0.0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, fv);
    if (e.isError()) {
        return e;
    }
    if (rate <= 0.0 || pv <= 0.0 || fv <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = std::log(fv / pv) / std::log(1.0 + rate);
    if (!std::isfinite(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_RRI(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    double nper = 0.0;
    double pv = 0.0;
    double fv = 0.0;
    EvalResult e = requireNumber(args[0], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, fv);
    if (e.isError()) {
        return e;
    }
    if (nper <= 0.0 || pv == 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double ratio = fv / pv;
    if (ratio < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = std::pow(ratio, 1.0 / nper) - 1.0;
    if (!std::isfinite(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_ISPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double per = 0.0;
    double nper = 0.0;
    double pv = 0.0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, per);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, pv);
    if (e.isError()) {
        return e;
    }
    if (nper == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(pv * rate * (per / nper - 1.0)));
}

EvalResult fn_DDB(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 4 || args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    double cost = 0.0;
    double salvage = 0.0;
    double life = 0.0;
    double period = 0.0;
    double factor = 2.0;
    EvalResult e = requireNumber(args[0], ctx, cost);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, salvage);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, life);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, period);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 4, ctx, 2.0, factor);
    if (e.isError()) {
        return e;
    }
    if (cost < 0.0 || salvage < 0.0 || life <= 0.0 || period <= 0.0 || period > life ||
        factor <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    double remaining = cost;
    double dep = 0.0;
    const int last = static_cast<int>(std::ceil(period));
    for (int i = 1; i <= last; ++i) {
        dep = remaining * factor / life;
        const double cap = remaining - salvage;
        if (dep > cap) {
            dep = cap < 0.0 ? 0.0 : cap;
        }
        if (i == last && period != static_cast<double>(last)) {
            dep *= (period - static_cast<double>(last - 1));
        }
        remaining -= dep;
    }
    return EvalResult::Number(excelNormalize(dep));
}

EvalResult fn_DB(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 4 || args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    double cost = 0.0;
    double salvage = 0.0;
    double life = 0.0;
    double period = 0.0;
    double month = 12.0;
    EvalResult e = requireNumber(args[0], ctx, cost);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, salvage);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, life);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, period);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 4, ctx, 12.0, month);
    if (e.isError()) {
        return e;
    }
    month = std::trunc(month);
    period = std::trunc(period);
    if (cost < 0.0 || salvage < 0.0 || life <= 0.0 || period < 1.0 || month < 1.0 || month > 12.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double maxPeriod = (month == 12.0) ? life : life + 1.0;
    if (period > maxPeriod) {
        return EvalResult::Error(CellError::NUM);
    }
    if (cost == 0.0) {
        return EvalResult::Number(0.0);
    }
    const double rate = roundTo3(1.0 - std::pow(salvage / cost, 1.0 / life));
    if (!std::isfinite(rate) || rate < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    double total = 0.0;
    double dep = 0.0;
    for (int i = 1; i <= static_cast<int>(period); ++i) {
        if (i == 1) {
            dep = cost * rate * month / 12.0;
        } else if (month != 12.0 && i == static_cast<int>(life) + 1) {
            dep = (cost - total) * rate * (12.0 - month) / 12.0;
        } else {
            dep = (cost - total) * rate;
        }
        total += dep;
    }
    return EvalResult::Number(excelNormalize(dep));
}

EvalResult fn_IPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 4 || args.size() > 6) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double per = 0.0;
    double nper = 0.0;
    double pv = 0.0;
    double fv = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, per);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 4, ctx, 0.0, fv);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 5, ctx, type);
    if (e.isError()) {
        return e;
    }
    return ipmtAt(rate, per, nper, pv, fv, type);
}

EvalResult fn_PPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 4 || args.size() > 6) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double per = 0.0;
    double nper = 0.0;
    double pv = 0.0;
    double fv = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, per);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = optionalNumber(args, 4, ctx, 0.0, fv);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 5, ctx, type);
    if (e.isError()) {
        return e;
    }
    const EvalResult pmtRes = annuityPmt(rate, nper, pv, fv, type);
    if (pmtRes.isError()) {
        return pmtRes;
    }
    const EvalResult ipmtRes = ipmtAt(rate, per, nper, pv, fv, type);
    if (ipmtRes.isError()) {
        return ipmtRes;
    }
    return EvalResult::Number(excelNormalize(pmtRes.getNumber() - ipmtRes.getNumber()));
}

EvalResult fn_CUMIPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 6) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double nper = 0.0;
    double pv = 0.0;
    double start = 0.0;
    double end = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, start);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[4], ctx, end);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 5, ctx, type);
    if (e.isError()) {
        return e;
    }
    start = std::trunc(start);
    end = std::trunc(end);
    if (rate <= 0.0 || nper <= 0.0 || pv <= 0.0 || start < 1.0 || end < start || end > nper) {
        return EvalResult::Error(CellError::NUM);
    }
    double sum = 0.0;
    for (int p = static_cast<int>(start); p <= static_cast<int>(end); ++p) {
        const EvalResult r = ipmtAt(rate, static_cast<double>(p), nper, pv, 0.0, type);
        if (r.isError()) {
            return r;
        }
        sum += r.getNumber();
    }
    return EvalResult::Number(excelNormalize(sum));
}

EvalResult fn_CUMPRINC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 6) {
        return EvalResult::Error(CellError::VALUE);
    }
    double rate = 0.0;
    double nper = 0.0;
    double pv = 0.0;
    double start = 0.0;
    double end = 0.0;
    int type = 0;
    EvalResult e = requireNumber(args[0], ctx, rate);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, nper);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, pv);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[3], ctx, start);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[4], ctx, end);
    if (e.isError()) {
        return e;
    }
    e = parseAnnuityType(args, 5, ctx, type);
    if (e.isError()) {
        return e;
    }
    start = std::trunc(start);
    end = std::trunc(end);
    if (rate <= 0.0 || nper <= 0.0 || pv <= 0.0 || start < 1.0 || end < start || end > nper) {
        return EvalResult::Error(CellError::NUM);
    }
    const EvalResult pmtRes = annuityPmt(rate, nper, pv, 0.0, type);
    if (pmtRes.isError()) {
        return pmtRes;
    }
    const double pmt = pmtRes.getNumber();
    double sum = 0.0;
    for (int p = static_cast<int>(start); p <= static_cast<int>(end); ++p) {
        const EvalResult ip = ipmtAt(rate, static_cast<double>(p), nper, pv, 0.0, type);
        if (ip.isError()) {
            return ip;
        }
        sum += pmt - ip.getNumber();
    }
    return EvalResult::Number(excelNormalize(sum));
}

EvalResult fn_TBILLPRICE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    double settlement = 0.0;
    double maturity = 0.0;
    double discount = 0.0;
    EvalResult e = requireNumber(args[0], ctx, settlement);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, maturity);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, discount);
    if (e.isError()) {
        return e;
    }
    double dsm = 0.0;
    e = tBillDsm(settlement, maturity, dsm);
    if (e.isError()) {
        return e;
    }
    if (discount <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double price = 100.0 * (1.0 - discount * dsm / 360.0);
    if (price <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(price));
}

EvalResult fn_TBILLYIELD(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    double settlement = 0.0;
    double maturity = 0.0;
    double pr = 0.0;
    EvalResult e = requireNumber(args[0], ctx, settlement);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, maturity);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, pr);
    if (e.isError()) {
        return e;
    }
    double dsm = 0.0;
    e = tBillDsm(settlement, maturity, dsm);
    if (e.isError()) {
        return e;
    }
    if (pr <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize((100.0 - pr) / pr * 360.0 / dsm));
}

EvalResult fn_TBILLEQ(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    double settlement = 0.0;
    double maturity = 0.0;
    double discount = 0.0;
    EvalResult e = requireNumber(args[0], ctx, settlement);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[1], ctx, maturity);
    if (e.isError()) {
        return e;
    }
    e = requireNumber(args[2], ctx, discount);
    if (e.isError()) {
        return e;
    }
    double dsm = 0.0;
    e = tBillDsm(settlement, maturity, dsm);
    if (e.isError()) {
        return e;
    }
    if (discount <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double denom = 360.0 - discount * dsm;
    if (denom == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    if (dsm <= 182.0) {
        return EvalResult::Number(excelNormalize(365.0 * discount / denom));
    }
    const double price = 100.0 * (1.0 - discount * dsm / 360.0);
    if (price <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double m = dsm / 365.0;
    const double discAmt = (100.0 - price) / price;
    const double inner = m * m - 2.0 * (m - 1.0) * discAmt;
    if (inner < 0.0 || m == 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize((-m + std::sqrt(inner)) / (m - 1.0)));
}

void registerFinancialFunctions(FunctionRegistry& registry) {
    registry.registerFunction("SLN", fn_SLN, "(cost, salvage, life)",
                              "Straight-line depreciation for one period", "Financial");
    registry.registerFunction("SYD", fn_SYD, "(cost, salvage, life, per)",
                              "Sum-of-years' digits depreciation", "Financial");
    registry.registerFunction("PV", fn_PV, "(rate, nper, pmt, [fv], [type])",
                              "Present value of an annuity", "Financial");
    registry.registerFunction("FV", fn_FV, "(rate, nper, pmt, [pv], [type])",
                              "Future value of an annuity", "Financial");
    registry.registerFunction("PMT", fn_PMT, "(rate, nper, pv, [fv], [type])",
                              "Payment for an annuity", "Financial");
    registry.registerFunction("NPER", fn_NPER, "(rate, pmt, pv, [fv], [type])",
                              "Number of periods for an annuity", "Financial");
    registry.registerFunction("NPV", fn_NPV, "(rate, value1, [value2], ...)",
                              "Net present value of cash flows", "Financial");
    registry.registerFunction("EFFECT", fn_EFFECT, "(nominal_rate, npery)",
                              "Effective annual interest rate", "Financial");
    registry.registerFunction("NOMINAL", fn_NOMINAL, "(effect_rate, npery)",
                              "Nominal annual interest rate", "Financial");
    registry.registerFunction("DOLLARDE", fn_DOLLARDE, "(fractional_dollar, fraction)",
                              "Converts a fractional dollar price to decimal", "Financial");
    registry.registerFunction("DOLLARFR", fn_DOLLARFR, "(decimal_dollar, fraction)",
                              "Converts a decimal dollar price to fractional", "Financial");
    registry.registerFunction("FVSCHEDULE", fn_FVSCHEDULE, "(principal, schedule)",
                              "Future value after a schedule of rates", "Financial");
    registry.registerFunction("PDURATION", fn_PDURATION, "(rate, pv, fv)",
                              "Periods to reach a future value", "Financial");
    registry.registerFunction("RRI", fn_RRI, "(nper, pv, fv)",
                              "Equivalent interest rate for growth", "Financial");
    registry.registerFunction("ISPMT", fn_ISPMT, "(rate, per, nper, pv)",
                              "Interest payment for even principal", "Financial");
    registry.registerFunction("DDB", fn_DDB, "(cost, salvage, life, period, [factor])",
                              "Double-declining balance depreciation", "Financial");
    registry.registerFunction("DB", fn_DB, "(cost, salvage, life, period, [month])",
                              "Fixed-declining balance depreciation", "Financial");
    registry.registerFunction("IPMT", fn_IPMT, "(rate, per, nper, pv, [fv], [type])",
                              "Interest portion of an annuity payment", "Financial");
    registry.registerFunction("PPMT", fn_PPMT, "(rate, per, nper, pv, [fv], [type])",
                              "Principal portion of an annuity payment", "Financial");
    registry.registerFunction("CUMIPMT", fn_CUMIPMT,
                              "(rate, nper, pv, start_period, end_period, type)",
                              "Cumulative interest between two periods", "Financial");
    registry.registerFunction("CUMPRINC", fn_CUMPRINC,
                              "(rate, nper, pv, start_period, end_period, type)",
                              "Cumulative principal between two periods", "Financial");
    registry.registerFunction("TBILLEQ", fn_TBILLEQ, "(settlement, maturity, discount)",
                              "Bond-equivalent yield of a Treasury bill", "Financial");
    registry.registerFunction("TBILLPRICE", fn_TBILLPRICE, "(settlement, maturity, discount)",
                              "Price of a Treasury bill per $100", "Financial");
    registry.registerFunction("TBILLYIELD", fn_TBILLYIELD, "(settlement, maturity, pr)",
                              "Yield of a Treasury bill", "Financial");
}

}  // namespace cells
