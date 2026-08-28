#pragma once

#include "dependency_plan.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace aur_update_execution_preflight_test_stub {

using ResolverHandler =
    std::function<BuildPlan(const std::vector<std::string>& targets)>;

void reset_preflight_stub();
void set_resolver_handler(ResolverHandler handler);

std::size_t resolver_call_count();
const std::vector<std::vector<std::string>>& resolver_calls();
const std::vector<bool>& resolver_selection_callback_presence();

} // namespace aur_update_execution_preflight_test_stub
