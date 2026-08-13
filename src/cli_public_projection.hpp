#pragma once

#include "cli_authority.hpp"

#include <span>
#include <string>
#include <vector>

// Public CLI syntax is projected from the structured grammar contracts. These
// helpers do not parse arguments or select runtime routes.
std::string cli_operation_form_syntax(
        const cli_authority::OperationMetadata& metadata,
        const cli_authority::OperationFormSpec& form);

std::string cli_operation_syntax(cli_authority::OperationId operation);

std::string cli_special_operation_syntax(
        cli_authority::SpecialOperationId operation);

std::span<const cli_authority::OperationId> cli_public_operation_order();

std::span<const cli_authority::SpecialOperationId>
cli_public_special_operation_order();

std::vector<std::string> cli_canonical_grammar();
