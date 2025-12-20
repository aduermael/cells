// Minimal WASM test - hello world with Embind
#include <emscripten/bind.h>
#include <string>

std::string greet(const std::string& name) {
    return "Hello, " + name + " from WASM!";
}

int add(int a, int b) { return a + b; }

EMSCRIPTEN_BINDINGS(hello) {
    emscripten::function("greet", &greet);
    emscripten::function("add", &add);
}
