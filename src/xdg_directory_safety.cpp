#include "xdg_directory_safety.hpp"

#include "application_identity.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace xdg_directory_safety {
namespace {

namespace fs = std::filesystem;

constexpr mode_t NEW_DIRECTORY_MODE = 0700;
constexpr mode_t REQUIRED_OWNER_PERMISSIONS = S_IRUSR | S_IWUSR | S_IXUSR;
constexpr mode_t FORBIDDEN_WRITE_PERMISSIONS = S_IWGRP | S_IWOTH;

struct DirectoryRequest {
    xdg_paths::DirectoryKind directory_kind;
    const fs::path&          directory;
    const xdg_paths::DirectoryCreationBoundary& creation_boundary;
    bool derived_paths_match;
};

class OwnedFileDescriptor final {
    int descriptor_ = -1;

public:
    explicit OwnedFileDescriptor(int descriptor = -1) noexcept
        : descriptor_(descriptor) {
    }

    OwnedFileDescriptor(const OwnedFileDescriptor&) = delete;
    OwnedFileDescriptor& operator=(const OwnedFileDescriptor&) = delete;

    OwnedFileDescriptor(OwnedFileDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {
    }

    OwnedFileDescriptor& operator=(OwnedFileDescriptor&& other) noexcept {
        if(this == &other) return *this;
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }

    ~OwnedFileDescriptor() noexcept {
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        return std::exchange(descriptor_, -1);
    }
};

struct OpenedDirectory {
    OwnedFileDescriptor descriptor;
    struct stat         status {};
};

struct PreparedDirectoryState {
    OwnedFileDescriptor parent_descriptor;
    OwnedFileDescriptor directory_descriptor;
    std::string         leaf_name;
    struct stat         status {};
    std::uintmax_t      observed_owner = 0;
    std::size_t         created_component_count = 0;
};

#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
using TestOverrides = DirectorySafetyTestOverrides;
#else
struct TestOverrides final {};
#endif

std::string_view directory_kind_name(
        xdg_paths::DirectoryKind directory_kind) {
    switch(directory_kind) {
    case xdg_paths::DirectoryKind::Config:
        return "config";
    case xdg_paths::DirectoryKind::State:
        return "state";
    case xdg_paths::DirectoryKind::Cache:
        return "cache";
    }
    throw std::logic_error("Unknown XDG directory kind.");
}

std::string_view preparation_stage_name(PreparationStage stage) {
    switch(stage) {
    case PreparationStage::BoundaryValidation:
        return "creation-boundary validation";
    case PreparationStage::FilesystemRootOpen:
        return "filesystem-root open";
    case PreparationStage::AnchorTraversal:
        return "anchor traversal";
    case PreparationStage::AnchorValidation:
        return "anchor validation";
    case PreparationStage::ComponentInspection:
        return "component inspection";
    case PreparationStage::ComponentCreation:
        return "component creation";
    case PreparationStage::ComponentOpen:
        return "component open";
    case PreparationStage::ComponentValidation:
        return "component validation";
    case PreparationStage::DirectoryRevalidation:
        return "directory revalidation";
    }
    throw std::logic_error("Unknown XDG directory preparation stage.");
}

std::string_view preparation_error_description(PreparationErrorCode code) {
    switch(code) {
    case PreparationErrorCode::MissingAnchor:
        return "the required existing anchor is missing";
    case PreparationErrorCode::Symlink:
        return "a symlink component is not allowed";
    case PreparationErrorCode::NotDirectory:
        return "a path component is not a directory";
    case PreparationErrorCode::OwnershipMismatch:
        return "directory ownership does not match the effective user";
    case PreparationErrorCode::UnsafePermissions:
        return "directory permissions are unsafe";
    case PreparationErrorCode::PermissionDenied:
        return "filesystem permission was denied";
    case PreparationErrorCode::CreationFailed:
        return "directory creation failed";
    case PreparationErrorCode::MetadataFailure:
        return "filesystem metadata could not be obtained safely";
    case PreparationErrorCode::ConcurrentReplacement:
        return "a path component changed during validation";
    case PreparationErrorCode::InvalidCreationBoundary:
        return "the resolved creation boundary is invalid";
    }
    throw std::logic_error("Unknown XDG directory preparation error code.");
}

std::string preparation_diagnostic(const PreparationFailure& failure) {
    std::string diagnostic =
            "Cannot prepare " +
            std::string(application_identity::PROJECT_NAME) + " " +
            std::string(directory_kind_name(failure.directory_kind)) +
            " directory during " +
            std::string(preparation_stage_name(failure.stage)) + ": " +
            std::string(preparation_error_description(failure.code)) + ".";
    if(failure.system_error.has_value()) {
        diagnostic += " System error: " + failure.system_error->message() + ".";
    }
    return diagnostic;
}

[[noreturn]] void throw_preparation_error(
        xdg_paths::DirectoryKind directory_kind,
        PreparationStage stage,
        PreparationErrorCode code,
        std::optional<int> error_number = std::nullopt,
        std::optional<std::size_t> component_index = std::nullopt) {
    std::optional<std::error_code> system_error;
    if(error_number.has_value()) {
        system_error =
                std::error_code(error_number.value(), std::generic_category());
    }
    throw PreparationError(PreparationFailure{
            directory_kind, stage, code, system_error,
            component_index});
}

bool has_ambiguous_leading_double_slash(const fs::path& path) {
    const std::string& value = path.native();
    return value.size() >= 2 && value[0] == '/' && value[1] == '/' &&
           (value.size() == 2 || value[2] != '/');
}

bool is_normalized_absolute_authority_path(const fs::path& path) {
    if(!path.is_absolute() || path.root_path() != fs::path("/")) return false;
    if(path.native().find('\0') != std::string::npos ||
       has_ambiguous_leading_double_slash(path)) {
        return false;
    }
    for(const auto& component : path.relative_path()) {
        if(component == "." || component == "..") return false;
    }
    try {
        return path.lexically_normal() == path;
    } catch(const fs::filesystem_error&) {
        return false;
    }
}

bool is_safe_leaf_name(const std::string& component) {
    if(component.empty() || component == "." || component == ".." ||
       component.find('\0') != std::string::npos) {
        return false;
    }
    const fs::path path(component);
    return path.is_relative() && path.has_filename() &&
           path.parent_path().empty() && path.filename().string() == component;
}

std::vector<std::string> expected_fallback_components(
        xdg_paths::DirectoryKind directory_kind) {
    const std::string application_component(application_identity::XDG_IDENTITY);
    switch(directory_kind) {
    case xdg_paths::DirectoryKind::Config:
        return {".config", application_component};
    case xdg_paths::DirectoryKind::State:
        return {".local", "state", application_component};
    case xdg_paths::DirectoryKind::Cache:
        return {".cache", application_component};
    }
    throw std::logic_error("Unknown XDG directory kind.");
}

void validate_creation_boundary(const DirectoryRequest& request) {
    const xdg_paths::DirectoryCreationBoundary& boundary =
            request.creation_boundary;
    const fs::path filesystem_root("/");

    if(!request.derived_paths_match ||
       !is_normalized_absolute_authority_path(request.directory) ||
       !is_normalized_absolute_authority_path(boundary.base_directory) ||
       !is_normalized_absolute_authority_path(boundary.existing_anchor) ||
       boundary.existing_anchor == filesystem_root ||
       boundary.creatable_components.empty()) {
        throw_preparation_error(
                request.directory_kind,
                PreparationStage::BoundaryValidation,
                PreparationErrorCode::InvalidCreationBoundary);
    }

    for(const std::string& component : boundary.creatable_components) {
        if(!is_safe_leaf_name(component)) {
            throw_preparation_error(
                    request.directory_kind,
                    PreparationStage::BoundaryValidation,
                    PreparationErrorCode::InvalidCreationBoundary);
        }
    }

    const std::string application_component(application_identity::XDG_IDENTITY);
    if(boundary.creatable_components.back() != application_component) {
        throw_preparation_error(
                request.directory_kind,
                PreparationStage::BoundaryValidation,
                PreparationErrorCode::InvalidCreationBoundary);
    }

    fs::path reconstructed_directory = boundary.existing_anchor;
    for(const std::string& component : boundary.creatable_components)
        reconstructed_directory /= component;

    fs::path reconstructed_base = boundary.existing_anchor;
    for(std::size_t index = 0;
        index + 1 < boundary.creatable_components.size(); ++index) {
        reconstructed_base /= boundary.creatable_components[index];
    }

    if(reconstructed_directory != request.directory ||
       reconstructed_base != boundary.base_directory ||
       request.directory.parent_path() != boundary.base_directory) {
        throw_preparation_error(
                request.directory_kind,
                PreparationStage::BoundaryValidation,
                PreparationErrorCode::InvalidCreationBoundary);
    }

    if(boundary.source == xdg_paths::DirectorySource::ExplicitXdg) {
        if(boundary.existing_anchor != boundary.base_directory ||
           boundary.creatable_components !=
                   std::vector<std::string>{application_component}) {
            throw_preparation_error(
                    request.directory_kind,
                    PreparationStage::BoundaryValidation,
                    PreparationErrorCode::InvalidCreationBoundary);
        }
        return;
    }

    if(boundary.source != xdg_paths::DirectorySource::HomeFallback ||
       boundary.creatable_components !=
               expected_fallback_components(request.directory_kind)) {
        throw_preparation_error(
                request.directory_kind,
                PreparationStage::BoundaryValidation,
                PreparationErrorCode::InvalidCreationBoundary);
    }
}

bool is_permission_error(int error_number) {
    return error_number == EACCES || error_number == EPERM ||
           error_number == EROFS;
}

bool is_replacement_error(int error_number) {
    return error_number == ENOENT || error_number == ENOTDIR ||
           error_number == ELOOP;
}

std::uintmax_t effective_user(const TestOverrides* overrides) {
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    if(overrides != nullptr && overrides->effective_user.has_value())
        return overrides->effective_user.value();
#else
    static_cast<void>(overrides);
#endif
    return static_cast<std::uintmax_t>(geteuid());
}

std::uintmax_t observed_owner(
        const struct stat& status, const TestOverrides* overrides) {
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    if(overrides != nullptr && overrides->observed_owner.has_value())
        return overrides->observed_owner.value();
#else
    static_cast<void>(overrides);
#endif
    return static_cast<std::uintmax_t>(status.st_uid);
}

#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
bool inject_failure(
        const TestOverrides* overrides,
        DirectorySafetyTestFailurePoint failure_point,
        std::size_t component_index) {
    if(overrides == nullptr || !overrides->injected_failure.has_value())
        return false;
    const DirectorySafetyInjectedFailure& failure =
            overrides->injected_failure.value();
    if(failure.failure_point != failure_point ||
       failure.component_index != component_index) {
        return false;
    }
    errno = failure.error_number;
    return true;
}

void emit_test_event(
        const TestOverrides* overrides, DirectorySafetyTestEvent event,
        xdg_paths::DirectoryKind directory_kind, std::size_t component_index,
        const fs::path& path) {
    if(overrides != nullptr && overrides->event_hook)
        overrides->event_hook(event, directory_kind, component_index, path);
}
#else
void emit_test_event(
        const TestOverrides*, xdg_paths::DirectoryKind, std::size_t,
        const fs::path&) {
}
#endif

bool same_filesystem_identity(
        const struct stat& expected, const struct stat& actual) {
    return expected.st_dev == actual.st_dev &&
           expected.st_ino == actual.st_ino &&
           (expected.st_mode & S_IFMT) == (actual.st_mode & S_IFMT);
}

void validate_directory_type(
        const struct stat& status,
        xdg_paths::DirectoryKind directory_kind,
        PreparationStage stage,
        std::size_t component_index,
        bool replacement_is_possible) {
    if(S_ISLNK(status.st_mode)) {
        throw_preparation_error(
                directory_kind, stage,
                replacement_is_possible
                        ? PreparationErrorCode::ConcurrentReplacement
                        : PreparationErrorCode::Symlink,
                std::nullopt, component_index);
    }
    if(!S_ISDIR(status.st_mode)) {
        throw_preparation_error(
                directory_kind, stage,
                replacement_is_possible
                        ? PreparationErrorCode::ConcurrentReplacement
                        : PreparationErrorCode::NotDirectory,
                std::nullopt, component_index);
    }
}

void validate_directory_security(
        const struct stat& status,
        xdg_paths::DirectoryKind directory_kind,
        PreparationStage stage,
        std::size_t component_index,
        std::uintmax_t expected_effective_user,
        const TestOverrides* overrides,
        bool was_created) {
    if(observed_owner(status, overrides) != expected_effective_user) {
        throw_preparation_error(
                directory_kind, stage,
                PreparationErrorCode::OwnershipMismatch,
                std::nullopt, component_index);
    }

    const mode_t permissions = status.st_mode & 07777;
    const bool has_required_owner_permissions =
            (permissions & REQUIRED_OWNER_PERMISSIONS) ==
            REQUIRED_OWNER_PERMISSIONS;
    const bool has_forbidden_write_permissions =
            (permissions & FORBIDDEN_WRITE_PERMISSIONS) != 0;
    const bool has_expected_created_mode =
            !was_created || permissions == NEW_DIRECTORY_MODE;
    if(!has_required_owner_permissions || has_forbidden_write_permissions ||
       !has_expected_created_mode) {
        throw_preparation_error(
                directory_kind, stage,
                PreparationErrorCode::UnsafePermissions,
                std::nullopt, component_index);
    }
}

OpenedDirectory open_observed_directory(
        int parent_descriptor, const std::string& leaf_name,
        const struct stat& observed_status,
        xdg_paths::DirectoryKind directory_kind,
        PreparationStage open_stage,
        PreparationStage validation_stage,
        std::size_t component_index,
        std::uintmax_t expected_effective_user,
        const TestOverrides* overrides,
        bool allow_test_injection,
        bool validate_security,
        bool was_created) {
#ifndef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    static_cast<void>(allow_test_injection);
#endif
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    const bool injected_open_failure =
            allow_test_injection && inject_failure(
                    overrides,
                    DirectorySafetyTestFailurePoint::ComponentOpen,
                    component_index);
#else
    const bool injected_open_failure = false;
#endif
    const int descriptor = injected_open_failure
                                   ? -1
                                   : openat(
                                             parent_descriptor,
                                             leaf_name.c_str(),
                                             O_PATH | O_DIRECTORY | O_CLOEXEC |
                                                     O_NOFOLLOW);
    if(descriptor < 0) {
        const int open_error = errno;
        if(is_permission_error(open_error)) {
            throw_preparation_error(
                    directory_kind, open_stage,
                    PreparationErrorCode::PermissionDenied, open_error,
                    component_index);
        }
        throw_preparation_error(
                directory_kind, open_stage,
                is_replacement_error(open_error)
                        ? PreparationErrorCode::ConcurrentReplacement
                        : PreparationErrorCode::MetadataFailure,
                open_error, component_index);
    }
    OwnedFileDescriptor opened(descriptor);

    struct stat opened_status {};
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    const bool injected_metadata_failure =
            allow_test_injection && inject_failure(
                    overrides,
                    DirectorySafetyTestFailurePoint::DescriptorMetadata,
                    component_index);
#else
    const bool injected_metadata_failure = false;
#endif
    if(injected_metadata_failure || fstat(opened.get(), &opened_status) != 0) {
        const int metadata_error = errno;
        throw_preparation_error(
                directory_kind, validation_stage,
                is_permission_error(metadata_error)
                        ? PreparationErrorCode::PermissionDenied
                        : PreparationErrorCode::MetadataFailure,
                metadata_error, component_index);
    }
    if(!same_filesystem_identity(observed_status, opened_status)) {
        throw_preparation_error(
                directory_kind, validation_stage,
                PreparationErrorCode::ConcurrentReplacement,
                std::nullopt, component_index);
    }

    struct stat revalidated_status {};
    if(fstatat(
               parent_descriptor, leaf_name.c_str(), &revalidated_status,
               AT_SYMLINK_NOFOLLOW) != 0) {
        const int metadata_error = errno;
        throw_preparation_error(
                directory_kind, validation_stage,
                is_replacement_error(metadata_error)
                        ? PreparationErrorCode::ConcurrentReplacement
                        : (is_permission_error(metadata_error)
                                   ? PreparationErrorCode::PermissionDenied
                                   : PreparationErrorCode::MetadataFailure),
                metadata_error, component_index);
    }
    if(!same_filesystem_identity(opened_status, revalidated_status)) {
        throw_preparation_error(
                directory_kind, validation_stage,
                PreparationErrorCode::ConcurrentReplacement,
                std::nullopt, component_index);
    }

    validate_directory_type(
            opened_status, directory_kind, validation_stage,
            component_index, true);
    if(validate_security) {
        validate_directory_security(
                opened_status, directory_kind, validation_stage,
                component_index, expected_effective_user, overrides,
                was_created);
        validate_directory_security(
                revalidated_status, directory_kind, validation_stage,
                component_index, expected_effective_user, overrides,
                was_created);
    }
    return OpenedDirectory{std::move(opened), opened_status};
}

OpenedDirectory reopen_absolute_directory(
        const fs::path& path,
        const fs::path& security_anchor,
        xdg_paths::DirectoryKind directory_kind,
        std::uintmax_t expected_owner,
        const TestOverrides* overrides,
        bool validate_security,
        const std::vector<struct stat>* expected_security_statuses) {
    const int root_descriptor = open(
            "/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(root_descriptor < 0) {
        const int open_error = errno;
        throw_preparation_error(
                directory_kind, PreparationStage::DirectoryRevalidation,
                is_permission_error(open_error)
                        ? PreparationErrorCode::PermissionDenied
                        : PreparationErrorCode::MetadataFailure,
                open_error);
    }
    OwnedFileDescriptor current_directory(root_descriptor);
    struct stat final_status {};

    fs::path current_path("/");
    std::size_t component_index = 0;
    std::optional<std::size_t> security_component_index;
    for(const auto& path_component : path.relative_path()) {
        const std::string leaf_name = path_component.string();
        struct stat observed_status {};
        if(fstatat(
                   current_directory.get(), leaf_name.c_str(),
                   &observed_status, AT_SYMLINK_NOFOLLOW) != 0) {
            const int metadata_error = errno;
            throw_preparation_error(
                    directory_kind,
                    PreparationStage::DirectoryRevalidation,
                    is_replacement_error(metadata_error)
                            ? PreparationErrorCode::ConcurrentReplacement
                            : (is_permission_error(metadata_error)
                                       ? PreparationErrorCode::PermissionDenied
                                       : PreparationErrorCode::MetadataFailure),
                    metadata_error, component_index);
        }
        current_path /= leaf_name;
        if(current_path == security_anchor) {
            security_component_index = 0;
        } else if(security_component_index.has_value()) {
            ++security_component_index.value();
        }
        validate_directory_type(
                observed_status, directory_kind,
                PreparationStage::DirectoryRevalidation,
                component_index, true);

        OpenedDirectory opened = open_observed_directory(
                current_directory.get(), leaf_name, observed_status,
                directory_kind, PreparationStage::DirectoryRevalidation,
                PreparationStage::DirectoryRevalidation,
                component_index, expected_owner, overrides,
                false,
                validate_security && security_component_index.has_value(),
                false);
        if(expected_security_statuses != nullptr &&
           security_component_index.has_value()) {
            const std::size_t security_index =
                    security_component_index.value();
            if(security_index >= expected_security_statuses->size() ||
               !same_filesystem_identity(
                       expected_security_statuses->at(security_index),
                       opened.status)) {
                throw_preparation_error(
                        directory_kind,
                        PreparationStage::DirectoryRevalidation,
                        PreparationErrorCode::ConcurrentReplacement,
                        std::nullopt, component_index);
            }
        }
        current_directory = std::move(opened.descriptor);
        final_status = opened.status;
        ++component_index;
    }
    if(validate_security && !security_component_index.has_value()) {
        throw_preparation_error(
                directory_kind, PreparationStage::DirectoryRevalidation,
                PreparationErrorCode::InvalidCreationBoundary);
    }
    if(expected_security_statuses != nullptr &&
       (!security_component_index.has_value() ||
        security_component_index.value() + 1 !=
                expected_security_statuses->size())) {
        throw_preparation_error(
                directory_kind, PreparationStage::DirectoryRevalidation,
                PreparationErrorCode::ConcurrentReplacement);
    }
    return OpenedDirectory{std::move(current_directory), final_status};
}

OpenedDirectory open_existing_anchor(
        const DirectoryRequest& request,
        std::uintmax_t expected_effective_user,
        const TestOverrides* overrides) {
    const int root_descriptor = open(
            "/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(root_descriptor < 0) {
        const int open_error = errno;
        throw_preparation_error(
                request.directory_kind,
                PreparationStage::FilesystemRootOpen,
                is_permission_error(open_error)
                        ? PreparationErrorCode::PermissionDenied
                        : PreparationErrorCode::MetadataFailure,
                open_error);
    }
    OwnedFileDescriptor current_directory(root_descriptor);
    struct stat current_status {};

    fs::path current_path("/");
    std::size_t component_index = 0;
    for(const auto& path_component :
        request.creation_boundary.existing_anchor.relative_path()) {
        const std::string leaf_name = path_component.string();
        struct stat observed_status {};
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
        const bool injected_metadata_failure =
                inject_failure(
                        overrides,
                        DirectorySafetyTestFailurePoint::AnchorMetadata,
                        component_index);
#else
        const bool injected_metadata_failure = false;
#endif
        if(injected_metadata_failure ||
           fstatat(
                   current_directory.get(), leaf_name.c_str(),
                   &observed_status, AT_SYMLINK_NOFOLLOW) != 0) {
            const int metadata_error = errno;
            if(metadata_error == ENOENT) {
                throw_preparation_error(
                        request.directory_kind,
                        PreparationStage::AnchorTraversal,
                        PreparationErrorCode::MissingAnchor,
                        std::nullopt, component_index);
            }
            throw_preparation_error(
                    request.directory_kind,
                    PreparationStage::AnchorTraversal,
                    is_permission_error(metadata_error)
                            ? PreparationErrorCode::PermissionDenied
                            : PreparationErrorCode::MetadataFailure,
                    metadata_error, component_index);
        }
        current_path /= leaf_name;
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
        emit_test_event(
                overrides, DirectorySafetyTestEvent::AfterAnchorMetadata,
                request.directory_kind, component_index, current_path);
#else
        emit_test_event(
                overrides, request.directory_kind, component_index,
                current_path);
#endif
        validate_directory_type(
                observed_status, request.directory_kind,
                PreparationStage::AnchorTraversal, component_index, false);

        const bool is_final_anchor_component =
                current_path ==
                request.creation_boundary.existing_anchor;
        OpenedDirectory opened = open_observed_directory(
                current_directory.get(), leaf_name, observed_status,
                request.directory_kind,
                PreparationStage::AnchorTraversal,
                is_final_anchor_component
                        ? PreparationStage::AnchorValidation
                        : PreparationStage::AnchorTraversal,
                component_index,
                expected_effective_user, overrides, false,
                is_final_anchor_component,
                false);
        current_directory = std::move(opened.descriptor);
        current_status = opened.status;
        ++component_index;
    }

    validate_directory_security(
            current_status, request.directory_kind,
            PreparationStage::AnchorValidation,
            component_index == 0 ? 0 : component_index - 1,
            expected_effective_user, overrides, false);
    return OpenedDirectory{std::move(current_directory), current_status};
}

std::optional<struct stat> inspect_managed_component(
        int parent_descriptor, const std::string& leaf_name,
        const DirectoryRequest& request, std::size_t component_index,
        const TestOverrides* overrides) {
#ifndef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    static_cast<void>(overrides);
#endif
    struct stat observed_status {};
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    const bool injected_metadata_failure =
            inject_failure(
                    overrides,
                    DirectorySafetyTestFailurePoint::ManagedMetadata,
                    component_index);
#else
    const bool injected_metadata_failure = false;
#endif
    if(!injected_metadata_failure &&
       fstatat(
               parent_descriptor, leaf_name.c_str(), &observed_status,
               AT_SYMLINK_NOFOLLOW) == 0) {
        return observed_status;
    }

    const int metadata_error = errno;
    if(metadata_error == ENOENT) return std::nullopt;
    throw_preparation_error(
            request.directory_kind,
            PreparationStage::ComponentInspection,
            is_permission_error(metadata_error)
                    ? PreparationErrorCode::PermissionDenied
                    : PreparationErrorCode::MetadataFailure,
            metadata_error, component_index);
}

PreparedDirectoryState prepare_directory_state(
        const DirectoryRequest& request, const TestOverrides* overrides) {
    validate_creation_boundary(request);
    const std::uintmax_t expected_effective_user =
            effective_user(overrides);
    OpenedDirectory anchor = open_existing_anchor(
            request, expected_effective_user, overrides);
    OwnedFileDescriptor current_directory = std::move(anchor.descriptor);
    std::vector<struct stat> security_statuses{anchor.status};

    fs::path current_path = request.creation_boundary.existing_anchor;
    std::size_t created_component_count = 0;
    OwnedFileDescriptor final_parent;
    struct stat final_status {};
    std::uintmax_t final_observed_owner = 0;

    for(std::size_t component_index = 0;
        component_index <
                request.creation_boundary.creatable_components.size();
        ++component_index) {
        const std::string& leaf_name =
                request.creation_boundary
                        .creatable_components[component_index];
        current_path /= leaf_name;
        std::optional<struct stat> observed_status =
                inspect_managed_component(
                        current_directory.get(), leaf_name, request,
                        component_index, overrides);
        bool was_created = false;
        bool appeared_concurrently = false;

        if(!observed_status.has_value()) {
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
            const bool injected_creation_failure =
                    inject_failure(
                            overrides,
                            DirectorySafetyTestFailurePoint::ComponentCreation,
                            component_index);
#else
            const bool injected_creation_failure = false;
#endif
            if(injected_creation_failure ||
               mkdirat(
                       current_directory.get(), leaf_name.c_str(),
                       NEW_DIRECTORY_MODE) != 0) {
                const int creation_error = errno;
                if(creation_error != EEXIST) {
                    throw_preparation_error(
                            request.directory_kind,
                            PreparationStage::ComponentCreation,
                            is_permission_error(creation_error)
                                    ? PreparationErrorCode::PermissionDenied
                                    : (is_replacement_error(creation_error)
                                               ? PreparationErrorCode::ConcurrentReplacement
                                               : PreparationErrorCode::CreationFailed),
                            creation_error, component_index);
                }
                appeared_concurrently = true;
            } else {
                was_created = true;
                ++created_component_count;
#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
                emit_test_event(
                        overrides,
                        DirectorySafetyTestEvent::AfterComponentCreation,
                        request.directory_kind, component_index,
                        current_path);
#else
                emit_test_event(
                        overrides, request.directory_kind,
                        component_index, current_path);
#endif
            }

            observed_status = inspect_managed_component(
                    current_directory.get(), leaf_name, request,
                    component_index, overrides);
            if(!observed_status.has_value()) {
                throw_preparation_error(
                        request.directory_kind,
                        PreparationStage::ComponentValidation,
                        PreparationErrorCode::ConcurrentReplacement,
                        std::nullopt, component_index);
            }
        }

#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
        emit_test_event(
                overrides, DirectorySafetyTestEvent::AfterManagedMetadata,
                request.directory_kind, component_index, current_path);
#else
        emit_test_event(
                overrides, request.directory_kind, component_index,
                current_path);
#endif
        validate_directory_type(
                observed_status.value(), request.directory_kind,
                PreparationStage::ComponentValidation, component_index,
                was_created || appeared_concurrently);
        validate_directory_security(
                observed_status.value(), request.directory_kind,
                PreparationStage::ComponentValidation, component_index,
                expected_effective_user, overrides, was_created);

        OpenedDirectory opened = open_observed_directory(
                current_directory.get(), leaf_name,
                observed_status.value(), request.directory_kind,
                PreparationStage::ComponentOpen,
                PreparationStage::ComponentValidation, component_index,
                expected_effective_user, overrides, true, true,
                was_created);

        const bool is_final_component =
                component_index + 1 ==
                request.creation_boundary.creatable_components.size();
        if(is_final_component) {
            final_parent = std::move(current_directory);
            final_status = opened.status;
            final_observed_owner = observed_owner(opened.status, overrides);
        }
        current_directory = std::move(opened.descriptor);
        security_statuses.push_back(opened.status);
    }

    OpenedDirectory named_directory = reopen_absolute_directory(
            request.directory, request.creation_boundary.existing_anchor,
            request.directory_kind, expected_effective_user, overrides,
            true, &security_statuses);
    if(!same_filesystem_identity(final_status, named_directory.status)) {
        throw_preparation_error(
                request.directory_kind,
                PreparationStage::DirectoryRevalidation,
                PreparationErrorCode::ConcurrentReplacement);
    }

    return PreparedDirectoryState{
            std::move(final_parent), std::move(current_directory),
            request.creation_boundary.creatable_components.back(),
            final_status, final_observed_owner,
            created_component_count};
}

std::uintmax_t status_permissions(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_mode & 07777);
}

std::uintmax_t status_device(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_dev);
}

