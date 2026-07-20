#include "shell_words.hpp"

#include <cstddef>
#include <sstream>

namespace shell_words {

std::string quote(const std::string& value) {
    // POLICY: validationとは独立に、値全体を1つのshell wordとしてsingle-quoteする。
    std::string quoted = "'";
    for(char character : value) {
        if(character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }
    quoted += "'";
    return quoted;
}

std::string join(const std::vector<std::string>& arguments) {
    std::stringstream joined;
    for(std::size_t i = 0; i < arguments.size(); ++i) {
        if(i > 0) joined << " ";
        joined << quote(arguments[i]);
    }
    return joined.str();
}

} // namespace shell_words
