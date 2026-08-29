#include "makepkg_syncdeps_adapter_protocol.hpp"
#include "makepkg_syncdeps_adapter_state.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>

namespace {

namespace fs = std::filesystem;

void expect(bool condition, const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
}

template <typename Callable>
void expect_failure(Callable&& callable, const std::string& diagnostic) {
    try {
        std::forward<Callable>(callable)();
    } catch(const std::exception&) {
        return;
    }
    throw std::runtime_error(diagnostic);
}

std::string token(char digit) {
    return std::string(
        MAKEPKG_SYNCDEPS_ADAPTER_TOKEN_HEX_LENGTH, digit);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        std::string pattern = "/tmp/moguet-syncdeps-state-test-XXXXXX";
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        char* created = mkdtemp(bytes.data());
        if(created == nullptr) {
            throw std::runtime_error(
                "unable to create makepkg syncdeps test directory");
        }
        path_ = created;
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory() {
        fs::remove_all(path_);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

struct BarrierChild {
    pid_t pid = -1;
    int release_descriptor = -1;
    std::optional<MakepkgSyncdepsObservedProcess> observed;

    BarrierChild(
        pid_t child_pid, int child_release_descriptor,
        std::optional<MakepkgSyncdepsObservedProcess> child_observed) noexcept
        : pid(child_pid), release_descriptor(child_release_descriptor),
          observed(std::move(child_observed)) {
    }

    BarrierChild(const BarrierChild&) = delete;
    BarrierChild& operator=(const BarrierChild&) = delete;
    BarrierChild(BarrierChild&& other) noexcept
        : pid(std::exchange(other.pid, -1)),
          release_descriptor(
              std::exchange(other.release_descriptor, -1)),
          observed(std::move(other.observed)) {
    }
    BarrierChild& operator=(BarrierChild&&) = delete;
    ~BarrierChild() {
        if(release_descriptor >= 0) {
            static_cast<void>(write(release_descriptor, "X", 1));
            static_cast<void>(close(release_descriptor));
        }
        if(pid > 0) {
            int status = 0;
            while(waitpid(pid, &status, 0) == -1 && errno == EINTR) {
            }
        }
    }

    int finish(int exit_marker = 'R') {
        if(release_descriptor < 0 || pid <= 0) {
            throw std::runtime_error("barrier child is not live");
        }
        const char marker = static_cast<char>(exit_marker);
        if(write(release_descriptor, &marker, 1) != 1) {
            throw std::runtime_error("unable to release barrier child");
        }
        static_cast<void>(close(release_descriptor));
        release_descriptor = -1;
        int status = 0;
        pid_t result;
        do {
            result = waitpid(pid, &status, 0);
        } while(result == -1 && errno == EINTR);
        if(result == -1 || !WIFEXITED(status)) {
            throw std::runtime_error("unable to wait barrier child");
        }
        pid = -1;
        return WEXITSTATUS(status);
    }
};

BarrierChild spawn_barrier_child() {
    int descriptors[2]{};
    if(pipe2(descriptors, O_CLOEXEC) == -1) {
        throw std::runtime_error("unable to create child barrier");
    }
    const pid_t child = fork();
    if(child == -1) {
        static_cast<void>(close(descriptors[0]));
        static_cast<void>(close(descriptors[1]));
        throw std::runtime_error("unable to fork child barrier");
    }
    if(child == 0) {
        static_cast<void>(close(descriptors[1]));
        char marker = 0;
        ssize_t count;
        do {
            count = read(descriptors[0], &marker, 1);
        } while(count == -1 && errno == EINTR);
        static_cast<void>(close(descriptors[0]));
        _exit(count == 1 && marker == 'R' ? 0 : 97);
    }
    static_cast<void>(close(descriptors[0]));
    BarrierChild result{child, descriptors[1], std::nullopt};
    result.observed = observe_makepkg_syncdeps_process(child);
    return result;
}

MakepkgSyncdepsPreparedSessionState prepared_state(
    const std::string& session_token,
    const MakepkgSyncdepsPidfdIdentity& supervisor_identity) {
    MakepkgSyncdepsObservedProcess current =
        observe_makepkg_syncdeps_process(getpid());
    MakepkgSyncdepsPidfdIdentity root_launcher = current.pidfd.identity();
    root_launcher.uid = 0;
    MakepkgSyncdepsPidfdIdentity root_supervisor = supervisor_identity;
    root_supervisor.uid = 0;
    return MakepkgSyncdepsPreparedSessionState{
        session_token, static_cast<std::uint32_t>(geteuid()),
        current.executable, root_launcher, root_supervisor};
}

MakepkgSyncdepsAdapterStateStore open_store(
    const TemporaryDirectory& directory,
    const MakepkgSyncdepsPreparedSessionState& prepared) {
    const int descriptor = open(
        directory.path().c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor == -1) {
        throw std::runtime_error("unable to open state test root");
    }
    try {
        MakepkgSyncdepsAdapterStateStore store =
            MakepkgSyncdepsAdapterStateStore::create_below_runtime_parent(
                descriptor, geteuid(), prepared);
        static_cast<void>(close(descriptor));
        return store;
    } catch(...) {
        static_cast<void>(close(descriptor));
        throw;
    }
}

fs::path active_root(const TemporaryDirectory& directory) {
    return directory.path() / "moguet" / "makepkg-syncdeps" / "active";
}

fs::path used_root(const TemporaryDirectory& directory) {
    return directory.path() / "moguet" / "makepkg-syncdeps" / "used";
}

void bind_child(
    MakepkgSyncdepsAdapterStateStore& store,
    const BarrierChild& child) {
    MakepkgSyncdepsPidfdIdentity transaction_adapter =
        child.observed->pidfd.identity();
    ++transaction_adapter.inode;
    store.bind_child(MakepkgSyncdepsBoundChildState{
        store.session_token(), child.observed->pidfd.identity(),
        transaction_adapter});
}

void complete_transaction(
    MakepkgSyncdepsAdapterStateStore& store, std::size_t ordinal) {
    const MakepkgSyncdepsTransactionPrepareResponse prepared =
        store.prepare_transaction(
            ordinal,
            {"synthetic-dependency-" + std::to_string(ordinal)});
    store.record_transaction(
        ordinal, prepared.transaction_token,
        MakepkgSyncdepsSyntheticObservation::Observed);
    store.finalize_transaction(
        ordinal, prepared.transaction_token,
        MakepkgSyncdepsCommandOutcome::Succeeded, 0);
    store.consume_transaction(ordinal, prepared.transaction_token);
    expect_failure(
        [&]() {
            store.consume_transaction(
                ordinal, prepared.transaction_token);
        },
        "transaction consume replay was accepted");
}

void test_protocol_and_token_contract() {
    const std::optional<std::string> first =
        generate_makepkg_syncdeps_adapter_token();
    const std::optional<std::string> second =
        generate_makepkg_syncdeps_adapter_token();
    expect(
        first.has_value() && second.has_value() && *first != *second &&
            is_valid_makepkg_syncdeps_adapter_token(*first) &&
            is_valid_makepkg_syncdeps_adapter_token(*second),
        "getrandom token generation is not canonical and independent");

    const auto valid_prepare = parse_makepkg_syncdeps_adapter_arguments(
        {"transaction-prepare", token('a'), "1", "--", "dep>=1"});
    expect(
        std::holds_alternative<MakepkgSyncdepsAdapterInvocation>(
            valid_prepare),
        "valid transaction prepare protocol was rejected");
    expect(
        std::holds_alternative<MakepkgSyncdepsAdapterInvocation>(
            parse_makepkg_syncdeps_adapter_arguments(
                {"session-bind", token('a'), "1", "2", "3"})) &&
            std::holds_alternative<MakepkgSyncdepsAdapterInvocation>(
                parse_makepkg_syncdeps_adapter_arguments(
                    {"synthetic-security", "post-exec"})),
        "exact role bind or security protocol was rejected");
    for(const std::vector<std::string>& arguments : {
            std::vector<std::string>{"session-bind", "../x", "1", "2"},
            {"session-bind", token('A'), "1", "2"},
            {"session-bind", std::string(63, 'a'), "1", "2"},
            {"session-bind", std::string(65, 'a'), "1", "2"},
            {"session-bind", token('a'), "0", "2"},
            {"transaction-prepare", token('a'), "0", "--", "dep"},
            {"transaction-prepare", token('a'), "1", "dep"},
            {"transaction-prepare", token('a'), "1", "--", "--root"},
            {"transaction-record", token('a'), "1", token('b'), "Complete"},
            {"transaction-finalize", token('a'), "1", token('b'), "Succeeded", "1"},
            {"session-finalize", token('a'), "1", "Failed", "0"},
            {"session-consume", token('a'), "1", "caller-owner"},
            {"synthetic-security", "unknown-scenario"},
        }) {
        expect(
            std::holds_alternative<MakepkgSyncdepsAdapterProtocolFailure>(
                parse_makepkg_syncdeps_adapter_arguments(arguments)),
            "protocol accepted invalid, owner-bearing, or capability-bearing argv");
    }

    MakepkgSyncdepsAdapterInvocation invocation =
        std::get<MakepkgSyncdepsAdapterInvocation>(valid_prepare);
    const std::string wire =
        serialize_makepkg_syncdeps_adapter_request(invocation);
    const auto reparsed = parse_makepkg_syncdeps_adapter_request(wire);
    const auto* request =
        std::get_if<MakepkgSyncdepsAdapterInvocation>(&reparsed);
    expect(
        request != nullptr &&
            request->dependency_specifications ==
                std::vector<std::string>{"dep>=1"},
        "request wire protocol lost opaque dependency specification bytes");
    expect(
        std::holds_alternative<MakepkgSyncdepsAdapterProtocolFailure>(
            parse_makepkg_syncdeps_adapter_request(
                wire.substr(0, wire.size() - 2))),
        "truncated request protocol was accepted");
}

void test_process_binding_uses_non_reused_pidfd() {
    MakepkgSyncdepsObservedProcess current =
        observe_makepkg_syncdeps_process(getpid());
    expect(
        current.pidfd.is_alive() &&
            observe_makepkg_syncdeps_retained_process(current.pidfd).uid ==
                geteuid(),
        "current process did not bind through a live pidfd");
    BarrierChild first = spawn_barrier_child();
    BarrierChild second = spawn_barrier_child();
    expect(
        first.observed->pidfd.identity().inode !=
                second.observed->pidfd.identity().inode &&
            first.observed->parent_pid == getpid() &&
            second.observed->parent_pid == getpid(),
        "independent children did not receive independent pidfd identities");
    expect(
        makepkg_syncdeps_executable_identity_matches(
            first.observed->executable, current.executable),
        "pre-release barrier child did not retain installed-launcher executable identity");
    static_cast<void>(first.finish());
    expect(
        !first.observed->pidfd.is_alive(),
        "exited child pidfd still reports a live exact process");
    static_cast<void>(second.finish());

    const fs::path unrelated = fs::temp_directory_path() /
                               "moguet-syncdeps-unrelated-executable";
    {
        std::ofstream output(unrelated);
        output << "not an executable identity\n";
    }
    struct stat metadata{};
    if(stat(unrelated.c_str(), &metadata) == -1) {
        fs::remove(unrelated);
        throw std::runtime_error("unable to inspect unrelated executable");
    }
    expect(
        !makepkg_syncdeps_executable_identity_matches(
            current.executable,
            {static_cast<std::uint64_t>(metadata.st_dev),
             static_cast<std::uint64_t>(metadata.st_ino)}),
        "wrong executable identity matched the launcher");
    fs::remove(unrelated);
}

void test_zero_one_two_and_replay() {
    for(std::size_t transaction_count = 0; transaction_count <= 2;
        ++transaction_count) {
        TemporaryDirectory directory;
        MakepkgSyncdepsObservedProcess current =
            observe_makepkg_syncdeps_process(getpid());
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(
                token(static_cast<char>('1' + transaction_count)),
                current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        bind_child(store, child);
        for(std::size_t ordinal = 1; ordinal <= transaction_count;
            ++ordinal) {
            complete_transaction(store, ordinal);
        }
        expect(child.finish() == 0, "bound child did not exit successfully");
        store.finalize_session(MakepkgSyncdepsCommandOutcome::Succeeded, 0);
        expect_failure(
            [&]() {
                static_cast<void>(store.prepare_transaction(1, {"late"}));
            },
            "post-finalize transaction mutation was accepted");
        const std::string manifest_protocol = store.consume_session();
        const auto parsed = parse_makepkg_syncdeps_session_manifest(
            manifest_protocol);
        const auto* manifest =
            std::get_if<MakepkgSyncdepsSessionManifest>(&parsed);
        expect(
            manifest != nullptr &&
                manifest->terminal.terminal_state ==
                    MakepkgSyncdepsTerminalState::Complete &&
                manifest->terminal.coverage ==
                    MakepkgSyncdepsAdapterCoverage::Complete &&
                manifest->terminal.transaction_count == transaction_count &&
                manifest->transactions.size() == transaction_count &&
                manifest->prepared.launcher.uid == 0 &&
                manifest->binding.child.uid == geteuid() &&
                manifest->binding.transaction_adapter.uid == geteuid() &&
                !makepkg_syncdeps_pidfd_identity_matches(
                    manifest->binding.child,
                    manifest->binding.transaction_adapter) &&
                manifest->evidence_kind ==
                    MakepkgSyncdepsEvidenceKind::Synthetic,
            "terminal 0/1/2 manifest lost count, coverage, or synthetic boundary");
        if(transaction_count == 0) {
            std::string wrong_owner = manifest_protocol;
            const std::string owner_record =
                "OWNER\tmakepkg-sync-dependencies";
            wrong_owner.replace(
                wrong_owner.find(owner_record), owner_record.size(),
                "OWNER\tselected-repository-provider");
            expect(
                std::holds_alternative<
                    MakepkgSyncdepsAdapterProtocolFailure>(
                    parse_makepkg_syncdeps_session_manifest(wrong_owner)),
                "selected-provider owner was accepted by makepkg session manifest");
        }
        if(transaction_count == 1) {
            std::string wrong_ordinal = manifest_protocol;
            const std::size_t ordinal = wrong_ordinal.find("ORDINAL\t1");
            wrong_ordinal.replace(ordinal, 9, "ORDINAL\t2");
            expect(
                std::holds_alternative<
                    MakepkgSyncdepsAdapterProtocolFailure>(
                    parse_makepkg_syncdeps_session_manifest(
                        wrong_ordinal)),
                "ordinal-mismatched session manifest was accepted");
        }
        if(transaction_count == 2) {
            std::string duplicate_token = manifest_protocol;
            const std::string first_record =
                "TRANSACTION_TOKEN\t" +
                manifest->transactions[0].prepared.transaction_token;
            const std::string second_record =
                "TRANSACTION_TOKEN\t" +
                manifest->transactions[1].prepared.transaction_token;
            duplicate_token.replace(
                duplicate_token.find(second_record), second_record.size(),
                first_record);
            expect(
                std::holds_alternative<
                    MakepkgSyncdepsAdapterProtocolFailure>(
                    parse_makepkg_syncdeps_session_manifest(
                        duplicate_token)),
                "duplicate transaction token manifest was accepted");
        }
        expect(
            !fs::exists(active_root(directory) / store.session_token()) &&
                fs::is_directory(
                    used_root(directory) / store.session_token()) &&
                fs::is_empty(used_root(directory) / store.session_token()),
            "session consume did not publish an empty replay tombstone");
        expect_failure(
            [&]() { static_cast<void>(store.consume_session()); },
            "session consume replay was accepted");
    }
}

void test_third_ordinal_mismatch_reentrant_and_abort() {
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsObservedProcess current =
            observe_makepkg_syncdeps_process(getpid());
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('4'), current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        bind_child(store, child);
        complete_transaction(store, 1);
        complete_transaction(store, 2);
        expect_failure(
            [&]() {
                static_cast<void>(store.prepare_transaction(3, {"third"}));
            },
            "third transaction was accepted");
        expect(
            !fs::exists(
                active_root(directory) / store.session_token() /
                "transactions" / "3") &&
                !fs::exists(
                    active_root(directory) / store.session_token() /
                    "current"),
            "third transaction mutated a transaction slot before rejection");
        expect(child.finish(91) == 97, "unsupported child fixture did not exit");
        store.finalize_session(MakepkgSyncdepsCommandOutcome::Failed, 91);
        const auto parsed = parse_makepkg_syncdeps_session_manifest(
            store.consume_session());
        const auto* manifest =
            std::get_if<MakepkgSyncdepsSessionManifest>(&parsed);
        expect(
            manifest != nullptr &&
                manifest->terminal.terminal_state ==
                    MakepkgSyncdepsTerminalState::Unsupported &&
                manifest->terminal.coverage ==
                    MakepkgSyncdepsAdapterCoverage::Unsupported &&
                manifest->transactions.size() == 2,
            "third transaction did not preserve two entries and unsupported terminal state");
    }
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsObservedProcess current =
            observe_makepkg_syncdeps_process(getpid());
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('5'), current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        bind_child(store, child);
        const auto prepared = store.prepare_transaction(1, {"first"});
        expect_failure(
            [&]() {
                static_cast<void>(store.prepare_transaction(1, {"reentrant"}));
            },
            "reentrant transaction was accepted");
        expect_failure(
            [&]() {
                store.record_transaction(
                    2, prepared.transaction_token,
                    MakepkgSyncdepsSyntheticObservation::Observed);
            },
            "ordinal mismatch was accepted");
        store.abort_session();
        static_cast<void>(child.finish());
    }
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsObservedProcess current =
            observe_makepkg_syncdeps_process(getpid());
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('0'), current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        bind_child(store, child);
        const fs::path transactions =
            active_root(directory) / store.session_token() /
            "transactions";
        fs::remove(transactions);
        std::ofstream(transactions) << "not a directory\n";
        fs::permissions(
            transactions,
            fs::perms::owner_read | fs::perms::owner_write,
            fs::perm_options::replace);
        expect_failure(
            [&]() {
                static_cast<void>(store.prepare_transaction(1, {"target"}));
            },
            "transaction directory replacement was accepted");
        static_cast<void>(child.finish());
    }
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsObservedProcess current =
            observe_makepkg_syncdeps_process(getpid());
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('6'), current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        bind_child(store, child);
        const auto prepared = store.prepare_transaction(1, {"aborted"});
        store.abort_transaction(1, prepared.transaction_token);
        static_cast<void>(child.finish());
        store.finalize_session(MakepkgSyncdepsCommandOutcome::Failed, 7);
        const auto parsed = parse_makepkg_syncdeps_session_manifest(
            store.consume_session());
        const auto* manifest =
            std::get_if<MakepkgSyncdepsSessionManifest>(&parsed);
        expect(
            manifest != nullptr && manifest->transactions.size() == 1 &&
                manifest->transactions[0].outcome.outcome ==
                    MakepkgSyncdepsCommandOutcome::NotAttempted &&
                manifest->transactions[0].observation.observation ==
                    MakepkgSyncdepsSyntheticObservation::Missing,
            "transaction abort did not retain a one-shot non-positive entry");
    }
}

