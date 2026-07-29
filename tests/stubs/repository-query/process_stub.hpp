#pragma once

#include "process.hpp"

#include <string>
#include <vector>

namespace repository_query_test_stub {

void enqueue_captured_command_result(
        const std::string& command,
        CapturedCommandResult result);

std::vector<std::string> captured_commands();

} // namespace repository_query_test_stub
