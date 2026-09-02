// =============================================================================
// Windows MSVC stub: QuickJS does not compile with cl.exe (GNU computed-goto
// interpreter). Office.js remains available on Linux/macOS CLI and WASM.
// =============================================================================

#include "core/cells/js_sandbox.h"

#include "core/cells/model.h"

namespace cells {

JsSandbox::JsSandbox() : JsSandbox(SandboxConfig{}) {}

JsSandbox::JsSandbox(const SandboxConfig& config) : config_(config) {}

JsSandbox::~JsSandbox() = default;

void JsSandbox::initState() {}

void JsSandbox::registerHost() {}

void JsSandbox::setContext(Workbook* workbook, Sheet* sheet) {
    workbook_ = workbook;
    sheet_ = sheet;
    activeSheet_ = sheet;
    if (activeSheet_ == nullptr && workbook_ != nullptr && !workbook_->sheets.empty()) {
        activeSheet_ = workbook_->sheets[0].get();
    }
}

void JsSandbox::clearContext() {
    workbook_ = nullptr;
    sheet_ = nullptr;
    activeSheet_ = nullptr;
}

Sheet* JsSandbox::activeSheet() const {
    if (activeSheet_ != nullptr) {
        return activeSheet_;
    }
    if (sheet_ != nullptr) {
        return sheet_;
    }
    if (workbook_ != nullptr && !workbook_->sheets.empty()) {
        return workbook_->sheets[0].get();
    }
    return nullptr;
}

void JsSandbox::setActiveSheet(Sheet* sheet) {
    activeSheet_ = sheet;
    if (sheet != nullptr) {
        sheet_ = sheet;
    }
}

void JsSandbox::appendOutput(const std::string& text) {
    printBuffer_ += text;
}

void JsSandbox::setUnhandledRejection(const std::string& message) {
    if (unhandledRejection_.empty()) {
        unhandledRejection_ = message;
    }
}

void JsSandbox::bumpInstructions() {
    instructionCount_++;
    if (instructionCount_ >= config_.maxInstructions) {
        interrupted_ = true;
    }
}

void JsSandbox::setMaxInstructions(int64_t limit) {
    config_.maxInstructions = limit;
}

std::string JsSandbox::exceptionToString() {
    return "Office.js is not available on this platform";
}

bool JsSandbox::drainJobs(ScriptResult* /*result*/) {
    return true;
}

ScriptResult JsSandbox::execute(const std::string& /*script*/) {
    ScriptResult result;
    result.error =
        "Office.js is not supported in the MSVC Windows build (QuickJS requires "
        "Clang/GCC). Use Luau, or run cells --script on Linux or macOS.";
    return result;
}

}  // namespace cells