void test_token_uid_filesystem_and_partial_negatives() {
    MakepkgSyncdepsObservedProcess current =
        observe_makepkg_syncdeps_process(getpid());
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('7'), current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        MakepkgSyncdepsBoundChildState wrong_uid{
            store.session_token(), child.observed->pidfd.identity(),
            child.observed->pidfd.identity()};
        ++wrong_uid.transaction_adapter.inode;
        ++wrong_uid.child.uid;
        expect_failure(
            [&]() { store.bind_child(wrong_uid); },
            "wrong invoking uid was bound");
        bind_child(store, child);
        const auto prepared = store.prepare_transaction(1, {"target"});
        expect_failure(
            [&]() {
                store.record_transaction(
                    1, token('f'),
                    MakepkgSyncdepsSyntheticObservation::Observed);
            },
            "wrong transaction token was accepted");
        store.record_transaction(
            1, prepared.transaction_token,
            MakepkgSyncdepsSyntheticObservation::Observed);
        const fs::path current_root =
            active_root(directory) / store.session_token() / "current";
        std::ofstream partial(current_root / "outcome.partial");
        partial << "truncated\n";
        partial.close();
        fs::permissions(
            current_root / "outcome.partial", fs::perms::owner_read | fs::perms::owner_write,
            fs::perm_options::replace);
        expect_failure(
            [&]() {
                store.finalize_transaction(
                    1, prepared.transaction_token,
                    MakepkgSyncdepsCommandOutcome::Succeeded, 0);
            },
            "partial publication was overwritten or accepted");
        store.abort_session();
        static_cast<void>(child.finish());
    }
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('e'), current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        bind_child(store, child);
        const fs::path session_file =
            active_root(directory) / store.session_token() / "session";
        std::ifstream input(session_file, std::ios::binary);
        const std::string contents{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        input.close();
        fs::remove(session_file);
        std::ofstream replacement(session_file, std::ios::binary);
        replacement << contents;
        replacement.close();
        fs::permissions(
            session_file,
            fs::perms::owner_read | fs::perms::owner_write,
            fs::perm_options::replace);
        expect_failure(
            [&]() {
                static_cast<void>(store.prepare_transaction(1, {"target"}));
            },
            "session state inode replacement was accepted");
        static_cast<void>(child.finish());
    }
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('8'), current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        bind_child(store, child);
        const fs::path session_file =
            active_root(directory) / store.session_token() / "session";
        fs::permissions(
            session_file,
            fs::perms::owner_read | fs::perms::owner_write |
                fs::perms::group_read,
            fs::perm_options::replace);
        expect_failure(
            [&]() {
                static_cast<void>(store.prepare_transaction(1, {"target"}));
            },
            "wrong-mode session state was accepted");
        static_cast<void>(child.finish());
    }
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('9'), current.pidfd.identity()));
        const fs::path outside = directory.path() / "outside";
        std::ofstream(outside) << "safe\n";
        const fs::path binding =
            active_root(directory) / store.session_token() / "binding";
        fs::create_symlink(outside, binding);
        BarrierChild child = spawn_barrier_child();
        expect_failure(
            [&]() { bind_child(store, child); },
            "binding symlink was followed or replaced");
        expect(
            std::ifstream(outside).peek() == 's',
            "binding symlink changed the outside file");
        static_cast<void>(child.finish());
    }
    {
        TemporaryDirectory directory;
        MakepkgSyncdepsAdapterStateStore store = open_store(
            directory,
            prepared_state(token('1'), current.pidfd.identity()));
        const fs::path session_root =
            active_root(directory) / store.session_token();
        const fs::path unexpected = session_root / "unexpected";
        std::ofstream(unexpected) << "unexpected\n";
        fs::permissions(
            unexpected,
            fs::perms::owner_read | fs::perms::owner_write,
            fs::perm_options::replace);
        expect_failure(
            [&]() { store.abort_session(); },
            "session abort hid unexpected state");
        expect(
            fs::is_regular_file(session_root / "session") &&
                fs::is_regular_file(unexpected),
            "malformed-state abort partially deleted authority files");
    }
}

