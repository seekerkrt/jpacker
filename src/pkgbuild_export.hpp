#pragma once

#include <string>

// AUR PackageBase repositoryをcommand開始時のcwdへatomicにexportする。
void export_pkgbuild_tree(const std::string& target);

// Temporary checkoutのcleanup完了後に、PKGBUILDのbytesだけを返す。
std::string load_pkgbuild_for_stdout(const std::string& target);
