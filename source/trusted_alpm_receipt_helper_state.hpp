#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>

class TrustedAlpmReceiptStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The caller supplies an already-opened runtime parent. The installed helper
// always passes a descriptor for the fixed /run directory and expected uid 0;
// tests may use an isolated descriptor without adding a root CLI path option.
class TrustedAlpmReceiptStateStore final {
public:
    [[nodiscard]] static TrustedAlpmReceiptStateStore open_below_runtime_parent(
        int runtime_parent_fd, uid_t expected_owner);

    TrustedAlpmReceiptStateStore(const TrustedAlpmReceiptStateStore&) = delete;
    TrustedAlpmReceiptStateStore(TrustedAlpmReceiptStateStore&&) noexcept;
    TrustedAlpmReceiptStateStore& operator=(
        const TrustedAlpmReceiptStateStore&) = delete;
    TrustedAlpmReceiptStateStore& operator=(
        TrustedAlpmReceiptStateStore&&) noexcept;
    ~TrustedAlpmReceiptStateStore();

    void prepare(
        const std::string& transaction_token,
        const std::vector<std::string>& requested_package_names);
    void record(
        const std::string& transaction_token,
        int needs_targets_input_fd);
    [[nodiscard]] std::string consume(
        const std::string& transaction_token);
    void abort(const std::string& transaction_token);

private:
    struct Implementation;

    explicit TrustedAlpmReceiptStateStore(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};
