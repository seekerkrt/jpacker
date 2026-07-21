#pragma once

#include "process.hpp"

#include <cstddef>
#include <string>

namespace package_metadata_test_stub {

void reset_process_stub();
void set_captured_command_result(CapturedCommandResult result);

std::size_t capture_command_call_count();
std::string last_captured_command();

} // namespace package_metadata_test_stub
