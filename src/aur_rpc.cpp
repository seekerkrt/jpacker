#include "aur_rpc.hpp"

#include "dependency_spec.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"

#include <cstddef>
#include <cstdlib>
#include <curl/curl.h>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#ifndef JPKG_VERSION
#define JPKG_VERSION "unknown"
#endif

namespace {

using json = nlohmann::json;

const std::string VERSION = JPKG_VERSION;
const std::string AUR_RPC_DEFAULT_BASE_URL = "https://aur.archlinux.org/rpc/";
const std::string USER_AGENT = "jpacker/" + VERSION;

// CURL easy handle の確保と解放を 1 request の寿命に束ねる RAII wrapper。
class CurlHandle {
    CURL* curl_;

public:
    CurlHandle() {
        curl_ = curl_easy_init();
        if(!curl_) throw std::runtime_error("Failed to initialize cURL handle.");
    }
    ~CurlHandle() {
        if(curl_) curl_easy_cleanup(curl_);
    }
    CURL* get() const {
        return curl_;
    }
};

// AUR parserをmonolithへ逆依存させず、汎用utilityを公開しないためのlocal helper。
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// AUR RPC / JSON解析
std::string aur_rpc_base_url() {
#ifdef JPACKER_ENABLE_TEST_OVERRIDES
    // POLICY: local fixture injection は isolated test binary 限定。production の endpoint は固定する。
    const char* test_base_url = std::getenv("JPACKER_TEST_AUR_RPC_BASE_URL");
    if(test_base_url && test_base_url[0] != '\0') {
        std::string base_url = test_base_url;
        if(base_url.back() != '/') base_url += '/';
        return base_url;
    }
#endif
    return AUR_RPC_DEFAULT_BASE_URL;
}

std::string aur_rpc_search_url() {
    return aur_rpc_base_url() + "v5/search/";
}

std::string aur_rpc_info_url() {
    return aur_rpc_base_url() + "?v=5&type=info&arg%5B%5D=";
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto*  buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), total_size);
    return total_size;
}

json parse_aur_rpc_response(const std::string& response, const std::string& context) {
    json parsed;
    try {
        parsed = json::parse(response);
    } catch(const json::exception& e) {
        throw AurRpcResponseError(
                "AUR RPC response parse failed for " + context + ": " + std::string(e.what()));
    }

    if(!parsed.is_object()) {
        throw AurRpcResponseError(
                "AUR RPC response validation failed for " + context +
                ": expected top-level object, got " + parsed.type_name());
    }
    return parsed;
}

const json& aur_rpc_results_array(const json& response, const std::string& context) {
    auto results = response.find("results");
    if(results == response.end()) {
        throw AurRpcResponseError(
                "AUR RPC response validation failed for " + context +
                ": field results expected array, got missing");
    }
    if(!results->is_array()) {
        throw AurRpcResponseError(
                "AUR RPC response validation failed for " + context +
                ": field results expected array, got " + std::string(results->type_name()));
    }
    return *results;
}

[[noreturn]] void throw_aur_rpc_validation_error(
        const std::string& context, const std::string& detail) {
    throw AurRpcResponseError("AUR RPC response validation failed for " + context + ": " + detail);
}

std::string json_value_for_error(const std::string& value) {
    return json(value).dump();
}

std::string aur_rpc_result_context(
        const std::string& context, size_t result_index) {
    return context + " result[" + std::to_string(result_index) + "]";
}

std::string required_json_string(
        const json& obj, const std::string& key, const std::string& context) {
    auto value = obj.find(key);
    if(value == obj.end()) {
        throw_aur_rpc_validation_error(context, "field " + key + " expected string, got missing");
    }
    if(!value->is_string()) {
        throw_aur_rpc_validation_error(
                context, "field " + key + " expected string, got " + std::string(value->type_name()));
    }

    std::string result = value->get<std::string>();
    if(trim(result).empty()) {
        throw_aur_rpc_validation_error(
                context, "field " + key + " expected non-empty string, got empty or whitespace-only string");
    }
    return result;
}

std::string optional_json_string(
        const json& obj, const std::string& key, const std::string& context) {
    auto value = obj.find(key);
    if(value == obj.end() || value->is_null()) return "";
    if(!value->is_string()) {
        throw_aur_rpc_validation_error(
                context, "field " + key + " expected string or null, got " +
                                 std::string(value->type_name()));
    }
    return value->get<std::string>();
}

std::optional<long long> optional_json_integer(
        const json& obj, const std::string& key, const std::string& context) {
    auto value = obj.find(key);
    if(value == obj.end() || value->is_null()) return std::nullopt;
    if(!value->is_number_integer()) {
        throw_aur_rpc_validation_error(
                context, "field " + key + " expected integer or null, got " +
                                 std::string(value->type_name()));
    }

    if(value->is_number_unsigned()) {
        auto unsigned_value = value->get<unsigned long long>();
        if(unsigned_value > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
            throw_aur_rpc_validation_error(
                    context, "field " + key + " integer is outside supported range");
        }
        return static_cast<long long>(unsigned_value);
    }
    return value->get<long long>();
}

