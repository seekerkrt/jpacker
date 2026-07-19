#pragma once

#include <set>
#include <string>
#include <vector>

// pacman local database から読んだ installed package の最小情報。
struct InstalledPackage {
    std::string name;
    std::string version;
};

// 依存名を満たす provider package と、その所属 repository。
struct ProvidedDependency {
    std::string repository;
    std::string package_name;
};

bool is_installed_package(const std::string& pkg_name);
bool is_repo_package(const std::string& pkg_name);
std::vector<ProvidedDependency> find_repo_providers(const std::string& dependency_name);
std::vector<InstalledPackage> get_foreign_packages();
std::set<std::string> get_foreign_package_names();
