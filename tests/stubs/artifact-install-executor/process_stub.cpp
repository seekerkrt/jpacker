#include "process_stub.hpp"

#include <cstdlib>
#include <deque>
#include <stdexcept>
#include <utility>

namespace {

struct ExpectedCaptureCommand {
    std::string           command;
    CapturedCommandResult result;
};

struct ExpectedRunCommand {
    std::string command;
    int         exit_code = 0;
};

struct ProcessStubState {
    std::deque<ExpectedCaptureCommand> expected_capture_commands;
    std::deque<ExpectedRunCommand>     expected_run_commands;
    std::size_t                        capture_calls = 0;
    std::size_t                        run_calls = 0;
    std::string                        last_captured_command;
    std::string                        last_run_command;
    const char*                        expectation_failure = nullptr;
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
    process_stub_state().expected_capture_commands.push_back(
            ExpectedCaptureCommand{
                    std::move(command), std::move(result)});
}

void expect_run_command(std::string command, int exit_code) {
    process_stub_state().expected_run_commands.push_back(
            ExpectedRunCommand{std::move(command), exit_code});
}

void require_process_expectations_consumed() {
    const ProcessStubState& state = process_stub_state();
    if(state.expectation_failure != nullptr) {
        throw std::logic_error(state.expectation_failure);
    }
    if(!state.expected_capture_commands.empty()) {
        throw std::logic_error(
                "Artifact install process stub has unconsumed capture command expectations.");
    }
    if(!state.expected_run_commands.empty()) {
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

    if(state.expected_capture_commands.empty()) {
        fail_process_expectation(
                state,
                "Unexpected artifact install capture command with no pending expectation.");
    }
    if(state.last_captured_command !=
       state.expected_capture_commands.front().command) {
        fail_process_expectation(
                state,
                "Artifact install capture command did not match the next expectation.");
    }

    ExpectedCaptureCommand expectation =
            std::move(state.expected_capture_commands.front());
    state.expected_capture_commands.pop_front();
    if(state.capture_hook != nullptr) state.capture_hook();
    return std::move(expectation.result);
}

int run_command(const std::string& command) {
    ProcessStubState& state = process_stub_state();
    ++state.run_calls;
    state.last_run_command = command;

    if(state.expected_run_commands.empty()) {
        fail_process_expectation(
                state,
                "Unexpected artifact install run command with no pending expectation.");
    }
    if(state.last_run_command != state.expected_run_commands.front().command) {
        fail_process_expectation(
                state,
                "Artifact install run command did not match the next expectation.");
    }

    ExpectedRunCommand expectation =
            std::move(state.expected_run_commands.front());
    state.expected_run_commands.pop_front();
    if(state.run_hook != nullptr) state.run_hook();
    return expectation.exit_code;
}
