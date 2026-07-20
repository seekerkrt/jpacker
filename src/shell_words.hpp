#pragma once

#include <string>
#include <vector>

namespace shell_words {

std::string quote(const std::string& value);
std::string join(const std::vector<std::string>& arguments);

} // namespace shell_words
