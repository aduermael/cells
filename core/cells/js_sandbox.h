// =============================================================================
// QuickJS Scripting Sandbox (Office.js Excel add-in host)
// =============================================================================
//
// Parallel to LuauSandbox: compiles and runs user JavaScript in QuickJS, not
// the browser/Node engine. Office.js Excel add-in globals are registered on
// the VM; workbook mutations go through the same CRDT helpers Luau uses.
//
// =============================================================================

#ifndef CELLS_JS_SANDBOX_H_
#define CELLS_JS_SANDBOX_H_

#include <cstdint>

#include <string>

#include "core/cells/luau_sandbox.h"

struct JSRuntime;
struct JSContext;

namespace cells {

struct Workbook;
struct Sheet;

// Sandboxed QuickJS VM with the Office.js Excel add-in surface.
class JsSandbox {
public:
    JsSandbox();
    explicit JsSandbox(const SandboxConfig& config);
    ~JsSandbox();

    JsSandbox(const JsSandbox&) = delete;
    JsSandbox& operator=(const JsSandbox&) = delete;

    void setContext(Workbook* workbook, Sheet* sheet);
    void clearContext();

    // Execute JavaScript (Office.js). Drains promise jobs so async/await and
    // Excel.run complete. Top-level await is enabled.
    [[nodiscard]] ScriptResult execute(const std::string& script);

    [[nodiscard]] const SandboxConfig& config() const { return config_; }
    void setMaxInstructions(int64_t limit);

    [[nodiscard]] Workbook* workbook() const { return workbook_; }
    [[nodiscard]] Sheet* sheet() const { return sheet_; }
    [[nodiscard]] Sheet* activeSheet() const;
    void setActiveSheet(Sheet* sheet);

    void appendOutput(const std::string& text);

    // Unhandled promise rejection captured by the host tracker.
    void setUnhandledRejection(const std::string& message);
    [[nodiscard]] const std::string& unhandledRejection() const { return unhandledRejection_; }

    void bumpInstructions();
    [[nodiscard]] bool interrupted() const { return interrupted_; }
    [[nodiscard]] int64_t instructionCount() const { return instructionCount_; }

private:
    void initState();
    void registerHost();
    [[nodiscard]] bool drainJobs(ScriptResult* result);
    [[nodiscard]] std::string exceptionToString();

    JSRuntime* rt_{nullptr};
    JSContext* ctx_{nullptr};
    SandboxConfig config_;

    Workbook* workbook_{nullptr};
    Sheet* sheet_{nullptr};
    Sheet* activeSheet_{nullptr};

    int64_t instructionCount_{0};
    bool interrupted_{false};
    std::string printBuffer_;
    std::string unhandledRejection_;
};

}  // namespace cells

#endif  // CELLS_JS_SANDBOX_H_
