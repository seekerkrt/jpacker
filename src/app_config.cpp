#include "app_config.hpp"

#include <cstdlib>
#include <string>
#include <utility>

namespace {

// POLICY(#65): editor selection stays environment-owned and is snapshotted
// once with the other invocation settings.
std::string resolve_editor_from_environment() {
    const char* visual = std::getenv("VISUAL");
    if(visual != nullptr && visual[0] != '\0') return visual;

    const char* editor = std::getenv("EDITOR");
    if(editor != nullptr && editor[0] != '\0') return editor;

    return "nano";
}

} // namespace

AppConfig make_app_config(
        UserConfig final_user_config, bool no_confirm, bool rm_deps) {
    return AppConfig{
            std::move(final_user_config),
            no_confirm,
            rm_deps,
            resolve_editor_from_environment(),
            make_provider_selection_session(no_confirm)};
}

ProviderSelectionCallback provider_selection_callback(const AppConfig& config) {
    if(!config.provider_selection) return {};

    std::shared_ptr<ProviderSelectionSession> session = config.provider_selection;
    return [session = std::move(session)](
                   const std::string& dependency,
                   const std::vector<ProvidedDependency>& candidates) {
        return session->select_provider(dependency, candidates);
    };
}
