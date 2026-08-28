#pragma once

#include "process.hpp"

#include <cstddef>
#include <string>

namespace artifact_install_executor_test_stub {

void reset_process_stub();
void expect_capture_command(
    std::string command, CapturedCommandResult result);
void expect_run_command(std::string command, int exit_code);
void require_process_expectations_consumed();
void set_capture_hook(void (*hook)());
void set_run_hook(void (*hook)());

std::size_t capture_command_call_count();
std::size_t run_command_call_count();
std::string last_captured_command();
std::string last_run_command();

} // namespace artifact_install_executor_test_stub
