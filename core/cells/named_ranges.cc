#include "core/cells/named_ranges.h"

#include <cctype>

#include <algorithm>

namespace cells {

bool NamedRangeRegistry::defineWorkbook(const std::string& name, const NamedRangeTarget& target) {
    if (!isValidName(name)) {
        return false;
    }

    // Check if name already exists at workbook scope
    if (_workbookScoped.find(name) != _workbookScoped.end()) {
        return false;
    }

    NamedRange nr;
    nr.name = name;
    nr.scope = NamedRangeScope::WORKBOOK;
    nr.scopeSheetId = ID();  // Null for workbook scope
    nr.target = target;

    _workbookScoped[name] = nr;
    return true;
}

bool NamedRangeRegistry::defineSheet(const std::string& name, const ID& sheetId,
                                     const NamedRangeTarget& target) {
    if (!isValidName(name)) {
        return false;
    }

    if (sheetId.isNull()) {
        return false;
    }

    // Check if name already exists at sheet scope for this sheet
    const std::string key = makeSheetKey(sheetId, name);
    if (_sheetScoped.find(key) != _sheetScoped.end()) {
        return false;
    }

    NamedRange nr;
    nr.name = name;
    nr.scope = NamedRangeScope::SHEET;
    nr.scopeSheetId = sheetId;
    nr.target = target;

    _sheetScoped[key] = nr;
    return true;
}

const NamedRange* NamedRangeRegistry::resolve(const std::string& name,
                                              const ID& currentSheetId) const {
    // First check sheet scope (takes precedence)
    if (!currentSheetId.isNull()) {
        const std::string key = makeSheetKey(currentSheetId, name);
        auto it = _sheetScoped.find(key);
        if (it != _sheetScoped.end()) {
            return &it->second;
        }
    }

    // Fall back to workbook scope
    auto it = _workbookScoped.find(name);
    if (it != _workbookScoped.end()) {
        return &it->second;
    }

    return nullptr;
}

bool NamedRangeRegistry::removeWorkbook(const std::string& name) {
    auto it = _workbookScoped.find(name);
    if (it == _workbookScoped.end()) {
        return false;
    }
    _workbookScoped.erase(it);
    return true;
}

bool NamedRangeRegistry::removeSheet(const std::string& name, const ID& sheetId) {
    if (sheetId.isNull()) {
        return false;
    }

    const std::string key = makeSheetKey(sheetId, name);
    auto it = _sheetScoped.find(key);
    if (it == _sheetScoped.end()) {
        return false;
    }
    _sheetScoped.erase(it);
    return true;
}

void NamedRangeRegistry::removeAllForSheet(const ID& sheetId) {
    if (sheetId.isNull()) {
        return;
    }

    const std::string prefix = sheetId.toString() + ":";

    // Collect keys to remove (can't modify map while iterating)
    std::vector<std::string> keysToRemove;
    for (const auto& [key, _] : _sheetScoped) {
        if (key.compare(0, prefix.size(), prefix) == 0) {
            keysToRemove.push_back(key);
        }
    }

    for (const auto& key : keysToRemove) {
        _sheetScoped.erase(key);
    }
}

std::vector<const NamedRange*> NamedRangeRegistry::getAll() const {
    std::vector<const NamedRange*> result;
    result.reserve(_workbookScoped.size() + _sheetScoped.size());

    for (const auto& [_, nr] : _workbookScoped) {
        result.push_back(&nr);
    }
    for (const auto& [_, nr] : _sheetScoped) {
        result.push_back(&nr);
    }

    return result;
}

std::vector<const NamedRange*> NamedRangeRegistry::getWorkbookScoped() const {
    std::vector<const NamedRange*> result;
    result.reserve(_workbookScoped.size());

    for (const auto& [_, nr] : _workbookScoped) {
        result.push_back(&nr);
    }

    return result;
}

std::vector<const NamedRange*> NamedRangeRegistry::getSheetScoped(const ID& sheetId) const {
    std::vector<const NamedRange*> result;

    if (sheetId.isNull()) {
        return result;
    }

    std::string prefix = sheetId.toString() + ":";

    for (const auto& [key, nr] : _sheetScoped) {
        if (key.compare(0, prefix.size(), prefix) == 0) {
            result.push_back(&nr);
        }
    }

    return result;
}

void NamedRangeRegistry::clear() {
    _workbookScoped.clear();
    _sheetScoped.clear();
}

bool NamedRangeRegistry::isValidName(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    // First character must be letter or underscore
    char first = name[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') {
        return false;
    }

    // Rest must be alphanumeric, underscore, or period
    for (size_t i = 1; i < name.size(); ++i) {
        char c = name[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') {
            return false;
        }
    }

    // Reserved words that look like cell references (A1, B2, etc.) are not valid
    // A cell reference is 1-3 letters (A-ZZZ) followed by only digits
    // We check: if it's ONLY 1-3 letters followed by ONLY digits, reject
    if (name.size() >= 2) {
        size_t letterCount = 0;
        while (letterCount < name.size() &&
               std::isalpha(static_cast<unsigned char>(name[letterCount]))) {
            ++letterCount;
        }
        // Only reject if:
        // - 1-3 letters (typical column names are A-ZZZ, so max 3 letters)
        // - Followed by only digits (row number)
        if (letterCount >= 1 && letterCount <= 3 && letterCount < name.size()) {
            bool allDigits = true;
            for (size_t i = letterCount; i < name.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) {
                // This looks like a cell reference (e.g., A1, AB123, ZZZ999)
                return false;
            }
        }
    }

    return true;
}

std::string NamedRangeRegistry::makeSheetKey(const ID& sheetId, const std::string& name) {
    return sheetId.toString() + ":" + name;
}

}  // namespace cells
