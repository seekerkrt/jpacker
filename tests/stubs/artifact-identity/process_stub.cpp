#include "process_stub.hpp"

#include <stdexcept>
#include <utility>

namespace {

CapturedCommandResult g_result;
std::size_t           g_capture_calls = 0;
std::string           g_last_command;
void (*g_capture_hook)() = nullptr;

} // namespace

namespace artifact_identity_test_stub {

void reset_process_stub() {
    g_result = CapturedCommandResult{};
    g_capture_calls = 0;
    g_last_command.clear();
    g_capture_hook = nullptr;
}

void set_captured_command_result(CapturedCommandResult result) {
    g_result = std::move(result);
}

void set_capture_hook(void (*hook)()) {
    g_capture_hook = hook;
}

std::size_t capture_command_call_count() {
    return g_capture_calls;
}

std::string last_captured_command() {
    return g_last_command;
}

} // namespace artifact_identity_test_stub

CapturedCommandResult capture_command_output_raw(const char* command) {
    ++g_capture_calls;
    g_last_command = command == nullptr ? "" : command;
    if(g_capture_hook != nullptr) g_capture_hook();
    return g_result;
}

int run_command(const std::string&) {
    // Identity testはbuild commandを実行しない。link対象workspace moduleからの予期せぬ呼出しはfail closedにする。
    throw std::runtime_error("Unexpected run_command call in artifact identity test.");
}
