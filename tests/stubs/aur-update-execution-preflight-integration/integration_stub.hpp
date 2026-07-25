#pragma once

#include "process.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace aur_update_execution_preflight_integration_stub {

void reset();
void enqueue_captured_command_result(
        const std::string& command, CapturedCommandResult result);

std::vector<std::string> captured_commands();
std::vector<std::string> strict_info_calls();
std::vector<std::string> strict_provider_search_calls();
std::size_t              forbidden_operation_count();

} // namespace aur_update_execution_preflight_integration_stub
