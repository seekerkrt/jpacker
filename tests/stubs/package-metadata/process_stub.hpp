#pragma once

#include "process.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace package_metadata_test_stub {

void reset_process_stub();
void enqueue_captured_command_result(
        const std::string& command,
        CapturedCommandResult result);

std::size_t capture_command_call_count();
std::string last_captured_command();
std::vector<std::string> captured_commands();

} // namespace package_metadata_test_stub