void test_cross_session_isolation_and_stale_retirement() {
    TemporaryDirectory directory;
    MakepkgSyncdepsObservedProcess current =
        observe_makepkg_syncdeps_process(getpid());
    MakepkgSyncdepsAdapterStateStore first = open_store(
        directory, prepared_state(token('a'), current.pidfd.identity()));
    MakepkgSyncdepsAdapterStateStore second = open_store(
        directory, prepared_state(token('b'), current.pidfd.identity()));
    BarrierChild first_child = spawn_barrier_child();
    BarrierChild second_child = spawn_barrier_child();
    bind_child(first, first_child);
    bind_child(second, second_child);
    const auto first_transaction = first.prepare_transaction(1, {"first"});
    const auto second_transaction = second.prepare_transaction(1, {"second"});
    expect_failure(
        [&]() {
            second.record_transaction(
                1, first_transaction.transaction_token,
                MakepkgSyncdepsSyntheticObservation::Observed);
        },
        "cross-session transaction token was accepted");
    first.abort_session();
    second.abort_session();
    static_cast<void>(first_child.finish());
    static_cast<void>(second_child.finish());

    TemporaryDirectory stale_directory;
    BarrierChild stale_supervisor = spawn_barrier_child();
    const std::string stale_token = token('c');
    {
        MakepkgSyncdepsAdapterStateStore stale = open_store(
            stale_directory,
            prepared_state(
                stale_token,
                stale_supervisor.observed->pidfd.identity()));
        static_cast<void>(stale.session_token());
    }
    static_cast<void>(stale_supervisor.finish());
    MakepkgSyncdepsAdapterStateStore replacement = open_store(
        stale_directory,
        prepared_state(token('d'), current.pidfd.identity()));
    expect(
        !fs::exists(active_root(stale_directory) / stale_token) &&
            fs::is_directory(used_root(stale_directory) / stale_token) &&
            fs::is_empty(used_root(stale_directory) / stale_token),
        "dead-supervisor session was not safely retired to a tombstone");
    replacement.abort_session();

    TemporaryDirectory staging_directory;
    {
        MakepkgSyncdepsAdapterStateStore bootstrap = open_store(
            staging_directory,
            prepared_state(token('2'), current.pidfd.identity()));
        bootstrap.abort_session();
    }
    const std::string abandoned_token = token('3');
    const fs::path abandoned_staging =
        active_root(staging_directory) /
        ("." + abandoned_token + ".preparing");
    fs::create_directory(abandoned_staging);
    fs::permissions(
        abandoned_staging, fs::perms::owner_all,
        fs::perm_options::replace);
    std::ofstream(abandoned_staging / "session") << "truncated\n";
    fs::permissions(
        abandoned_staging / "session",
        fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace);
    fs::create_directory(abandoned_staging / "transactions");
    fs::permissions(
        abandoned_staging / "transactions", fs::perms::owner_all,
        fs::perm_options::replace);
    MakepkgSyncdepsAdapterStateStore after_partial = open_store(
        staging_directory,
        prepared_state(token('4'), current.pidfd.identity()));
    expect(
        !fs::exists(abandoned_staging),
        "abandoned partial session publication was not retired safely");
    after_partial.abort_session();

    TemporaryDirectory wrong_owner_directory;
    const int wrong_owner_root = open(
        wrong_owner_directory.path().c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(wrong_owner_root == -1) {
        throw std::runtime_error("unable to open wrong-owner state root");
    }
    expect_failure(
        [&]() {
            static_cast<void>(
                MakepkgSyncdepsAdapterStateStore::create_below_runtime_parent(
                    wrong_owner_root, static_cast<uid_t>(geteuid() + 1U),
                    prepared_state(token('f'), current.pidfd.identity())));
        },
        "wrong-owner runtime parent was accepted");
    static_cast<void>(close(wrong_owner_root));
}

void prepare_terminal_session_for_retirement(
    MakepkgSyncdepsAdapterStateStore& store, BarrierChild& child) {
    bind_child(store, child);
    complete_transaction(store, 1);
    expect(child.finish() == 0, "retirement child did not exit");
    store.finalize_session(MakepkgSyncdepsCommandOutcome::Succeeded, 0);
}

void test_crash_consistent_used_retirement_recovery() {
    MakepkgSyncdepsObservedProcess current =
        observe_makepkg_syncdeps_process(getpid());
    const std::vector<fs::path> cleanup_order{
        "session",
        "binding",
        "terminal",
        fs::path("transactions") / "1" / "prepared",
        fs::path("transactions") / "1" / "observation",
        fs::path("transactions") / "1" / "outcome",
        fs::path("transactions") / "1",
        "transactions",
    };
    for(std::size_t removed_count = 0;
        removed_count <= cleanup_order.size(); ++removed_count) {
        TemporaryDirectory directory;
        const std::string retired_token =
            token(static_cast<char>('0' + (removed_count % 10U)));
        MakepkgSyncdepsAdapterStateStore retired = open_store(
            directory,
            prepared_state(retired_token, current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        prepare_terminal_session_for_retirement(retired, child);
        const fs::path active = active_root(directory) / retired_token;
        const fs::path used = used_root(directory) / retired_token;
        fs::rename(active, used);
        for(std::size_t index = 0; index < removed_count; ++index) {
            expect(
                fs::remove(used / cleanup_order[index]),
                "unable to construct interrupted retirement shape");
        }
        MakepkgSyncdepsAdapterStateStore replacement = open_store(
            directory,
            prepared_state(token('a'), current.pidfd.identity()));
        expect(
            fs::is_directory(used) && fs::is_empty(used),
            "known interrupted retirement did not recover to an empty tombstone");
        replacement.abort_session();
    }

    for(const std::string malformed_kind :
        {"unknown", "symlink", "mode"}) {
        TemporaryDirectory directory;
        const std::string retired_token = token('b');
        MakepkgSyncdepsAdapterStateStore retired = open_store(
            directory,
            prepared_state(retired_token, current.pidfd.identity()));
        BarrierChild child = spawn_barrier_child();
        prepare_terminal_session_for_retirement(retired, child);
        const fs::path active = active_root(directory) / retired_token;
        const fs::path used = used_root(directory) / retired_token;
        fs::rename(active, used);
        if(malformed_kind == "unknown") {
            std::ofstream(used / "unexpected") << "unexpected\n";
            fs::permissions(
                used / "unexpected",
                fs::perms::owner_read | fs::perms::owner_write,
                fs::perm_options::replace);
        } else if(malformed_kind == "symlink") {
            fs::remove(used / "session");
            fs::create_symlink("terminal", used / "session");
        } else {
            fs::permissions(
                used / "session",
                fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_read,
                fs::perm_options::replace);
        }
        const std::size_t before_count = static_cast<std::size_t>(
            std::distance(
                fs::directory_iterator(used), fs::directory_iterator()));
        expect_failure(
            [&]() {
                static_cast<void>(open_store(
                    directory,
                    prepared_state(token('c'), current.pidfd.identity())));
            },
            "malformed used tombstone was recovered");
        const std::size_t after_count = static_cast<std::size_t>(
            std::distance(
                fs::directory_iterator(used), fs::directory_iterator()));
        expect(
            before_count == after_count && fs::exists(used / "terminal"),
            "malformed used tombstone was partially deleted");
    }
}

} // namespace

int main() {
    try {
        test_protocol_and_token_contract();
        test_process_binding_uses_non_reused_pidfd();
        test_zero_one_two_and_replay();
        test_third_ordinal_mismatch_reentrant_and_abort();
        test_token_uid_filesystem_and_partial_negatives();
        test_cross_session_isolation_and_stale_retirement();
        test_crash_consistent_used_retirement_recovery();
    } catch(const std::exception& error) {
        std::cerr << "makepkg-syncdeps-adapter-state-test: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
