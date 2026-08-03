#pragma once

#include "dependency_plan.hpp"

#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// invocation内で確定済みだったproviderが、同じ依存の現在候補から消えた状態。
class ProviderSelectionConflict final : public std::runtime_error {
public:
    explicit ProviderSelectionConflict(std::string dependency_name);

    const std::string& dependency_name() const noexcept;

private:
    std::string dependency_name_;
};

// provider選択をinvocation単位で共有し、CLI入出力とplan callbackを接続する。
class ProviderSelectionSession final {
public:
    ProviderSelectionSession(
            std::istream& input, std::ostream& output,
            bool is_interactive);

    ProviderSelectionSession(const ProviderSelectionSession&) = delete;
    ProviderSelectionSession& operator=(const ProviderSelectionSession&) = delete;

    std::optional<ProvidedDependency> select_provider(
            const std::string& dependency,
            const std::vector<ProvidedDependency>& candidates);

    bool is_interactive() const noexcept;

private:
    std::istream* input_;
    std::ostream* output_;
    bool is_interactive_;
    std::map<std::string, ProvidedDependency> selections_;
    std::set<std::string> cancelled_dependencies_;
};

// production sessionはstdinがTTYかつ--noconfirm未指定の場合だけ入力を読む。
std::shared_ptr<ProviderSelectionSession> make_provider_selection_session(
        bool no_confirm);