std::uintmax_t status_inode(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_ino);
}

DirectoryRequest make_request(const xdg_paths::ConfigPaths& paths) {
    return DirectoryRequest{
            xdg_paths::DirectoryKind::Config, paths.directory,
            paths.creation_boundary,
            paths.config_file == paths.directory / "config.toml"};
}

DirectoryRequest make_request(const xdg_paths::StatePaths& paths) {
    fs::path expected_log_file =
            paths.directory /
            std::string(application_identity::XDG_IDENTITY);
    expected_log_file += ".log";
    return DirectoryRequest{
            xdg_paths::DirectoryKind::State, paths.directory,
            paths.creation_boundary,
            paths.default_log_file == expected_log_file};
}

DirectoryRequest make_request(const xdg_paths::CachePaths& paths) {
    return DirectoryRequest{
            xdg_paths::DirectoryKind::Cache, paths.directory,
            paths.creation_boundary, true};
}

} // namespace

PreparationError::PreparationError(PreparationFailure failure)
    : std::runtime_error(preparation_diagnostic(failure)),
      failure_(failure) {
}

PreparedDirectory::PreparedDirectory(
        xdg_paths::DirectoryKind directory_kind, fs::path path,
        fs::path security_anchor,
        int parent_descriptor, int directory_descriptor,
        std::string leaf_name, std::uintmax_t device,
        std::uintmax_t inode, std::uintmax_t owner,
        std::uintmax_t filesystem_owner, std::uintmax_t permissions,
        std::size_t created_component_count) noexcept
    : directory_kind_(directory_kind), path_(std::move(path)),
      security_anchor_(std::move(security_anchor)),
      parent_descriptor_(parent_descriptor),
      directory_descriptor_(directory_descriptor),
      leaf_name_(std::move(leaf_name)), device_(device), inode_(inode),
      owner_(owner), filesystem_owner_(filesystem_owner),
      permissions_(permissions),
      created_component_count_(created_component_count) {
}

