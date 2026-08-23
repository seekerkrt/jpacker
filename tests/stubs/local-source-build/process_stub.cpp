#include "process_stub.hpp"

#include <deque>
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
    local_source_build_test_stub::ProcessHook hook = nullptr;
};

std::deque<ExpectedProcessCall> g_expected_calls;
std::size_t                     g_capture_calls = 0;
std::size_t                     g_run_calls = 0;
std::string                     g_expectation_failure;

[[noreturn]] void fail_unexpected_process_call(const char* diagnostic) {
    g_expectation_failure = diagnostic;
    throw std::logic_error(diagnostic);
}

ExpectedProcessCall take_expected_call(
        ExpectedProcessKind kind, const std::string& command) {
    if(g_expected_calls.empty() || g_expected_calls.front().kind != kind) {
        fail_unexpected_process_call(
                "Unexpected local source build process call kind.");
    }
    if(g_expected_calls.front().command != command) {
        fail_unexpected_process_call(
                "Local source build process command did not match the next expectation.");
    }

    ExpectedProcessCall expected = std::move(g_expected_calls.front());
    g_expected_calls.pop_front();
    return expected;
}

} // namespace

namespace local_source_build_test_stub {

void reset_process_stub() {
    g_expected_calls.clear();
    g_capture_calls = 0;
    g_run_calls = 0;
    g_expectation_failure.clear();
}

void expect_capture_command(
        std::string command, CapturedCommandResult result,
        ProcessHook hook) {
    g_expected_calls.push_back(ExpectedProcessCall{
            ExpectedProcessKind::Capture,
            std::move(command),
            std::move(result),
            0,
            hook});
}

void expect_run_command(
        std::string command, int exit_code, ProcessHook hook) {
    g_expected_calls.push_back(ExpectedProcessCall{
            ExpectedProcessKind::Run,
            std::move(command),
            CapturedCommandResult{},
            exit_code,
            hook});
}

void require_process_expectations_consumed() {
    if(!g_expectation_failure.empty()) {
        throw std::logic_error(g_expectation_failure);
    }
    if(!g_expected_calls.empty()) {
        throw std::logic_error(
                "Local source build process stub has unconsumed expectations.");
    }
}

std::size_t capture_command_call_count() {
    return g_capture_calls;
}

std::size_t run_command_call_count() {
    return g_run_calls;
}

} // namespace local_source_build_test_stub

CapturedCommandResult capture_command_output_raw(const char* command) {
    ++g_capture_calls;
    ExpectedProcessCall expected = take_expected_call(
            ExpectedProcessKind::Capture,
            command == nullptr ? std::string{} : std::string(command));
    if(expected.hook != nullptr) expected.hook();
    return std::move(expected.capture_result);
}

int run_command(const std::string& command) {
    ++g_run_calls;
    ExpectedProcessCall expected =
            take_expected_call(ExpectedProcessKind::Run, command);
    if(expected.hook != nullptr) expected.hook();
    return expected.run_exit_code;
}

int run_command_with_parent_independent_lifetime_guard(
        const std::string& command,
        int,
        const std::string&) {
    return run_command(command);
}
