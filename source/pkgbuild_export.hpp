#pragma once

#include <optional>
#include <string>

// AUR PackageBase repositoryを安全にanchorしたparentへatomicにexportする。
// nulloptはcommand開始時cwdを表し、明示値は既存parentだけを受理する。
void export_pkgbuild_tree(
        const std::string& target,
        const std::optional<std::string>& output_directory);

// Temporary checkoutのcleanup完了後に、PKGBUILDのbytesだけを返す。
std::string load_pkgbuild_for_stdout(const std::string& target);
