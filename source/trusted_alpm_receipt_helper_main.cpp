#include "trusted_alpm_receipt_helper_state.hpp"
#include "trusted_alpm_receipt_protocol.hpp"

#include <cerrno>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace {

bool write_stdout(const std::string& output) noexcept {
    std::size_t offset = 0;
    while(offset < output.size()) {
        const ssize_t written = write(
            STDOUT_FILENO, output.data() + offset,
            output.size() - offset);
        if(written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if(written == -1 && errno == EINTR) continue;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for(int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const TrustedAlpmReceiptHelperInvocationResult parsed =
        parse_trusted_alpm_receipt_helper_arguments(arguments);
    const auto* invocation =
        std::get_if<TrustedAlpmReceiptHelperInvocation>(&parsed);
    if(invocation == nullptr) {
        std::cerr << "moguet-alpm-receipt-helper: invalid fixed protocol invocation\n";
        return 2;
    }
    if(geteuid() != 0) {
        std::cerr << "moguet-alpm-receipt-helper: root execution is required\n";
        return 1;
    }

    int runtime_fd = open(
        "/run", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(runtime_fd == -1) {
        std::cerr << "moguet-alpm-receipt-helper: unable to open /run: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    try {
        TrustedAlpmReceiptStateStore store =
            TrustedAlpmReceiptStateStore::open_below_runtime_parent(
                runtime_fd, 0);
        static_cast<void>(close(runtime_fd));
        runtime_fd = -1;
        switch(invocation->command) {
            case TrustedAlpmReceiptHelperCommand::Prepare: {
                const std::string response =
                    serialize_trusted_alpm_receipt_prepare_response(
                        TrustedAlpmReceiptPrepareResponse{
                            invocation->transaction_token,
                            trusted_alpm_receipt_hook_directory(
                                invocation->transaction_token)});
                store.prepare(
                    invocation->transaction_token,
                    invocation->requested_package_names);
                if(!write_stdout(response)) {
                    try {
                        store.abort(invocation->transaction_token);
                    } catch(const std::exception& cleanup_error) {
                        std::cerr
                            << "moguet-alpm-receipt-helper: unable to publish prepare response and exact abort failed: "
                            << cleanup_error.what() << '\n';
                        return 1;
                    }
                    std::cerr << "moguet-alpm-receipt-helper: unable to publish prepare response; exact state was aborted\n";
                    return 1;
                }
                break;
            }
            case TrustedAlpmReceiptHelperCommand::Record:
                store.record(invocation->transaction_token, STDIN_FILENO);
                break;
            case TrustedAlpmReceiptHelperCommand::Consume: {
                const std::string response =
                    store.consume(invocation->transaction_token);
                if(!write_stdout(response)) {
                    std::cerr << "moguet-alpm-receipt-helper: unable to publish receipt response\n";
                    return 1;
                }
                break;
            }
            case TrustedAlpmReceiptHelperCommand::Abort:
                store.abort(invocation->transaction_token);
                break;
        }
    } catch(const std::exception& error) {
        if(runtime_fd >= 0) static_cast<void>(close(runtime_fd));
        std::cerr << "moguet-alpm-receipt-helper: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
