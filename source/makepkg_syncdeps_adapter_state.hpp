#pragma once

#include "makepkg_syncdeps_adapter_protocol.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>

class MakepkgSyncdepsAdapterStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class MakepkgSyncdepsPidfd final {
public:
    [[nodiscard]] static MakepkgSyncdepsPidfd open(pid_t pid);

    MakepkgSyncdepsPidfd(const MakepkgSyncdepsPidfd&) = delete;
    MakepkgSyncdepsPidfd(MakepkgSyncdepsPidfd&&) noexcept;
    MakepkgSyncdepsPidfd& operator=(const MakepkgSyncdepsPidfd&) = delete;
    MakepkgSyncdepsPidfd& operator=(MakepkgSyncdepsPidfd&&) noexcept;
    ~MakepkgSyncdepsPidfd();

    [[nodiscard]] int descriptor() const noexcept;
    [[nodiscard]] const MakepkgSyncdepsPidfdIdentity& identity()
        const noexcept;
    [[nodiscard]] bool is_alive() const;

private:
    MakepkgSyncdepsPidfd(
        int descriptor, MakepkgSyncdepsPidfdIdentity identity) noexcept;

    int descriptor_ = -1;
    MakepkgSyncdepsPidfdIdentity identity_;
};

struct MakepkgSyncdepsObservedProcess {
    MakepkgSyncdepsPidfd pidfd;
    pid_t parent_pid = -1;
    pid_t tracer_pid = -1;
    MakepkgSyncdepsInstalledExecutableIdentity executable;
};

struct MakepkgSyncdepsRetainedProcessObservation {
    pid_t parent_pid = -1;
    pid_t tracer_pid = -1;
    std::uint32_t uid = 0;
    MakepkgSyncdepsInstalledExecutableIdentity executable;
};

// Observation opens pidfd before and after procfs reads and rejects a changed
// handle. There is no PID-only or timestamp-only fallback.
[[nodiscard]] MakepkgSyncdepsObservedProcess
observe_makepkg_syncdeps_process(pid_t pid);

// Request authorization uses a root-retained pidfd and atomic SCM_CREDENTIALS.
// This observer never reopens the numeric PID as a new authority handle.
[[nodiscard]] MakepkgSyncdepsRetainedProcessObservation
observe_makepkg_syncdeps_retained_process(
    const MakepkgSyncdepsPidfd& process);

[[nodiscard]] bool makepkg_syncdeps_pidfd_identity_matches(
    const MakepkgSyncdepsPidfdIdentity& lhs,
    const MakepkgSyncdepsPidfdIdentity& rhs) noexcept;

[[nodiscard]] bool makepkg_syncdeps_executable_identity_matches(
    const MakepkgSyncdepsInstalledExecutableIdentity& lhs,
    const MakepkgSyncdepsInstalledExecutableIdentity& rhs) noexcept;

// One instance belongs to one live root supervisor and one session. It keeps
// the session directory descriptor stable while publishing immutable
// transition files below the fixed owner-specific namespace.
class MakepkgSyncdepsAdapterStateStore final {
public:
    [[nodiscard]] static MakepkgSyncdepsAdapterStateStore
    create_below_runtime_parent(
        int runtime_parent_fd, uid_t expected_owner,
        const MakepkgSyncdepsPreparedSessionState& prepared_state);

    MakepkgSyncdepsAdapterStateStore(
        const MakepkgSyncdepsAdapterStateStore&) = delete;
    MakepkgSyncdepsAdapterStateStore(
        MakepkgSyncdepsAdapterStateStore&&) noexcept;
    MakepkgSyncdepsAdapterStateStore& operator=(
        const MakepkgSyncdepsAdapterStateStore&) = delete;
    MakepkgSyncdepsAdapterStateStore& operator=(
        MakepkgSyncdepsAdapterStateStore&&) noexcept;
    ~MakepkgSyncdepsAdapterStateStore();

    [[nodiscard]] const std::string& session_token() const noexcept;
    [[nodiscard]] const MakepkgSyncdepsPreparedSessionState& prepared_state()
        const noexcept;

    void bind_child(const MakepkgSyncdepsBoundChildState& binding);

    [[nodiscard]] MakepkgSyncdepsTransactionPrepareResponse
    prepare_transaction(
        std::size_t ordinal,
        const std::vector<std::string>& dependency_specifications);
    void record_transaction(
        std::size_t ordinal, const std::string& transaction_token,
        MakepkgSyncdepsSyntheticObservation observation);
    void finalize_transaction(
        std::size_t ordinal, const std::string& transaction_token,
        MakepkgSyncdepsCommandOutcome outcome, int exit_code);
    void consume_transaction(
        std::size_t ordinal, const std::string& transaction_token);
    void abort_transaction(
        std::size_t ordinal, const std::string& transaction_token);

    void finalize_session(
        MakepkgSyncdepsCommandOutcome makepkg_outcome,
        int makepkg_exit_code);
    [[nodiscard]] std::string consume_session();
    void abort_session();

private:
    struct Implementation;

    explicit MakepkgSyncdepsAdapterStateStore(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};