PreparedDirectory::PreparedDirectory(PreparedDirectory&& other) noexcept
    : directory_kind_(other.directory_kind_), path_(std::move(other.path_)),
      security_anchor_(std::move(other.security_anchor_)),
      parent_descriptor_(std::exchange(other.parent_descriptor_, -1)),
      directory_descriptor_(std::exchange(other.directory_descriptor_, -1)),
      leaf_name_(std::move(other.leaf_name_)), device_(other.device_),
      inode_(other.inode_), owner_(other.owner_),
      filesystem_owner_(other.filesystem_owner_),
      permissions_(other.permissions_),
      created_component_count_(other.created_component_count_) {
}

PreparedDirectory::~PreparedDirectory() noexcept {
    if(directory_descriptor_ >= 0)
        static_cast<void>(close(directory_descriptor_));
    if(parent_descriptor_ >= 0)
        static_cast<void>(close(parent_descriptor_));
}

struct DirectorySafetyAccess {
    static PreparedDirectory prepare(
            const DirectoryRequest& request,
            const TestOverrides* overrides) {
        PreparedDirectoryState state =
                prepare_directory_state(request, overrides);
        fs::path prepared_path = request.directory;
        fs::path security_anchor =
                request.creation_boundary.existing_anchor;
        PreparedDirectory prepared(
                request.directory_kind, std::move(prepared_path),
                std::move(security_anchor),
                state.parent_descriptor.get(),
                state.directory_descriptor.get(),
                std::move(state.leaf_name), status_device(state.status),
                status_inode(state.status), state.observed_owner,
                static_cast<std::uintmax_t>(state.status.st_uid),
                status_permissions(state.status),
                state.created_component_count);
        static_cast<void>(state.parent_descriptor.release());
        static_cast<void>(state.directory_descriptor.release());
        return prepared;
    }
};