std::vector<std::string> optional_json_string_array(
        const json& obj, const std::string& key, const std::string& context) {
    auto value = obj.find(key);
    if(value == obj.end() || value->is_null()) return {};
    if(!value->is_array()) {
        throw_aur_rpc_validation_error(
                context, "field " + key + " expected array or null, got " +
                                 std::string(value->type_name()));
    }

    std::vector<std::string> values;
    values.reserve(value->size());
    for(size_t i = 0; i < value->size(); ++i) {
        const json& item = (*value)[i];
        if(!item.is_string()) {
            throw_aur_rpc_validation_error(
                    context, "field " + key + "[" + std::to_string(i) +
                                     "] expected string, got " + item.type_name());
        }
        values.push_back(item.get<std::string>());
    }
    return values;
}

void validate_package_identifier(
        const std::string& value, const std::string& field, const std::string& context) {
    if(!is_valid_package_name(value)) {
        throw_aur_rpc_validation_error(
                context, "invalid " + field + " " + json_value_for_error(value));
    }
}

void validate_metadata_identifiers(
        const std::vector<std::string>& values, const std::string& field,
        const std::string& context) {
    for(size_t i = 0; i < values.size(); ++i) {
        ParsedDependency parsed = parse_dependency_string(values[i]);
        if(!is_valid_package_name(parsed.name)) {
            throw_aur_rpc_validation_error(
                    context, "field " + field + "[" + std::to_string(i) +
                                     "] contains invalid package identifier " +
                                     json_value_for_error(parsed.name));
        }
    }
}

AurPackageInfo parse_aur_rpc_package_info(
        const json& pkg, const std::string& context, size_t result_index) {
    std::string entry_context = aur_rpc_result_context(context, result_index);
    if(!pkg.is_object()) {
        throw_aur_rpc_validation_error(
                entry_context, "package entry expected object, got " + std::string(pkg.type_name()));
    }

    AurPackageInfo info;
    info.Name = required_json_string(pkg, "Name", entry_context);
    validate_package_identifier(info.Name, "Name", entry_context);
    entry_context += " (package " + json_value_for_error(info.Name) + ")";

    info.PackageBase = required_json_string(pkg, "PackageBase", entry_context);
    validate_package_identifier(info.PackageBase, "PackageBase", entry_context);
    info.Version = required_json_string(pkg, "Version", entry_context);
    info.Description = optional_json_string(pkg, "Description", entry_context);
    info.Maintainer = optional_json_string(pkg, "Maintainer", entry_context);
    info.OutOfDate = optional_json_integer(pkg, "OutOfDate", entry_context);

    info.Depends = optional_json_string_array(pkg, "Depends", entry_context);
    info.MakeDepends = optional_json_string_array(pkg, "MakeDepends", entry_context);
    info.CheckDepends = optional_json_string_array(pkg, "CheckDepends", entry_context);
    info.OptDepends = optional_json_string_array(pkg, "OptDepends", entry_context);
    info.Provides = optional_json_string_array(pkg, "Provides", entry_context);
    info.Conflicts = optional_json_string_array(pkg, "Conflicts", entry_context);
    info.Replaces = optional_json_string_array(pkg, "Replaces", entry_context);

    // POLICY(#174): OptDepends は `pkg: description` を許すため型だけを検証する。
    validate_metadata_identifiers(info.Depends, "Depends", entry_context);
    validate_metadata_identifiers(info.MakeDepends, "MakeDepends", entry_context);
    validate_metadata_identifiers(info.CheckDepends, "CheckDepends", entry_context);
    validate_metadata_identifiers(info.Provides, "Provides", entry_context);
    validate_metadata_identifiers(info.Conflicts, "Conflicts", entry_context);
    validate_metadata_identifiers(info.Replaces, "Replaces", entry_context);
    return info;
}

std::vector<AurPackageInfo> parse_aur_rpc_package_results(
        const std::string& response, const std::string& context) {
    json        parsed = parse_aur_rpc_response(response, context);
    const json& results = aur_rpc_results_array(parsed, context);

    std::vector<AurPackageInfo> packages;
    packages.reserve(results.size());
    for(size_t i = 0; i < results.size(); ++i) {
        packages.push_back(parse_aur_rpc_package_info(results[i], context, i));
    }
    return packages;
}

std::optional<AurPackageInfo> parse_single_aur_info_response(
        const std::string& response, const std::string& pkg_name) {
    std::string                 context = "package info " + pkg_name;
    std::vector<AurPackageInfo> results = parse_aur_rpc_package_results(response, context);
    if(results.empty()) return std::nullopt;
    if(results.size() != 1) {
        throw_aur_rpc_validation_error(
                context, "expected zero or one result, got " + std::to_string(results.size()));
    }
    if(results.front().Name != pkg_name) {
        throw_aur_rpc_validation_error(
                context, "requested " + pkg_name + " but response Name was " + results.front().Name);
    }
    return results.front();
}

} // namespace

CurlGlobal::CurlGlobal() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlGlobal::~CurlGlobal() {
    curl_global_cleanup();
}

