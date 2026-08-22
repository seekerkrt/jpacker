#include "process_stub.hpp"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace {

enum class ExpectedProcessKind {
    Capture,
    Run,
};

struct ExpectedProcessCall {
    ExpectedProcessKind   kind;
    std::string           command;
    CapturedCommandResult capture_result;
    int                   run_exit_code = 0;
};

struct ProcessStubState {
    // POLICY: capture/runを同じFIFOへ積み、process API種別を跨ぐ順序も契約に含める。
    std::deque<ExpectedProcessCall> expected_calls;
    std::size_t                     capture_calls = 0;
    std::size_t                     run_calls = 0;
    std::string                     last_captured_command;
    std::string                     last_run_command;
    const char*                     expectation_failure = nullptr;
    void (*capture_hook)() = nullptr;
    void (*run_hook)() = nullptr;
};

ProcessStubState* g_state = nullptr;
bool              g_cleanup_registered = false;

void destroy_process_stub_state() {
    delete g_state;
    g_state = nullptr;
}

ProcessStubState& process_stub_state() {
    if(g_state == nullptr) {
        g_state = new ProcessStubState;
        if(!g_cleanup_registered) {
            static_cast<void>(std::atexit(destroy_process_stub_state));
            g_cleanup_registered = true;
        }
    }
    return *g_state;
}

[[noreturn]] void fail_process_expectation(
        ProcessStubState& state, const char* diagnostic) {
    // POLICY: fixed diagnosticだけを保持し、package-controlled commandをerrorへ埋め込まない。
    state.expectation_failure = diagnostic;
    throw std::logic_error(diagnostic);
}

} // namespace

namespace artifact_install_executor_test_stub {

void reset_process_stub() {
    process_stub_state() = ProcessStubState{};
}

void expect_capture_command(
        std::string command, CapturedCommandResult result) {
    process_stub_state().expected_calls.push_back(ExpectedProcessCall{
            ExpectedProcessKind::Capture,
            std::move(command),
            std::move(result),
            0});
}

void expect_run_command(std::string command, int exit_code) {
    process_stub_state().expected_calls.push_back(ExpectedProcessCall{
            ExpectedProcessKind::Run,
            std::move(command),
            CapturedCommandResult{},
            exit_code});
}

void require_process_expectations_consumed() {
    const ProcessStubState& state = process_stub_state();
    if(state.expectation_failure != nullptr) {
        throw std::logic_error(state.expectation_failure);
    }
    if(!state.expected_calls.empty() &&
       state.expected_calls.front().kind == ExpectedProcessKind::Capture) {
        throw std::logic_error(
                "Artifact install process stub has unconsumed capture command expectations.");
    }
    if(!state.expected_calls.empty()) {
        throw std::logic_error(
                "Artifact install process stub has unconsumed run command expectations.");
    }
}

void set_capture_hook(void (*hook)()) {
    process_stub_state().capture_hook = hook;
}

void set_run_hook(void (*hook)()) {
    process_stub_state().run_hook = hook;
}

std::size_t capture_command_call_count() {
    return process_stub_state().capture_calls;
}

std::size_t run_command_call_count() {
    return process_stub_state().run_calls;
}

std::string last_captured_command() {
    return process_stub_state().last_captured_command;
}

std::string last_run_command() {
    return process_stub_state().last_run_command;
}

} // namespace artifact_install_executor_test_stub

CapturedCommandResult capture_command_output_raw(const char* command) {
    ProcessStubState& state = process_stub_state();
    ++state.capture_calls;
    state.last_captured_command = command == nullptr ? "" : command;

    if(state.expected_calls.empty() ||
       state.expected_calls.front().kind != ExpectedProcessKind::Capture) {
        fail_process_expectation(
                state,
                "Unexpected artifact install capture command with no pending expectation.");
    }
    if(state.last_captured_command !=
       state.expected_calls.front().command) {
        fail_process_expectation(
                state,
                "Artifact install capture command did not match the next expectation.");
    }

    ExpectedProcessCall expectation = std::move(state.expected_calls.front());
    state.expected_calls.pop_front();
    if(state.capture_hook != nullptr) state.capture_hook();
    return std::move(expectation.capture_result);
}

int run_command(const std::string& command) {
    ProcessStubState& state = process_stub_state();
    ++state.run_calls;
    state.last_run_command = command;

    if(state.expected_calls.empty() ||
       state.expected_calls.front().kind != ExpectedProcessKind::Run) {
        fail_process_expectation(
                state,
                "Unexpected artifact install run command with no pending expectation.");
    }
    if(state.last_run_command != state.expected_calls.front().command) {
        fail_process_expectation(
                state,
                "Artifact install run command did not match the next expectation.");
    }

    ExpectedProcessCall expectation = std::move(state.expected_calls.front());
    state.expected_calls.pop_front();
    if(state.run_hook != nullptr) state.run_hook();
    return expectation.run_exit_code;
}

int run_command_with_parent_independent_lifetime_guard(
        const std::string& command,
        int,
        const std::string& display_command) {
    if(display_command.empty()) return run_command(command);

    CapturedCommandResult result =
            capture_command_output_raw(display_command.c_str());
    const std::size_t redirect = command.rfind(" > ");
    if(redirect == std::string::npos) {
        throw std::logic_error(
                "Guarded capture command has no output redirection.");
    }
    std::string output_path = command.substr(redirect + 3);
    if(output_path.size() < 2 || output_path.front() != '\'' ||
       output_path.back() != '\'') {
        throw std::logic_error(
                "Guarded capture command has an unsupported output path.");
    }
    output_path = output_path.substr(1, output_path.size() - 2);
    std::ofstream output(
            output_path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::logic_error(
                "Guarded capture stub could not open its output path.");
    }
    output.write(
            result.output.data(),
            static_cast<std::streamsize>(result.output.size()));
    output.close();
    if(!output) {
        throw std::logic_error(
                "Guarded capture stub could not write its output.");
    }
    return result.exit_code;
}
