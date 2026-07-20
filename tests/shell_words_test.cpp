#include "shell_words.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_equal(
        const std::string& test_case,
        const std::string& actual,
        const std::string& expected) {
    if(actual == expected) return;
    throw std::runtime_error(
            test_case + ": expected [" + expected + "], actual [" + actual + "]");
}

void test_quote() {
    expect_equal("quote empty string", shell_words::quote(""), "''");
    expect_equal("quote plain word", shell_words::quote("plain"), "'plain'");
    expect_equal("quote apostrophe", shell_words::quote("a'b"), "'a'\\''b'");
    expect_equal("quote space", shell_words::quote("two words"), "'two words'");
    expect_equal("quote tab", shell_words::quote("a\tb"), "'a\tb'");
    expect_equal("quote newline", shell_words::quote("a\nb"), "'a\nb'");
    expect_equal("quote leading hyphen", shell_words::quote("-option"), "'-option'");
    expect_equal(
            "quote shell metacharacters",
            shell_words::quote("$HOME;echo * | cat & < >"),
            "'$HOME;echo * | cat & < >'");
    expect_equal(
            "quote multiple apostrophes",
            shell_words::quote("a'b'c"),
            "'a'\\''b'\\''c'");
}

void test_join() {
    expect_equal("join empty vector", shell_words::join({}), "");
    expect_equal("join one argument", shell_words::join({"plain"}), "'plain'");
    expect_equal(
            "join two arguments",
            shell_words::join({"one", "two"}),
            "'one' 'two'");
    expect_equal(
            "join empty element",
            shell_words::join({"one", "", "two"}),
            "'one' '' 'two'");
    expect_equal(
            "join apostrophe",
            shell_words::join({"a'b"}),
            "'a'\\''b'");
    expect_equal(
            "join whitespace and newline",
            shell_words::join({"two words", "a\nb"}),
            "'two words' 'a\nb'");
    expect_equal(
            "join leading hyphen",
            shell_words::join({"command", "-option"}),
            "'command' '-option'");
    expect_equal(
            "join argument order",
            shell_words::join({"third", "first", "second"}),
            "'third' 'first' 'second'");
    expect_equal(
            "join single-space separator without trailing space",
            shell_words::join({"a", "b", "c"}),
            "'a' 'b' 'c'");
}

} // namespace

int main() {
    try {
        test_quote();
        test_join();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "shell word serialization tests: all checks passed\n";
    return 0;
}
