// =============================================================================
// QuickJS sandbox: VM lifecycle, instruction limits, promise job pump.
// Office.js host bindings live in officejs_api.cc.
// =============================================================================

#include "core/cells/js_sandbox.h"

#include <cstring>

#include "core/cells/model.h"
#include "core/cells/officejs_api.h"

#include "quickjs.h"  // NOLINT(build/include_subdir)

namespace cells {

// QuickJS predicates return int; JSValue handles are freed by value.
// NOLINTBEGIN(readability-implicit-bool-conversion,misc-const-correctness)

namespace {

int interruptHandler(JSRuntime* /*rt*/, void* opaque) {
    auto* sandbox = static_cast<JsSandbox*>(opaque);
    if (sandbox == nullptr) {
        return 0;
    }
    sandbox->bumpInstructions();
    return sandbox->interrupted() ? 1 : 0;
}

void rejectionTracker(JSContext* ctx, JSValueConst /*promise*/, JSValueConst reason,
                      JS_BOOL isHandled, void* opaque) {
    if (isHandled != 0) {
        return;
    }
    auto* sandbox = static_cast<JsSandbox*>(opaque);
    if (sandbox == nullptr) {
        return;
    }
    const char* msg = JS_ToCString(ctx, reason);
    sandbox->setUnhandledRejection(msg != nullptr ? msg : "Unhandled promise rejection");
    JS_FreeCString(ctx, msg);
}

}  // namespace

JsSandbox::JsSandbox() : JsSandbox(SandboxConfig{}) {}

JsSandbox::JsSandbox(const SandboxConfig& config) : config_(config) {
    initState();
}

JsSandbox::~JsSandbox() {
    if (ctx_ != nullptr) {
        JS_FreeContext(ctx_);
        ctx_ = nullptr;
    }
    if (rt_ != nullptr) {
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
    }
}

void JsSandbox::initState() {
    rt_ = JS_NewRuntime();
    if (rt_ == nullptr) {
        return;
    }
#ifdef __EMSCRIPTEN__
    // Browser workers have a tiny WASM call stack. Native/CLI does not.
    JS_SetMaxStackSize(rt_, 512 * 1024);
#endif
    JS_SetInterruptHandler(rt_, interruptHandler, this);
    JS_SetHostPromiseRejectionTracker(rt_, rejectionTracker, this);

    ctx_ = JS_NewContext(rt_);
    if (ctx_ == nullptr) {
        return;
    }
    JS_SetContextOpaque(ctx_, this);
#ifdef __EMSCRIPTEN__
    JS_UpdateStackTop(rt_);
#endif
    registerHost();
}

void JsSandbox::registerHost() {
    registerOfficeJsHost(ctx_, this);
}

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

std::string jsErrorToString(JSContext* ctx, JSValueConst val) {
    std::string name;
    std::string code;
    std::string message;
    auto readProp = [&](const char* key, std::string* out) {
        const JSValue v = JS_GetPropertyStr(ctx, val, key);
        if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
            const char* s = JS_ToCString(ctx, v);
            if (s != nullptr) {
                *out = s;
            }
            JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, v);
    };
    if (JS_IsObject(val)) {
        readProp("name", &name);
        readProp("code", &code);
        readProp("message", &message);
    }
    if (message.empty()) {
        const char* s = JS_ToCString(ctx, val);
        message = s != nullptr ? s : "JavaScript exception";
        JS_FreeCString(ctx, s);
    }
    std::string out;
    if (!name.empty()) {
        out += name;
    }
    if (!code.empty()) {
        if (!out.empty()) {
            out += " ";
        }
        out += "[";
        out += code;
        out += "]";
    }
    if (!message.empty()) {
        if (!out.empty()) {
            out += ": ";
        }
        out += message;
    }
    if (JS_IsObject(val)) {
        const JSValue stack = JS_GetPropertyStr(ctx, val, "stack");
        if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) {
            const char* st = JS_ToCString(ctx, stack);
            if (st != nullptr && st[0] != '\0') {
                out += "\n";
                out += st;
            }
            JS_FreeCString(ctx, st);
        }
        JS_FreeValue(ctx, stack);
    }
    if (out.empty()) {
        return "JavaScript exception";
    }
    return out;
}

std::string JsSandbox::exceptionToString() {
    if (ctx_ == nullptr) {
        return "no JS context";
    }
    const JSValue ex = JS_GetException(ctx_);
    std::string out = jsErrorToString(ctx_, ex);
    JS_FreeValue(ctx_, ex);
    return out;
}

