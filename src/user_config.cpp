#include "user_config.hpp"

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <toml++/toml.hpp>

namespace {

namespace fs = std::filesystem;

constexpr std::int64_t SUPPORTED_SCHEMA_VERSION = 1;

std::string diagnostic_prefix(const fs::path& config_path) {
    return "User config error: '" + config_path.string() + "'";
}

std::string source_location(const toml::source_region& source) {
    if(!source.begin) return "";

    std::ostringstream location;
    location << " (line " << source.begin.line << ", column "
             << source.begin.column << ')';
    return location.str();
}

std::string node_type_name(const toml::node& node) {
    std::ostringstream type;
    type << node.type();
    return type.str();
}

std::string file_type_name(fs::file_type type) {
    switch(type) {
        case fs::file_type::none:
            return "unknown file type";
        case fs::file_type::not_found:
            return "missing target";
        case fs::file_type::regular:
            return "regular file";
        case fs::file_type::directory:
            return "directory";
        case fs::file_type::symlink:
            return "symbolic link";
        case fs::file_type::block:
            return "block device";
        case fs::file_type::character:
            return "character device";
        case fs::file_type::fifo:
            return "FIFO";
        case fs::file_type::socket:
            return "socket";
        case fs::file_type::unknown:
            return "unknown file type";
    }
    return "unknown file type";
}

[[noreturn]] void throw_path_error(
        const fs::path& config_path, const std::string& problem) {
    throw UserConfigError(diagnostic_prefix(config_path) + ": " + problem);
}

[[noreturn]] void throw_validation_error(
        const fs::path& config_path, const std::string& subject,
        const std::string& problem,
        const toml::source_region* source = nullptr) {
    std::string diagnostic =
            diagnostic_prefix(config_path) + ": " + subject;
    if(source != nullptr) diagnostic += source_location(*source);
    diagnostic += ": " + problem;
    throw UserConfigError(std::move(diagnostic));
}

bool require_regular_config_file(const fs::path& config_path) {
    std::error_code error;
    const fs::file_status entry_status = fs::symlink_status(config_path, error);
    if(error == std::errc::no_such_file_or_directory) return false;
    if(error) {
        throw_path_error(
                config_path,
                "unable to inspect config path: " + error.message());
    }
    if(entry_status.type() == fs::file_type::not_found) return false;

    const fs::file_status target_status = fs::status(config_path, error);
    if(error) {
        throw_path_error(
                config_path,
                "unable to inspect config target: " + error.message());
    }
    if(!fs::is_regular_file(target_status)) {
        throw_path_error(
                config_path,
                "config path must resolve to a readable regular file; got " +
                        file_type_name(target_status.type()));
    }
    return true;
}

std::string read_config_file(const fs::path& config_path) {
    std::ifstream file(config_path, std::ios::binary);
    if(!file.is_open()) {
        throw_path_error(config_path, "unable to open config file for reading");
    }

    std::string              contents;
    std::array<char, 16'384> buffer{};
    while(file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if(count > 0) {
            contents.append(buffer.data(), static_cast<std::size_t>(count));
        }
    }

    if(file.bad() || (file.fail() && !file.eof())) {
        throw_path_error(config_path, "unable to read config file");
    }
    return contents;
}

toml::table parse_config_file(const fs::path& config_path) {
    const std::string contents = read_config_file(config_path);
    try {
        return toml::parse(contents, config_path.string());
    } catch(const toml::parse_error& error) {
        std::string diagnostic =
                diagnostic_prefix(config_path) + ": TOML parse error" +
                source_location(error.source()) + ": " +
                std::string(error.description());
        throw UserConfigError(std::move(diagnostic));
    }
}

void validate_top_level_keys(
        const toml::table& root, const fs::path& config_path) {
    for(const auto& [key, node] : root) {
        const std::string_view name = key.str();
        if(name == "schema_version" || name == "review" || name == "build") {
            continue;
        }

        if(node.is_table()) {
            throw_validation_error(
                    config_path,
                    "top-level section '" + std::string(name) + "'",
                    "unknown top-level section; accepted entries: "
                    "schema_version, review, build",
                    &key.source());
        }
        throw_validation_error(
                config_path, "top-level key '" + std::string(name) + "'",
                "unknown top-level key; accepted entries: "
                "schema_version, review, build",
                &key.source());
    }
}

std::int64_t parse_schema_version(
        const toml::table& root, const fs::path& config_path) {
    const toml::node* version_node = root.get("schema_version");
    if(version_node == nullptr) {
        throw_validation_error(
                config_path, "key 'schema_version'",
                "missing required key; expected integer 1");
    }

    const auto version = version_node->value_exact<std::int64_t>();
    if(!version) {
        throw_validation_error(
                config_path, "key 'schema_version'",
                "expected integer 1; got " + node_type_name(*version_node),
                &version_node->source());
    }
    if(*version != SUPPORTED_SCHEMA_VERSION) {
        throw_validation_error(
                config_path, "key 'schema_version'",
                "unsupported schema version " + std::to_string(*version) +
                        "; expected integer 1",
                &version_node->source());
    }
    return *version;
}

const toml::table* optional_section(
        const toml::table& root, const fs::path& config_path,
        std::string_view section_name) {
    const toml::node* section_node = root.get(section_name);
    if(section_node == nullptr) return nullptr;

    const toml::table* section = section_node->as_table();
    if(section == nullptr) {
        throw_validation_error(
                config_path,
                "section '" + std::string(section_name) + "'",
                "expected table; got " + node_type_name(*section_node),
                &section_node->source());
    }
    return section;
}

void validate_review_keys(
        const toml::table& review, const fs::path& config_path) {
    for(const auto& [key, node] : review) {
        static_cast<void>(node);
        const std::string_view name = key.str();
        if(name == "pkgbuild" || name == "diff") continue;

        throw_validation_error(
                config_path,
                "section 'review', key '" + std::string(name) + "'",
                "unknown key; accepted keys: pkgbuild, diff",
                &key.source());
    }
}

void validate_build_keys(
        const toml::table& build, const fs::path& config_path) {
    for(const auto& [key, node] : build) {
        static_cast<void>(node);
        const std::string_view name = key.str();
        if(name == "mode") continue;

        throw_validation_error(
                config_path,
                "section 'build', key '" + std::string(name) + "'",
                "unknown key; accepted key: mode", &key.source());
    }
}

ReviewPolicy parse_review_policy(
        const toml::node& node, const fs::path& config_path,
        std::string_view key_name) {
    const auto value = node.value_exact<std::string>();
    if(!value) {
        throw_validation_error(
                config_path, "key '" + std::string(key_name) + "'",
                "expected string; accepted values: prompt, skip; got " +
                        node_type_name(node),
                &node.source());
    }
    if(*value == "prompt") return ReviewPolicy::Prompt;
    if(*value == "skip") return ReviewPolicy::Skip;

    throw_validation_error(
            config_path, "key '" + std::string(key_name) + "'",
            "unsupported value; accepted values: prompt, skip",
            &node.source());
}

BuildMode parse_build_mode(
        const toml::node& node, const fs::path& config_path) {
    const auto value = node.value_exact<std::string>();
    if(!value) {
        throw_validation_error(
                config_path, "key 'build.mode'",
                "expected string; accepted values: normal, rebuild, clean; "
                "got " +
                        node_type_name(node),
                &node.source());
    }
    if(*value == "normal") return BuildMode::Normal;
    if(*value == "rebuild") return BuildMode::Rebuild;
    if(*value == "clean") return BuildMode::Clean;

    throw_validation_error(
            config_path, "key 'build.mode'",
            "unsupported value; accepted values: normal, rebuild, clean",
            &node.source());
}

void apply_review_config(
        UserConfig& config, const toml::table& root,
        const fs::path& config_path) {
    const toml::table* review = optional_section(root, config_path, "review");
    if(review == nullptr) return;

    validate_review_keys(*review, config_path);
    if(const toml::node* pkgbuild = review->get("pkgbuild")) {
        config.review.pkgbuild =
                parse_review_policy(*pkgbuild, config_path, "review.pkgbuild");
    }
    if(const toml::node* diff = review->get("diff")) {
        config.review.diff =
                parse_review_policy(*diff, config_path, "review.diff");
    }
}

void apply_build_config(
        UserConfig& config, const toml::table& root,
        const fs::path& config_path) {
    const toml::table* build = optional_section(root, config_path, "build");
    if(build == nullptr) return;

    validate_build_keys(*build, config_path);
    if(const toml::node* mode = build->get("mode")) {
        config.build.mode = parse_build_mode(*mode, config_path);
    }
}

} // namespace

UserConfig load_user_config(const std::filesystem::path& config_path) {
    if(!require_regular_config_file(config_path)) return UserConfig{};

    const toml::table root = parse_config_file(config_path);
    validate_top_level_keys(root, config_path);

    UserConfig config;
    config.schema_version = parse_schema_version(root, config_path);
    apply_review_config(config, root, config_path);
    apply_build_config(config, root, config_path);
    return config;
}
