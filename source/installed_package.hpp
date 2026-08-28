#pragma once

#include <string>

// installed packageのidentityを、外部sessionに依存しないowned valueで表す。
enum class InstalledPackageReason {
    Explicit,
    Dependency,
    Unknown,
};

struct InstalledPackageMetadata {
    std::string name;
    std::string version;
    InstalledPackageReason reason = InstalledPackageReason::Unknown;
};
