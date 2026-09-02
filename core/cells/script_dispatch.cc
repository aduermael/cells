#include "core/cells/script_dispatch.h"

#include <cctype>
#include <cstring>

#include "core/cells/js_sandbox.h"
#include "core/cells/model.h"

namespace cells {
namespace {

bool endsWithIgnoreCase(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        const char a =
            static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - n + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

}  // namespace

ScriptKind detectScriptKind(const std::string& path, const std::string& source) {
    if (!path.empty()) {
        if (endsWithIgnoreCase(path, ".js") || endsWithIgnoreCase(path, ".mjs")) {
            return ScriptKind::JavaScript;
        }
        if (endsWithIgnoreCase(path, ".luau") || endsWithIgnoreCase(path, ".lua")) {
            return ScriptKind::Luau;
        }
    }
    if (source.find("Excel.run") != std::string::npos ||
        source.find("Office.onReady") != std::string::npos ||
        source.find("Office.initialize") != std::string::npos ||
        source.find("OfficeExtension") != std::string::npos) {
        return ScriptKind::JavaScript;
    }
    return ScriptKind::Luau;
}

ScriptKind scriptKindFromLanguage(const std::string& language) {
    std::string lower;
    lower.reserve(language.size());
    for (const char c : language) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "javascript" || lower == "js" || lower == "officejs") {
        return ScriptKind::JavaScript;
    }
    return ScriptKind::Luau;
}

ScriptKind resolveScriptKind(const std::string& language, const std::string& source) {
    if (scriptKindFromLanguage(language) == ScriptKind::JavaScript) {
        return ScriptKind::JavaScript;
    }
    if (!language.empty()) {
        return ScriptKind::Luau;
    }
    return detectScriptKind("", source);
}

ScriptResult executeUserScript(Workbook& workbook, Sheet* sheet, const std::string& source,
                               ScriptKind kind) {
    Sheet* ctxSheet = sheet;
    if (ctxSheet == nullptr && !workbook.sheets.empty()) {
        ctxSheet = workbook.sheets[0].get();
    }
    if (kind == ScriptKind::JavaScript) {
        JsSandbox sandbox;
        sandbox.setContext(&workbook, ctxSheet);
        return sandbox.execute(source);
    }
    LuauSandbox sandbox;
    sandbox.setContext(&workbook, ctxSheet);
    return sandbox.execute(source);
}

ScriptResult executeUserScript(Workbook& workbook, Sheet* sheet, const std::string& source,
                               const std::string& language) {
    return executeUserScript(workbook, sheet, source, resolveScriptKind(language, source));
}

}  // namespace cells
