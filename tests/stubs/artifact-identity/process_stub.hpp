#pragma once

#include "process.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace artifact_identity_test_stub {

using CaptureHook = void (*)();

struct CapturedCommandStep {
    CapturedCommandResult result;
    CaptureHook           before_hook = nullptr;
    CaptureHook           after_hook = nullptr;
};

void reset_process_stub();
void set_captured_command_result(CapturedCommandResult result);
void set_capture_hook(void (*hook)());
void set_captured_command_results(
        std::vector<CapturedCommandResult> results);
void set_captured_command_steps(
        std::vector<CapturedCommandStep> steps);

std::size_t capture_command_call_count();
std::string last_captured_command();
std::vector<std::string> captured_commands();

} // namespace artifact_identity_test_stub