void PreparedDirectory::require_unchanged_identity() const {
    if(parent_descriptor_ < 0 || directory_descriptor_ < 0) {
        throw_preparation_error(
                directory_kind_, PreparationStage::DirectoryRevalidation,
                PreparationErrorCode::MetadataFailure);
    }

    struct stat retained_status {};
    if(fstat(directory_descriptor_, &retained_status) != 0) {
        const int metadata_error = errno;
        throw_preparation_error(
                directory_kind_, PreparationStage::DirectoryRevalidation,
                is_permission_error(metadata_error)
                        ? PreparationErrorCode::PermissionDenied
                        : PreparationErrorCode::MetadataFailure,
                metadata_error);
    }

    struct stat named_status {};
    if(fstatat(
               parent_descriptor_, leaf_name_.c_str(), &named_status,
               AT_SYMLINK_NOFOLLOW) != 0) {
        const int metadata_error = errno;
        throw_preparation_error(
                directory_kind_, PreparationStage::DirectoryRevalidation,
                is_replacement_error(metadata_error)
                        ? PreparationErrorCode::ConcurrentReplacement
                        : (is_permission_error(metadata_error)
                                   ? PreparationErrorCode::PermissionDenied
                                   : PreparationErrorCode::MetadataFailure),
                metadata_error);
    }

    if(!S_ISDIR(retained_status.st_mode) ||
       !same_filesystem_identity(retained_status, named_status) ||
       status_device(retained_status) != device_ ||
       status_inode(retained_status) != inode_) {
        throw_preparation_error(
                directory_kind_, PreparationStage::DirectoryRevalidation,
                PreparationErrorCode::ConcurrentReplacement);
    }

    const auto validate_current_security = [this](const struct stat& status) {
        if(static_cast<std::uintmax_t>(status.st_uid) != filesystem_owner_) {
            throw_preparation_error(
                    directory_kind_,
                    PreparationStage::DirectoryRevalidation,
                    PreparationErrorCode::OwnershipMismatch);
        }
        const mode_t permissions = status.st_mode & 07777;
        if((permissions & REQUIRED_OWNER_PERMISSIONS) !=
                   REQUIRED_OWNER_PERMISSIONS ||
           (permissions & FORBIDDEN_WRITE_PERMISSIONS) != 0) {
            throw_preparation_error(
                    directory_kind_,
                    PreparationStage::DirectoryRevalidation,
                    PreparationErrorCode::UnsafePermissions);
        }
    };
    validate_current_security(retained_status);
    validate_current_security(named_status);

    OpenedDirectory absolute_directory = reopen_absolute_directory(
            path_, security_anchor_, directory_kind_, filesystem_owner_,
            nullptr, true, nullptr);
    if(!same_filesystem_identity(
               retained_status, absolute_directory.status)) {
        throw_preparation_error(
                directory_kind_, PreparationStage::DirectoryRevalidation,
                PreparationErrorCode::ConcurrentReplacement);
    }
}