bool JsSandbox::drainJobs(ScriptResult* result) {
    constexpr int kMaxJobs = 100000;
    int jobs = 0;
    while (JS_IsJobPending(rt_) != 0) {
        if (jobs++ >= kMaxJobs) {
            result->error = "Script exceeded promise job limit";
            return false;
        }
        if (interrupted_) {
            result->error = "Script exceeded instruction limit";
            return false;
        }
        JSContext* jobCtx = nullptr;
        const int err = JS_ExecutePendingJob(rt_, &jobCtx);
        if (err < 0) {
            JSContext* errCtx = jobCtx != nullptr ? jobCtx : ctx_;
            const JSValue ex = JS_GetException(errCtx);
            result->error = jsErrorToString(errCtx, ex);
            result->output = printBuffer_;
            JS_FreeValue(errCtx, ex);
            return false;
        }
    }
    return true;
}

ScriptResult JsSandbox::execute(const std::string& script) {
    ScriptResult result;
    if (ctx_ == nullptr || rt_ == nullptr) {
        result.error = "QuickJS state not initialized";
        return result;
    }

    instructionCount_ = 0;
    interrupted_ = false;
    printBuffer_.clear();
    unhandledRejection_.clear();
#ifdef __EMSCRIPTEN__
    JS_UpdateStackTop(rt_);
#endif
    JSValue val = JS_Eval(ctx_, script.c_str(), script.size(), "<script>",
                          JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC);
    if (JS_IsException(val)) {
        result.error = exceptionToString();
        result.output = printBuffer_;
        result.instructions = instructionCount_;
        return result;
    }

    if (!drainJobs(&result)) {
        JS_FreeValue(ctx_, val);
        result.output = printBuffer_;
        result.instructions = instructionCount_;
        return result;
    }

    // Office.initialize is the legacy ready callback; invoke if the script set it.
    const JSValue global = JS_GetGlobalObject(ctx_);
    const JSValue office = JS_GetPropertyStr(ctx_, global, "Office");
    if (JS_IsObject(office)) {
        const JSValue init = JS_GetPropertyStr(ctx_, office, "initialize");
        if (JS_IsFunction(ctx_, init)) {
            JSValue reason = JS_NewString(ctx_, "initialization");
            JSValue argv[1] = {reason};
            JSValue initRet = JS_Call(ctx_, init, office, 1, argv);
            JS_FreeValue(ctx_, reason);
            if (JS_IsException(initRet)) {
                result.error = exceptionToString();
                JS_FreeValue(ctx_, init);
                JS_FreeValue(ctx_, office);
                JS_FreeValue(ctx_, global);
                JS_FreeValue(ctx_, val);
                result.output = printBuffer_;
                result.instructions = instructionCount_;
                return result;
            }
            JS_FreeValue(ctx_, initRet);
            if (!drainJobs(&result)) {
                JS_FreeValue(ctx_, init);
                JS_FreeValue(ctx_, office);
                JS_FreeValue(ctx_, global);
                JS_FreeValue(ctx_, val);
                result.output = printBuffer_;
                result.instructions = instructionCount_;
                return result;
            }
        }
        JS_FreeValue(ctx_, init);
    }
    JS_FreeValue(ctx_, office);
    JS_FreeValue(ctx_, global);

    if (!unhandledRejection_.empty()) {
        result.error = unhandledRejection_;
        result.output = printBuffer_;
        JS_FreeValue(ctx_, val);
        result.instructions = instructionCount_;
        return result;
    }

    if (JS_IsObject(val)) {
        const JSPromiseStateEnum st = JS_PromiseState(ctx_, val);
        if (st == JS_PROMISE_REJECTED) {
            const JSValue reason = JS_PromiseResult(ctx_, val);
            result.error = jsErrorToString(ctx_, reason);
            result.output = printBuffer_;
            JS_FreeValue(ctx_, reason);
            JS_FreeValue(ctx_, val);
            result.instructions = instructionCount_;
            return result;
        }
        if (st == JS_PROMISE_PENDING) {
            result.error = "Script did not complete (pending promise)";
            result.output = printBuffer_;
            JS_FreeValue(ctx_, val);
            result.instructions = instructionCount_;
            return result;
        }
    }

    if (interrupted_) {
        result.error = "Script exceeded instruction limit";
        result.output = printBuffer_;
        JS_FreeValue(ctx_, val);
        result.instructions = instructionCount_;
        return result;
    }

    result.success = true;
    result.output = printBuffer_;
    result.instructions = instructionCount_;
    JS_FreeValue(ctx_, val);
    return result;
}

// NOLINTEND(readability-implicit-bool-conversion,misc-const-correctness)

}  // namespace cells
