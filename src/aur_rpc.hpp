#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// AUR RPC の package info response を、依存解決や表示で扱いやすくした型。
// NOTE: メンバ名は AUR RPC JSON key と 1:1 で対応させるため PascalCase のまま維持する。
struct AurPackageInfo {
    std::string              Name;
    std::string              PackageBase;
    std::string              Version;
    std::string              Description;
    std::vector<std::string> Depends;
    std::vector<std::string> MakeDepends;
    std::vector<std::string> CheckDepends;
    std::vector<std::string> OptDepends;
    std::vector<std::string> Provides;
    std::vector<std::string> Conflicts;
    std::vector<std::string> Replaces;
    std::string              Maintainer;
    std::optional<long long> OutOfDate;
};

// AUR RPC の parse/schema/semantic violation。transport failure や not-found と区別して伝播する。
class AurRpcResponseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// libcurl の global init/cleanup を 1 実行の寿命に束ねる RAII guard。
class CurlGlobal {
public:
    CurlGlobal();
    ~CurlGlobal();
};

// AUR RPC access をtyped queryへ閉じ込め、raw responseやJSON型をconsumerへ出さない。
class AurClient {
public:
    static std::vector<AurPackageInfo> search(const std::string& query);
    static std::vector<std::string> search_names_by_provides(const std::string& provided_name);
    static std::optional<AurPackageInfo> info(const std::string& pkg_name);
    static std::optional<AurPackageInfo> info_strict(const std::string& pkg_name);
    static std::map<std::string, AurPackageInfo> info_many(const std::vector<std::string>& pkg_names);
};
