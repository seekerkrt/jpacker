#include "process_stub.hpp"

#include <utility>

namespace {

CapturedCommandResult g_result;
std::size_t           g_capture_calls = 0;
std::string           g_last_command;

} // namespace

namespace package_metadata_test_stub {

void reset_process_stub() {
    g_result = CapturedCommandResult{};
    g_capture_calls = 0;
    g_last_command.clear();
}

void set_captured_command_result(CapturedCommandResult result) {
    g_result = std::move(result);
}

std::size_t capture_command_call_count() {
    return g_capture_calls;
}

std::string last_captured_command() {
    return g_last_command;
}

} // namespace package_metadata_test_stub

CapturedCommandResult capture_command_output_raw(const char* command) {
    ++g_capture_calls;
    g_last_command = command == nullptr ? "" : command;
    return g_result;
}
