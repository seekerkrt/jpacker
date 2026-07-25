#include "preflight_stub.hpp"

#include <stdexcept>
#include <utility>

namespace {

struct PreflightStubState {
    aur_update_execution_preflight_test_stub::ResolverHandler resolver_handler;
    std::vector<std::vector<std::string>>                      resolver_calls;
};

PreflightStubState g_state;

} // namespace

namespace aur_update_execution_preflight_test_stub {

void reset_preflight_stub() {
    g_state = PreflightStubState{};
}

void set_resolver_handler(ResolverHandler handler) {
    g_state.resolver_handler = std::move(handler);
}

std::size_t resolver_call_count() {
    return g_state.resolver_calls.size();
}

const std::vector<std::vector<std::string>>& resolver_calls() {
    return g_state.resolver_calls;
}

} // namespace aur_update_execution_preflight_test_stub

BuildPlan resolve_build_plan_for_preflight(
        const std::vector<std::string>& targets) {
    g_state.resolver_calls.push_back(targets);
    if(!g_state.resolver_handler) {
        throw std::logic_error(
                "Unexpected AUR update execution preflight resolver call.");
    }
    return g_state.resolver_handler(targets);
}
