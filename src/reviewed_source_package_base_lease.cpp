#include "reviewed_source_pinned_build.hpp"

#include "process.hpp"

#include <cerrno>
#include <stdexcept>
#include <sys/file.h>
#include <system_error>
#include <utility>

ReviewedSourcePackageBaseLease::ReviewedSourcePackageBaseLease(
        RetainedTrustedCacheDirectory directory,
        int descriptor) noexcept
    : directory_(std::move(directory)), descriptor_(descriptor) {}

ReviewedSourcePackageBaseLease::ReviewedSourcePackageBaseLease(
        ReviewedSourcePackageBaseLease&& other) noexcept
    : directory_(std::move(other.directory_)),
      descriptor_(std::exchange(other.descriptor_, -1)),
      valid_(std::exchange(other.valid_, false)) {}

ReviewedSourcePackageBaseLease::~ReviewedSourcePackageBaseLease() noexcept =
        default;

bool ReviewedSourcePackageBaseLease::valid() const noexcept {
    return valid_ && descriptor_ >= 0;
}

const ValidatedCachePath& ReviewedSourcePackageBaseLease::path() const {
    if(!valid()) {
        throw std::logic_error(
                "A moved-from PackageBase lease has no authority.");
    }
    return directory_.path();
}

std::uintmax_t ReviewedSourcePackageBaseLease::device() const {
    return path().device();
}

std::uintmax_t ReviewedSourcePackageBaseLease::inode() const {
    return path().inode();
}

void ReviewedSourcePackageBaseLease::require_unchanged_identity() const {
    if(!valid()) {
        throw std::logic_error(
                "A moved-from PackageBase lease has no authority.");
    }
    directory_.require_unchanged_identity();
}

int ReviewedSourcePackageBaseLease::run_guarded_command(
        const std::string& command,
        const std::string& display_command) const {
    require_unchanged_identity();
    const int status =
            run_command_with_parent_independent_lifetime_guard(
                    command, descriptor_, display_command);
    require_unchanged_identity();
    return status;
}

ReviewedSourcePackageBaseLease
acquire_reviewed_source_package_base_lease(
        RetainedTrustedCacheDirectory directory) {
    directory.require_unchanged_identity();
    const int descriptor = directory.descriptor_;
    int lock_result;
    do {
        lock_result = ::flock(descriptor, LOCK_EX | LOCK_NB);
    } while(lock_result != 0 && errno == EINTR);
    if(lock_result != 0) {
        throw std::system_error(
                errno, std::generic_category(),
                "Failed to acquire reviewed source PackageBase lease");
    }
    directory.require_unchanged_identity();
    return ReviewedSourcePackageBaseLease(
            std::move(directory), descriptor);
}
