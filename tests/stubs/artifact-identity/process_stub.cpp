#include "process_stub.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

CapturedCommandResult g_result;
std::vector<artifact_identity_test_stub::CapturedCommandStep> g_steps;
std::size_t g_capture_calls = 0;
std::vector<std::string> g_commands;
void (*g_capture_hook)() = nullptr;
bool g_uses_ordered_steps = false;

} // namespace

namespace artifact_identity_test_stub {

void reset_process_stub() {
    g_result = CapturedCommandResult{};
    g_steps.clear();
    g_capture_calls = 0;
    g_commands.clear();
    g_capture_hook = nullptr;
    g_uses_ordered_steps = false;
}

void set_captured_command_result(CapturedCommandResult result) {
    g_result = std::move(result);
    g_steps.clear();
    g_uses_ordered_steps = false;
}

void set_capture_hook(void (*hook)()) {
    g_capture_hook = hook;
}

void set_captured_command_results(
    std::vector<CapturedCommandResult> results) {
    std::vector<CapturedCommandStep> steps;
    steps.reserve(results.size());
    for(CapturedCommandResult& result : results) {
        steps.push_back(CapturedCommandStep{std::move(result)});
    }
    set_captured_command_steps(std::move(steps));
}

void set_captured_command_steps(
    std::vector<CapturedCommandStep> steps) {
    g_steps = std::move(steps);
    g_uses_ordered_steps = true;
}

std::size_t capture_command_call_count() {
    return g_capture_calls;
}

std::string last_captured_command() {
    return g_commands.empty() ? "" : g_commands.back();
}

std::vector<std::string> captured_commands() {
    return g_commands;
}

} // namespace artifact_identity_test_stub

CapturedCommandResult capture_command_output_raw(const char* command) {
    const std::size_t call_index = g_capture_calls;
    ++g_capture_calls;
    g_commands.push_back(command == nullptr ? "" : command);

    if(!g_uses_ordered_steps) {
        if(g_capture_hook != nullptr) g_capture_hook();
        return g_result;
    }
    if(call_index >= g_steps.size()) {
        throw std::runtime_error(
            "Unexpected extra capture_command_output_raw call in artifact identity test.");
    }

    const artifact_identity_test_stub::CapturedCommandStep& step =
        g_steps[call_index];
    if(step.before_hook != nullptr) step.before_hook();
    CapturedCommandResult result = step.result;
    if(step.after_hook != nullptr) step.after_hook();
    if(g_capture_hook != nullptr) g_capture_hook();
    return result;
}

int run_command(const std::string&) {
    // Identity testはbuild commandを実行しない。link対象workspace moduleからの予期せぬ呼出しはfail closedにする。
    throw std::runtime_error("Unexpected run_command call in artifact identity test.");
}

int run_command_with_parent_independent_lifetime_guard(
    const std::string& command,
    int,
    const std::string&) {
    return run_command(command);
}