PreparedDirectory prepare_directory(const xdg_paths::ConfigPaths& paths) {
    const DirectoryRequest request = make_request(paths);
    return DirectorySafetyAccess::prepare(request, nullptr);
}

PreparedDirectory prepare_directory(const xdg_paths::StatePaths& paths) {
    const DirectoryRequest request = make_request(paths);
    return DirectorySafetyAccess::prepare(request, nullptr);
}

PreparedDirectory prepare_directory(const xdg_paths::CachePaths& paths) {
    const DirectoryRequest request = make_request(paths);
    return DirectorySafetyAccess::prepare(request, nullptr);
}

#ifdef MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
PreparedDirectory prepare_directory_for_test(
        const xdg_paths::ConfigPaths& paths,
        const DirectorySafetyTestOverrides& overrides) {
    return DirectorySafetyAccess::prepare(make_request(paths), &overrides);
}

PreparedDirectory prepare_directory_for_test(
        const xdg_paths::StatePaths& paths,
        const DirectorySafetyTestOverrides& overrides) {
    return DirectorySafetyAccess::prepare(make_request(paths), &overrides);
}

PreparedDirectory prepare_directory_for_test(
        const xdg_paths::CachePaths& paths,
        const DirectorySafetyTestOverrides& overrides) {
    return DirectorySafetyAccess::prepare(make_request(paths), &overrides);
}
#endif

} // namespace xdg_directory_safety
