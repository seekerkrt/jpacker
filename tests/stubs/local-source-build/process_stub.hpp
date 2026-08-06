#pragma once

#include "process.hpp"

#include <cstddef>
#include <string>

namespace local_source_build_test_stub {

using ProcessHook = void (*)();

void reset_process_stub();
void expect_capture_command(
        std::string command, CapturedCommandResult result,
        ProcessHook hook = nullptr);
void expect_run_command(
        std::string command, int exit_code, ProcessHook hook = nullptr);
void require_process_expectations_consumed();

std::size_t capture_command_call_count();
std::size_t run_command_call_count();

} // namespace local_source_build_test_stub
