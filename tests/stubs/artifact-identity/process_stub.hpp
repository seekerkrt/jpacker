#pragma once

#include "process.hpp"

#include <cstddef>
#include <string>

namespace artifact_identity_test_stub {

void reset_process_stub();
void set_captured_command_result(CapturedCommandResult result);
void set_capture_hook(void (*hook)());

std::size_t capture_command_call_count();
std::string last_captured_command();

} // namespace artifact_identity_test_stub