namespace {

std::string get_url_strict(const std::string& url) {
    CurlHandle  handle;
    std::string readBuffer;
    char        errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, USER_AGENT.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(handle.get(), CURLOPT_FAILONERROR, 1L);
    CURLcode res = curl_easy_perform(handle.get());
    if(res != CURLE_OK) {
        std::string error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(res);
        throw std::runtime_error("AUR request failed: " + error);
    }

    long response_code = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &response_code);
    if(response_code < 200 || response_code >= 300) {
        throw std::runtime_error(
                "AUR request failed with HTTP status " + std::to_string(response_code) + ".");
    }
    if(readBuffer.empty()) {
        throw std::runtime_error("AUR request returned an empty response.");
    }
    return readBuffer;
}

std::string get_url(const std::string& url) {
    CurlHandle  handle;
    std::string readBuffer;
    char        errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, USER_AGENT.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, errorBuffer);
    CURLcode res = curl_easy_perform(handle.get());
    if(res != CURLE_OK) {
        // NOTE: 呼び出し側は空 response を「取得不能/未検出」として扱い、CLI 境界で文脈付きに変換する。
        std::string error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(res);
        Logger::warn("AUR request failed: " + error);
        return "";
    }
    return readBuffer;
}

} // namespace

std::vector<AurPackageInfo> AurClient::search(const std::string& query) {
    std::vector<AurPackageInfo> packages;
    CurlHandle  handle;
    char* escaped = curl_easy_escape(handle.get(), query.c_str(), static_cast<int>(query.length()));
    if(!escaped) return packages;
    std::string url = aur_rpc_search_url() + escaped;
    curl_free(escaped);

    std::string response = get_url(url);
    if(response.empty()) return packages;
    return parse_aur_rpc_package_results(response, "search query " + query);
}

std::vector<std::string> AurClient::search_names_by_provides(const std::string& provided_name) {
    std::vector<std::string> names;
    CurlHandle               handle;
    char* escaped = curl_easy_escape(handle.get(), provided_name.c_str(), static_cast<int>(provided_name.length()));
    if(!escaped) return names;
    std::string url = aur_rpc_search_url() + escaped + "?by=provides";
    curl_free(escaped);

    std::string response = get_url(url);
    if(response.empty()) return names;

    std::vector<AurPackageInfo> results =
            parse_aur_rpc_package_results(response, "provides search " + provided_name);
    for(const auto& info : results) {
        names.push_back(info.Name);
    }
    return names;
}

std::optional<AurPackageInfo> AurClient::info(const std::string& pkg_name) {
    CurlHandle handle;
    char*      escaped = curl_easy_escape(handle.get(), pkg_name.c_str(), static_cast<int>(pkg_name.length()));
    if(!escaped) return std::nullopt;
    std::string url = aur_rpc_info_url() + escaped;
    curl_free(escaped);

    std::string response = get_url(url);
    if(response.empty()) return std::nullopt;

    return parse_single_aur_info_response(response, pkg_name);
}

std::optional<AurPackageInfo> AurClient::info_strict(const std::string& pkg_name) {
    CurlHandle handle;
    char*      escaped = curl_easy_escape(
            handle.get(), pkg_name.c_str(), static_cast<int>(pkg_name.length()));
    if(!escaped) {
        throw std::runtime_error("Failed to encode AUR package name: " + pkg_name);
    }
    std::string url = aur_rpc_info_url() + escaped;
    curl_free(escaped);

    std::string response = get_url_strict(url);
    return parse_single_aur_info_response(response, pkg_name);
}

std::map<std::string, AurPackageInfo> AurClient::info_many(const std::vector<std::string>& pkg_names) {
    std::map<std::string, AurPackageInfo> results;
    if(pkg_names.empty()) return results;

    std::set<std::string> requested_names;
    for(const auto& pkg_name : pkg_names) {
        require_valid_package_name(pkg_name);
        requested_names.insert(pkg_name);
    }

    CurlHandle  handle;
    std::string url = aur_rpc_base_url() + "?v=5&type=info";
    bool        has_arg = false;
    for(size_t i = 0; i < pkg_names.size(); ++i) {
        char* escaped = curl_easy_escape(handle.get(), pkg_names[i].c_str(), static_cast<int>(pkg_names[i].length()));
        if(!escaped) continue;
        url += "&";
        url += "arg%5B%5D=";
        url += escaped;
        has_arg = true;
        curl_free(escaped);
    }
    if(!has_arg) return results;

    std::string response = get_url(url);
    if(response.empty()) return results;

    std::string                 context = "multiinfo";
    std::vector<AurPackageInfo> aur_results = parse_aur_rpc_package_results(response, context);
    for(const auto& pkg_info : aur_results) {
        if(!requested_names.contains(pkg_info.Name)) {
            throw_aur_rpc_validation_error(
                    context, "response Name " + pkg_info.Name + " was not requested");
        }
        if(!results.emplace(pkg_info.Name, pkg_info).second) {
            throw_aur_rpc_validation_error(
                    context, "duplicate response Name " + pkg_info.Name);
        }
    }
    return results;
}
