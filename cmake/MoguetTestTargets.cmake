# C++ test executable graph migrated from the legacy Make authority. Keep
# source closures explicit: target-local stubs replace only the production
# owners excluded by the corresponding list firewall.

set(_moguet_test_source_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(_moguet_test_support_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests")
set(
    _moguet_test_alpm_stub_include_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs/package-metadata"
)

function(_moguet_test_production_complement output_variable)
    set(_moguet_forbidden_sources ${MOGUET_PRODUCTION_SOURCES})
    list(REMOVE_ITEM _moguet_forbidden_sources ${ARGN})
    set(${output_variable} ${_moguet_forbidden_sources} PARENT_SCOPE)
endfunction()

set(
    _moguet_aur_update_command_test_sources
    ${MOGUET_PRODUCTION_SOURCES}
)
list(
    REMOVE_ITEM _moguet_aur_update_command_test_sources
    source/aur_update_query.cpp
    source/aur_update_execution_preflight.cpp
    source/aur_update_execution_preparation.cpp
    source/aur_update_execution_runner.cpp
    source/aur_update_operation_result.cpp
    source/filtered_aur_update_operation.cpp
    source/upgrade_all_operation.cpp
)
list(
    APPEND _moguet_aur_update_command_test_sources
    tests/stubs/aur-update-command/operation_stub.cpp
    tests/stubs/upgrade-all-command/operation_stub.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
)
_moguet_test_production_complement(
    _moguet_aur_update_command_forbidden_sources
    ${_moguet_aur_update_command_test_sources}
)
moguet_add_cpp_test(
    moguet-aur-update-command-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_aur_update_command_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_TEST_CONFIG_PATH
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_aur_update_command_forbidden_sources}
)

set(
    _moguet_upgrade_all_command_test_sources
    ${MOGUET_PRODUCTION_SOURCES}
)
list(
    REMOVE_ITEM _moguet_upgrade_all_command_test_sources
    source/upgrade_all_operation.cpp
)
list(
    APPEND _moguet_upgrade_all_command_test_sources
    tests/stubs/upgrade-all-command/operation_stub.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
)
_moguet_test_production_complement(
    _moguet_upgrade_all_command_forbidden_sources
    ${_moguet_upgrade_all_command_test_sources}
)
moguet_add_cpp_test(
    moguet-upgrade-all-command-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_upgrade_all_command_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_TEST_CONFIG_PATH
        "MOGUET_LOCALE_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/locale\""
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_upgrade_all_command_forbidden_sources}
)

set(_moguet_commands_sync_test_sources ${MOGUET_PRODUCTION_SOURCES})
list(
    REMOVE_ITEM _moguet_commands_sync_test_sources
    source/aur_rpc.cpp
    source/root_package_search.cpp
)
list(
    APPEND _moguet_commands_sync_test_sources
    tests/stubs/commands-sync/aur_rpc_stub.cpp
    tests/stubs/commands-sync/root_package_search_stub.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
)
_moguet_test_production_complement(
    _moguet_commands_sync_forbidden_sources
    ${_moguet_commands_sync_test_sources}
)
moguet_add_cpp_test(
    moguet-commands-sync-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_commands_sync_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_TEST_CONFIG_PATH
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_commands_sync_forbidden_sources}
)

set(_moguet_commands_inspect_test_sources ${MOGUET_PRODUCTION_SOURCES})
list(
    REMOVE_ITEM _moguet_commands_inspect_test_sources
    source/aur_rpc.cpp
    source/repository_query.cpp
)
list(
    APPEND _moguet_commands_inspect_test_sources
    tests/commands_inspect_aur_stub.cpp
    tests/stubs/commands-inspect/repository_query_stub.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
)
_moguet_test_production_complement(
    _moguet_commands_inspect_forbidden_sources
    ${_moguet_commands_inspect_test_sources}
)
moguet_add_cpp_test(
    moguet-commands-inspect-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_commands_inspect_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        "MOGUET_LOCALE_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/locale\""
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_commands_inspect_forbidden_sources}
)

moguet_add_cpp_test(
    moguet-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    CURL
    SOURCES ${MOGUET_PRODUCTION_SOURCES}
    DEFINITIONS MOGUET_ENABLE_TEST_OVERRIDES
)

moguet_add_cpp_test(
    moguet-cli-localization-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    CURL
    SOURCES ${MOGUET_PRODUCTION_SOURCES}
    DEFINITIONS
        "MOGUET_LOCALE_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/locale\""
        MOGUET_ENABLE_TEST_OVERRIDES
)

moguet_add_cpp_test(
    moguet-app-config-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    CURL
    SOURCES ${MOGUET_PRODUCTION_SOURCES}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_TEST_CONFIG_PATH
        MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS
)

set(
    _moguet_aur_rpc_validation_test_sources
    ${MOGUET_PRODUCTION_SOURCES}
    tests/stubs/package-metadata/alpm_stub.cpp
)
moguet_add_cpp_test(
    moguet-aur-rpc-validation-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_aur_rpc_validation_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_AUR_RPC_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

set(
    _moguet_source_install_characterization_test_sources
    ${MOGUET_PRODUCTION_SOURCES}
)
list(
    REMOVE_ITEM _moguet_source_install_characterization_test_sources
    source/moguet.cpp
)
list(
    APPEND _moguet_source_install_characterization_test_sources
    tests/source_install_characterization.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
)
_moguet_test_production_complement(
    _moguet_source_install_characterization_forbidden_sources
    ${_moguet_source_install_characterization_test_sources}
)
moguet_add_cpp_test(
    moguet-source-install-characterization-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_source_install_characterization_test_sources}
    DEFINITIONS MOGUET_ENABLE_TEST_OVERRIDES
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_source_install_characterization_forbidden_sources}
)

set(
    _moguet_upgrade_baseline_metadata_test_sources
    ${MOGUET_PRODUCTION_SOURCES}
    tests/stubs/package-metadata/alpm_stub.cpp
)
moguet_add_cpp_test(
    moguet-upgrade-baseline-metadata-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_upgrade_baseline_metadata_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_TEST_CONFIG_PATH
        MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

moguet_add_cpp_test(
    application-identity-test
    SOURCES tests/application_identity_test.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    interactive-confirmation-test
    SOURCES
        tests/interactive_confirmation_test.cpp
        source/interactive_confirmation.cpp
        source/logging.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_localization_test_sources
    tests/localization_test.cpp
    source/reviewed_source_production_failure.cpp
    source/reviewed_source_production_outcome.cpp
    source/source_package_identity.cpp
    source/package_identifier.cpp
    source/localization.cpp
)
moguet_add_cpp_test(
    localization-test
    SOURCES ${_moguet_localization_test_sources}
    DEFINITIONS
        "MOGUET_LOCALE_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/locale\""
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)
moguet_add_cpp_test(
    localization-missing-catalog-test
    SOURCES ${_moguet_localization_test_sources}
    DEFINITIONS
        "MOGUET_LOCALE_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/tests/missing-locale\""
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    xdg-paths-test
    SOURCES
        tests/xdg_paths_test.cpp
        source/xdg_paths.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    xdg-directory-safety-test
    SOURCES
        tests/xdg_directory_safety_test.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
    DEFINITIONS MOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    xdg-state-log-test
    SOURCES
        tests/xdg_state_log_test.cpp
        source/xdg_state_log.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/logging.cpp
    DEFINITIONS MOGUET_TEST_XDG_STATE_LOG_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_trusted_cache_test_sources
    tests/trusted_cache_test.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/logging.cpp
)
moguet_add_cpp_test(
    trusted-cache-test
    SOURCES ${_moguet_trusted_cache_test_sources}
    DEFINITIONS MOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    moguet-root-execution-identity-test
    REAL_ALPM
    CURL
    SOURCES tests/stubs/runtime-identity/geteuid_stub.cpp
    OBJECTS
        $<TARGET_OBJECTS:moguet_production_objects>
        $<TARGET_OBJECTS:moguet_unified_plan_projection_object>
        $<TARGET_OBJECTS:moguet_unified_plan_renderer_object>
    OBJECT_PRODUCTION_SOURCES ${MOGUET_PRODUCTION_SOURCES}
    LINK_OPTIONS LINKER:--wrap=geteuid
)

moguet_add_cpp_test(
    aur-rpc-envelope-validation-test
    ALPM_COMPILE
    CURL
    SOURCES
        tests/aur_rpc_validation_test.cpp
        source/aur_rpc.cpp
        source/aur_constraint_metadata.cpp
        source/package_relation.cpp
        source/dependency_constraint.cpp
        source/dependency_spec.cpp
        source/package_identifier.cpp
        source/logging.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_AUR_RPC_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

moguet_add_cpp_test(
    app-config-test
    SOURCES
        tests/app_config_test.cpp
        source/app_config.cpp
        source/provider_selection.cpp
        source/dependency_spec.cpp
        source/localization.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    provider-selection-test
    ALPM_COMPILE
    SOURCES
        tests/provider_selection_test.cpp
        source/provider_selection.cpp
        source/dependency_constraint.cpp
        source/provider_installed_state_presentation.cpp
        source/provider_installed_state.cpp
        source/package_metadata.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        source/dependency_spec.cpp
        source/localization.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/package-metadata/process_stub.cpp
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

set(
    _moguet_root_package_candidate_test_sources
    tests/root_package_candidate_test.cpp
    source/root_package_candidate.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_root_package_candidate_forbidden_sources
    ${_moguet_root_package_candidate_test_sources}
)
moguet_add_cpp_test(
    root-package-candidate-test
    FIREWALL
    SOURCES ${_moguet_root_package_candidate_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_root_package_candidate_forbidden_sources}
)

set(
    _moguet_root_package_search_test_sources
    tests/root_package_search_test.cpp
    source/root_package_search.cpp
    source/root_package_candidate.cpp
    source/package_identifier.cpp
    tests/stubs/root-package-search/search_stub.cpp
)
_moguet_test_production_complement(
    _moguet_root_package_search_forbidden_sources
    ${_moguet_root_package_search_test_sources}
)
moguet_add_cpp_test(
    root-package-search-test
    FIREWALL
    SOURCES ${_moguet_root_package_search_test_sources}
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_root_package_search_forbidden_sources}
)

set(
    _moguet_root_package_selection_test_sources
    tests/root_package_selection_test.cpp
    source/root_package_selection.cpp
    source/root_package_candidate.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_root_package_selection_forbidden_sources
    ${_moguet_root_package_selection_test_sources}
)
moguet_add_cpp_test(
    root-package-selection-test
    FIREWALL
    SOURCES ${_moguet_root_package_selection_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_root_package_selection_forbidden_sources}
)

set(
    _moguet_root_package_route_projection_test_sources
    tests/root_package_route_projection_test.cpp
    source/root_package_route_projection.cpp
    source/root_package_selection.cpp
    source/root_package_candidate.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_root_package_route_projection_forbidden_sources
    ${_moguet_root_package_route_projection_test_sources}
)
moguet_add_cpp_test(
    root-package-route-projection-test
    FIREWALL
    SOURCES ${_moguet_root_package_route_projection_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_root_package_route_projection_forbidden_sources}
)

set(
    _moguet_local_package_metadata_test_sources
    tests/local_package_metadata_test.cpp
    source/local_package_metadata.cpp
)
_moguet_test_production_complement(
    _moguet_local_package_metadata_forbidden_sources
    ${_moguet_local_package_metadata_test_sources}
)
moguet_add_cpp_test(
    local-package-metadata-test
    FIREWALL
    SOURCES ${_moguet_local_package_metadata_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_local_package_metadata_forbidden_sources}
)

set(
    _moguet_local_source_root_test_sources
    tests/local_source_root_test.cpp
    source/local_source_root.cpp
    source/local_package_metadata.cpp
)
_moguet_test_production_complement(
    _moguet_local_source_root_forbidden_sources
    ${_moguet_local_source_root_test_sources}
)
moguet_add_cpp_test(
    local-source-root-test
    FIREWALL
    SOURCES ${_moguet_local_source_root_test_sources}
    DEFINITIONS MOGUET_ENABLE_LOCAL_SOURCE_ROOT_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_local_source_root_forbidden_sources}
)

set(
    _moguet_local_dependency_plan_projection_test_sources
    tests/local_dependency_plan_projection_test.cpp
    source/local_dependency_plan_projection.cpp
    source/aur_constraint_metadata.cpp
    source/package_relation.cpp
    source/package_relation_observation.cpp
    source/package_relation_observation_adapter.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan.cpp
    source/dependency_plan_model.cpp
    source/package_relation_presentation.cpp
    source/dependency_spec.cpp
    source/package_identifier.cpp
    source/logging.cpp
    tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
    tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
    tests/stubs/local-dependency-plan/repository_query_stub.cpp
)
_moguet_test_production_complement(
    _moguet_local_dependency_plan_projection_forbidden_sources
    ${_moguet_local_dependency_plan_projection_test_sources}
)
moguet_add_cpp_test(
    local-dependency-plan-projection-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_local_dependency_plan_projection_test_sources}
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_local_dependency_plan_projection_forbidden_sources}
)

set(
    _moguet_local_source_workspace_test_sources
    tests/local_source_workspace_test.cpp
    source/local_source_workspace.cpp
    source/local_source_root.cpp
    source/local_package_metadata.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/logging.cpp
)
_moguet_test_production_complement(
    _moguet_local_source_workspace_forbidden_sources
    ${_moguet_local_source_workspace_test_sources}
)
moguet_add_cpp_test(
    local-source-workspace-test
    FIREWALL
    SOURCES ${_moguet_local_source_workspace_test_sources}
    DEFINITIONS MOGUET_ENABLE_LOCAL_SOURCE_WORKSPACE_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_local_source_workspace_forbidden_sources}
)

set(
    _moguet_local_source_build_test_sources
    tests/local_source_build_test.cpp
    source/local_source_build.cpp
    source/local_source_workspace.cpp
    source/local_source_root.cpp
    source/local_package_metadata.cpp
    source/local_dependency_plan_projection.cpp
    source/aur_constraint_metadata.cpp
    source/package_relation.cpp
    source/package_relation_observation.cpp
    source/package_relation_observation_adapter.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan.cpp
    source/dependency_plan_model.cpp
    source/package_relation_presentation.cpp
    source/dependency_spec.cpp
    source/build_plan_artifact_target_projection.cpp
    source/artifact_workspace.cpp
    source/artifact_identity.cpp
    source/artifact_identity_set.cpp
    source/artifact_identity_selection.cpp
    source/artifact_install_plan.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
    tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
    tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
    tests/stubs/local-dependency-plan/repository_query_stub.cpp
    tests/stubs/local-source-build/process_stub.cpp
)
_moguet_test_production_complement(
    _moguet_local_source_build_forbidden_sources
    ${_moguet_local_source_build_test_sources}
)
moguet_add_cpp_test(
    local-source-build-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_local_source_build_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
        MOGUET_ENABLE_LOCAL_SOURCE_WORKSPACE_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_local_source_build_forbidden_sources}
)

moguet_add_cpp_test(
    user-config-test
    SOURCES
        tests/user_config_test.cpp
        source/user_config.cpp
        source/cli_parser.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    package-identifier-test
    SOURCES
        tests/package_identifier_test.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    source-package-identity-test
    SOURCES
        tests/source_package_identity_test.cpp
        tests/vcs_source_identity_test.cpp
        tests/source_entry_parser_test.cpp
        tests/srcinfo_source_metadata_test.cpp
        source/source_package_identity.cpp
        source/vcs_source_identity.cpp
        source/source_entry_parser.cpp
        source/srcinfo_source_metadata.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_source_package_identity_projection_test_sources
    tests/source_package_identity_projection_test.cpp
    source/local_source_build.cpp
    source/local_source_workspace.cpp
    source/local_source_root.cpp
    source/local_package_metadata.cpp
    source/local_dependency_plan_projection.cpp
    source/aur_constraint_metadata.cpp
    source/package_relation.cpp
    source/package_relation_observation.cpp
    source/package_relation_observation_adapter.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan.cpp
    source/dependency_plan_model.cpp
    source/package_relation_presentation.cpp
    source/dependency_spec.cpp
    source/build_plan_artifact_target_projection.cpp
    source/artifact_workspace.cpp
    source/artifact_identity.cpp
    source/artifact_identity_set.cpp
    source/artifact_identity_selection.cpp
    source/artifact_install_plan.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
    source/source_package_compatibility.cpp
    source/source_package_identity.cpp
    source/source_package_identity_projection.cpp
    tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
    tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
    tests/stubs/local-dependency-plan/repository_query_stub.cpp
    tests/stubs/local-source-build/process_stub.cpp
)
_moguet_test_production_complement(
    _moguet_source_package_identity_projection_forbidden_sources
    ${_moguet_source_package_identity_projection_test_sources}
)
moguet_add_cpp_test(
    source-package-identity-projection-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_source_package_identity_projection_test_sources}
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_source_package_identity_projection_forbidden_sources}
)

moguet_add_cpp_test(
    source-package-compatibility-test
    SOURCES
        tests/source_package_compatibility_test.cpp
        source/source_package_compatibility.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

# The adapter reuses the dependency projection entry in the broader
# source-package identity TU. Section GC keeps unrelated local/artifact
# projection consumers outside this deterministic focused closure.
moguet_add_cpp_test(
    invocation-owned-cleanup-model-test
    ALPM_COMPILE
    SOURCES
        tests/invocation_owned_cleanup_model_test.cpp
        tests/invocation_owned_cleanup_adapter_test.cpp
        source/invocation_owned_cleanup_adapter.cpp
        source/invocation_owned_cleanup_model.cpp
        source/build_plan_artifact_target_projection.cpp
        source/dependency_constraint_presentation.cpp
        source/dependency_plan_model.cpp
        source/package_relation.cpp
        source/package_relation_presentation.cpp
        source/source_package_identity.cpp
        source/source_package_identity_projection.cpp
        source/dependency_constraint.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
)

moguet_add_cpp_test(
    reviewed-source-state-test
    SOURCES
        tests/reviewed_source_state_test.cpp
        source/reviewed_source_state.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-state-store-test
    SOURCES
        tests/reviewed_source_state_store_test.cpp
        source/reviewed_source_state_store.cpp
        source/reviewed_source_state.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
    DEFINITIONS MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-lifecycle-test
    SOURCES
        tests/reviewed_source_lifecycle_test.cpp
        source/reviewed_source_lifecycle.cpp
        source/reviewed_source_state.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    DEFINITIONS MOGUET_ENABLE_REVIEWED_SOURCE_LIFECYCLE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-acceptance-test
    SOURCES
        tests/reviewed_source_acceptance_test.cpp
        source/reviewed_source_production_failure.cpp
        source/reviewed_source_acceptance.cpp
        source/reviewed_source_lifecycle.cpp
        source/reviewed_source_trusted_review.cpp
        source/reviewed_source_presentation.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/reviewed_source_state.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
        source/interactive_confirmation.cpp
        source/logging.cpp
    DEFINITIONS
        MOGUET_ENABLE_REVIEWED_SOURCE_LIFECYCLE_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-pinned-build-test
    SOURCES
        tests/reviewed_source_pinned_build_test.cpp
        source/reviewed_source_package_base_lease.cpp
        source/reviewed_source_pinned_build.cpp
        source/reviewed_source_acceptance.cpp
        source/reviewed_source_lifecycle.cpp
        source/reviewed_source_trusted_review.cpp
        source/reviewed_source_presentation.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/reviewed_source_git_parser.cpp
        source/reviewed_source_state_store.cpp
        source/reviewed_source_state.cpp
        source/trusted_git.cpp
        source/persistent_checkout.cpp
        source/trusted_cache.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
        source/interactive_confirmation.cpp
        source/process.cpp
        source/logging.cpp
        source/localization.cpp
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-production-connection-test
    SOURCES
        tests/reviewed_source_production_connection_test.cpp
        source/source_build.cpp
        source/reviewed_source_production_failure.cpp
        source/reviewed_source_production_outcome.cpp
        source/cache_authority.cpp
        source/artifact_workspace.cpp
        source/reviewed_source_package_base_lease.cpp
        source/reviewed_source_pinned_build.cpp
        source/reviewed_source_acceptance.cpp
        source/reviewed_source_lifecycle.cpp
        source/reviewed_source_trusted_review.cpp
        source/reviewed_source_presentation.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/reviewed_source_git_parser.cpp
        source/reviewed_source_state_store.cpp
        source/reviewed_source_state.cpp
        source/trusted_git.cpp
        source/persistent_checkout.cpp
        source/trusted_cache.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
        source/interactive_confirmation.cpp
        source/process.cpp
        source/source_environment.cpp
        source/shell_words.cpp
        source/diagnostic_projection.cpp
        source/runtime_diagnostic.cpp
        source/logging.cpp
        source/localization.cpp
        tests/stubs/reviewed-source-production/execution_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_REVIEWED_SOURCE_PRODUCTION_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-projection-test
    SOURCES
        tests/reviewed_source_projection_test.cpp
        source/reviewed_source_projection.cpp
        source/reviewed_source_git_parser.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-review-test
    SOURCES
        tests/reviewed_source_review_test.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-patch-test
    SOURCES
        tests/reviewed_source_patch_test.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-presentation-test
    SOURCES
        tests/reviewed_source_presentation_test.cpp
        source/reviewed_source_presentation.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    DEFINITIONS MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    reviewed-source-git-test
    SOURCES
        tests/reviewed_source_git_test.cpp
        source/reviewed_source_trusted_review.cpp
        source/reviewed_source_lifecycle.cpp
        source/reviewed_source_presentation.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/reviewed_source_git_parser.cpp
        source/reviewed_source_package_base_lease.cpp
        source/trusted_git.cpp
        source/persistent_checkout.cpp
        source/trusted_cache.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/source_package_identity.cpp
        source/reviewed_source_state.cpp
        source/package_identifier.cpp
        source/process.cpp
        source/logging.cpp
        source/localization.cpp
    DEFINITIONS
        MOGUET_ENABLE_REVIEWED_SOURCE_LIFECYCLE_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_GIT_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
)

moguet_add_cpp_test(
    shell-words-test
    SOURCES
        tests/shell_words_test.cpp
        source/shell_words.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    source-environment-test
    SOURCES
        tests/source_environment_test.cpp
        source/source_environment.cpp
        source/source_preference.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_artifact_workspace_production_sources
    source/artifact_workspace.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/process.cpp
    source/logging.cpp
)
moguet_add_cpp_test(
    artifact-workspace-test
    SOURCES
        tests/artifact_workspace_test.cpp
        ${_moguet_artifact_workspace_production_sources}
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
        MOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_multiple_artifact_workspace_test_sources
    tests/multiple_artifact_workspace_test.cpp
    ${_moguet_artifact_workspace_production_sources}
)
_moguet_test_production_complement(
    _moguet_multiple_artifact_workspace_forbidden_sources
    ${_moguet_multiple_artifact_workspace_test_sources}
)
moguet_add_cpp_test(
    multiple-artifact-workspace-test
    FIREWALL
    SOURCES ${_moguet_multiple_artifact_workspace_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
        MOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_multiple_artifact_workspace_forbidden_sources}
)

set(
    _moguet_makepkg_assignment_precedence_test_sources
    tests/makepkg_assignment_precedence_test.cpp
    source/local_source_metadata_evaluation.cpp
    source/local_source_build.cpp
    source/local_source_root.cpp
    source/local_package_metadata.cpp
    source/artifact_workspace.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/process.cpp
    source/logging.cpp
)
_moguet_test_production_complement(
    _moguet_makepkg_assignment_precedence_forbidden_sources
    ${_moguet_makepkg_assignment_precedence_test_sources}
)
moguet_add_cpp_test(
    makepkg-assignment-precedence-test
    FIREWALL
    SOURCES ${_moguet_makepkg_assignment_precedence_test_sources}
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
    FORBIDDEN_SOURCES
        ${_moguet_makepkg_assignment_precedence_forbidden_sources}
)

set(
    _moguet_artifact_identity_production_sources
    source/artifact_identity.cpp
    source/artifact_identity_set.cpp
    source/artifact_workspace.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
)
moguet_add_cpp_test(
    artifact-identity-test
    SOURCES
        tests/artifact_identity_test.cpp
        ${_moguet_artifact_identity_production_sources}
        tests/stubs/artifact-identity/process_stub.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_multiple_artifact_identity_test_sources
    tests/multiple_artifact_identity_test.cpp
    ${_moguet_artifact_identity_production_sources}
    tests/stubs/artifact-identity/process_stub.cpp
)
_moguet_test_production_complement(
    _moguet_multiple_artifact_identity_forbidden_sources
    ${_moguet_multiple_artifact_identity_test_sources}
)
moguet_add_cpp_test(
    multiple-artifact-identity-test
    FIREWALL
    SOURCES ${_moguet_multiple_artifact_identity_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_multiple_artifact_identity_forbidden_sources}
)

set(
    _moguet_package_base_artifact_install_plan_test_sources
    tests/package_base_artifact_install_plan_test.cpp
    source/package_base_artifact_install_plan.cpp
    source/artifact_install_plan.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_package_base_artifact_install_plan_forbidden_sources
    ${_moguet_package_base_artifact_install_plan_test_sources}
)
moguet_add_cpp_test(
    package-base-artifact-install-plan-test
    FIREWALL
    SOURCES ${_moguet_package_base_artifact_install_plan_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_package_base_artifact_install_plan_forbidden_sources}
)

moguet_add_cpp_test(
    artifact-install-executor-test
    ALPM_COMPILE
    SOURCES
        tests/artifact_install_executor_test.cpp
        source/artifact_install_executor.cpp
        source/artifact_install_plan.cpp
        source/artifact_identity.cpp
        source/artifact_identity_set.cpp
        source/artifact_workspace.cpp
        source/package_metadata.cpp
        source/trusted_cache.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/source_environment.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        source/logging.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/artifact-install-executor/process_stub.cpp
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

set(
    _moguet_package_base_artifact_install_executor_test_sources
    tests/package_base_artifact_install_executor_test.cpp
    source/package_base_artifact_install_executor.cpp
    source/package_base_artifact_install_plan.cpp
    source/artifact_install_executor.cpp
    source/artifact_install_plan.cpp
    source/artifact_identity.cpp
    source/artifact_identity_set.cpp
    source/artifact_identity_selection.cpp
    source/artifact_workspace.cpp
    source/package_metadata.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
    tests/stubs/artifact-install-executor/process_stub.cpp
)
_moguet_test_production_complement(
    _moguet_package_base_artifact_install_executor_forbidden_sources
    ${_moguet_package_base_artifact_install_executor_test_sources}
)
moguet_add_cpp_test(
    package-base-artifact-install-executor-test
    FIREWALL
    ALPM_COMPILE
    SOURCES ${_moguet_package_base_artifact_install_executor_test_sources}
    DEFINITIONS MOGUET_ENABLE_PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_package_base_artifact_install_executor_forbidden_sources}
)

moguet_add_cpp_test(
    separated-source-build-test
    ALPM_COMPILE
    SOURCES
        tests/separated_source_build_test.cpp
        source/separated_source_build.cpp
        source/artifact_install_executor.cpp
        source/artifact_install_plan.cpp
        source/artifact_identity.cpp
        source/artifact_identity_set.cpp
        source/artifact_workspace.cpp
        source/package_metadata.cpp
        source/trusted_cache.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/source_environment.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        source/logging.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/artifact-install-executor/process_stub.cpp
    DEFINITIONS MOGUET_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

set(
    _moguet_separated_package_base_source_build_test_sources
    tests/separated_package_base_source_build_test.cpp
    source/separated_package_base_source_build.cpp
    source/package_base_artifact_install_executor.cpp
    source/package_base_artifact_install_plan.cpp
    source/artifact_install_executor.cpp
    source/artifact_install_plan.cpp
    source/artifact_identity.cpp
    source/artifact_identity_set.cpp
    source/artifact_identity_selection.cpp
    source/artifact_workspace.cpp
    source/package_metadata.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
    tests/stubs/artifact-install-executor/process_stub.cpp
)
_moguet_test_production_complement(
    _moguet_separated_package_base_source_build_forbidden_sources
    ${_moguet_separated_package_base_source_build_test_sources}
)
moguet_add_cpp_test(
    separated-package-base-source-build-test
    FIREWALL
    ALPM_COMPILE
    SOURCES ${_moguet_separated_package_base_source_build_test_sources}
    DEFINITIONS MOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_separated_package_base_source_build_forbidden_sources}
)

moguet_add_cpp_test(
    production-source-build-test
    ALPM_COMPILE
    CURL
    SOURCES
        tests/production_source_build_test.cpp
        source/app_config.cpp
        source/aur_constraint_metadata.cpp
        source/package_relation.cpp
        source/package_relation_observation.cpp
        source/package_relation_observation_adapter.cpp
        source/provider_selection.cpp
        source/source_install.cpp
        source/local_source_build_dependency_preparation.cpp
        source/local_dependency_plan_projection.cpp
        source/dependency_constraint.cpp
        source/dependency_constraint_presentation.cpp
        source/local_package_metadata.cpp
        source/cache_authority.cpp
        source/source_install_preparation.cpp
        source/source_build.cpp
        source/reviewed_source_production_failure.cpp
        source/reviewed_source_production_outcome.cpp
        source/reviewed_source_package_base_lease.cpp
        source/reviewed_source_pinned_build.cpp
        source/reviewed_source_acceptance.cpp
        source/reviewed_source_lifecycle.cpp
        source/reviewed_source_trusted_review.cpp
        source/reviewed_source_presentation.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/reviewed_source_git_parser.cpp
        source/reviewed_source_state_store.cpp
        source/reviewed_source_state.cpp
        source/source_package_identity.cpp
        source/interactive_confirmation.cpp
        source/diagnostic_projection.cpp
        source/runtime_diagnostic.cpp
        source/separated_source_build.cpp
        source/separated_package_base_source_build.cpp
        source/package_base_artifact_install_executor.cpp
        source/package_base_artifact_install_plan.cpp
        source/artifact_install_executor.cpp
        source/artifact_install_plan.cpp
        source/artifact_identity.cpp
        source/artifact_identity_set.cpp
        source/artifact_identity_selection.cpp
        source/artifact_workspace.cpp
        source/package_metadata.cpp
        source/trusted_cache.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/persistent_checkout.cpp
        tests/stubs/trusted-git/process_stub.cpp
        source/source_environment.cpp
        source/source_preference.cpp
        source/build_plan_artifact_target_projection.cpp
        source/dependency_plan.cpp
        source/dependency_plan_model.cpp
        source/package_relation_presentation.cpp
        source/dependency_spec.cpp
        source/package_identifier.cpp
        tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
        tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
        tests/stubs/local-dependency-plan/repository_query_stub.cpp
        source/shell_words.cpp
        source/logging.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/artifact-install-executor/process_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS
        MOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_PRODUCTION_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

moguet_add_cpp_test(
    process-capture-test
    SOURCES
        tests/process_capture_test.cpp
        source/process.cpp
        source/shell_words.cpp
        source/logging.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    process-stdin-fd-test
    SOURCES
        tests/process_stdin_fd_test.cpp
        source/process.cpp
        source/logging.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    aur-update-plan-test
    SOURCES
        tests/aur_update_plan_test.cpp
        tests/devel_package_classification_test.cpp
        tests/devel_update_model_test.cpp
        source/aur_update_plan.cpp
        source/devel_package_classification.cpp
        source/devel_update_model.cpp
        source/vcs_source_identity.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_upgrade_all_plan_test_sources
    tests/test-upgrade-all-plan.cpp
    source/upgrade_all_plan.cpp
)
_moguet_test_production_complement(
    _moguet_upgrade_all_plan_forbidden_sources
    ${_moguet_upgrade_all_plan_test_sources}
)
moguet_add_cpp_test(
    upgrade-all-plan-test
    FIREWALL
    SOURCES ${_moguet_upgrade_all_plan_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_upgrade_all_plan_forbidden_sources}
)

set(
    _moguet_system_source_upgrade_test_sources
    tests/system_source_upgrade_test.cpp
    source/system_source_upgrade.cpp
    source/unified_plan_projection.cpp
    source/unified_plan_observation.cpp
    source/dependency_constraint.cpp
    source/cache_authority.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
    tests/stubs/system-source-upgrade/phase_stub.cpp
)
_moguet_test_production_complement(
    _moguet_system_source_upgrade_forbidden_sources
    ${_moguet_system_source_upgrade_test_sources}
)
moguet_add_cpp_test(
    system-source-upgrade-test
    FIREWALL
    SOURCES ${_moguet_system_source_upgrade_test_sources}
    DEFINITIONS MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
    FORBIDDEN_SOURCES ${_moguet_system_source_upgrade_forbidden_sources}
)

moguet_add_cpp_test(
    aur-update-query-test
    SOURCES
        tests/aur_update_query_test.cpp
        source/aur_update_query.cpp
        source/aur_update_plan.cpp
        source/devel_package_classification.cpp
        source/devel_update_model.cpp
        source/vcs_source_identity.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        source/logging.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_aur_update_execution_preflight_test_sources
    tests/aur_update_execution_preflight_test.cpp
    source/aur_update_plan.cpp
    source/aur_update_execution_preflight.cpp
    source/devel_package_classification.cpp
    source/devel_update_model.cpp
    source/vcs_source_identity.cpp
    source/source_package_identity.cpp
    source/build_plan_artifact_target_projection.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/package_relation.cpp
    source/package_relation_presentation.cpp
    source/dependency_spec.cpp
    source/package_identifier.cpp
    tests/stubs/aur-update-execution-preflight/preflight_stub.cpp
)
set(
    _moguet_aur_update_execution_preflight_forbidden_sources
    source/build_plan_relation_assessment.cpp
    source/installed_package_relation_inventory.cpp
    source/package_relation_assessment.cpp
    source/package_relation_observation.cpp
    source/package_relation_observation_adapter.cpp
    source/package_constraint_metadata.cpp
    source/package_metadata.cpp
    source/repository_query.cpp
)
moguet_add_cpp_test(
    aur-update-execution-preflight-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_aur_update_execution_preflight_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_aur_update_execution_preflight_forbidden_sources}
)

moguet_add_cpp_test(
    aur-update-execution-preflight-integration-test
    ALPM_COMPILE
    SOURCES
        tests/aur_update_execution_preflight_integration_test.cpp
        source/aur_update_plan.cpp
        source/aur_update_execution_preflight.cpp
        source/devel_package_classification.cpp
        source/devel_update_model.cpp
        source/vcs_source_identity.cpp
        source/source_package_identity.cpp
        source/aur_constraint_metadata.cpp
        source/build_plan_relation_assessment.cpp
        source/installed_package_relation_inventory.cpp
        source/package_relation_assessment.cpp
        source/package_relation.cpp
        source/package_relation_observation.cpp
        source/package_relation_observation_adapter.cpp
        source/build_plan_artifact_target_projection.cpp
        source/dependency_constraint.cpp
        source/dependency_constraint_presentation.cpp
        source/dependency_plan.cpp
        source/dependency_plan_model.cpp
        source/package_relation_presentation.cpp
        source/dependency_spec.cpp
        source/package_constraint_metadata.cpp
        source/repository_query.cpp
        source/package_metadata.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/aur-update-execution-preflight-integration/integration_stub.cpp
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs/aur-update-execution-preflight-integration"
)

moguet_add_cpp_test(
    aur-update-execution-preparation-test
    ALPM_COMPILE
    REAL_ALPM
    SOURCES
        tests/aur_update_execution_preparation_test.cpp
        source/aur_update_execution_preparation.cpp
        source/build_plan_artifact_target_projection.cpp
        source/dependency_constraint.cpp
        source/dependency_constraint_presentation.cpp
        source/dependency_plan_model.cpp
        source/package_relation.cpp
        source/package_relation_presentation.cpp
        source/source_install_preparation.cpp
        source/source_package_identity.cpp
        source/source_environment.cpp
        source/shell_words.cpp
        source/package_identifier.cpp
        tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
    DEFINITIONS MOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    aur-update-execution-preparation-integration-test
    ALPM_COMPILE
    REAL_ALPM
    SOURCES
        tests/aur_update_execution_preparation_integration_test.cpp
        source/aur_update_execution_preparation.cpp
        source/build_plan_artifact_target_projection.cpp
        source/dependency_constraint.cpp
        source/dependency_constraint_presentation.cpp
        source/dependency_plan_model.cpp
        source/package_relation.cpp
        source/package_relation_presentation.cpp
        source/source_install_preparation.cpp
        source/source_package_identity.cpp
        source/source_preference.cpp
        source/source_environment.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
        source/shell_words.cpp
        source/package_identifier.cpp
        tests/stubs/aur-update-execution-preparation-integration/preparation_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS
        MOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_aur_update_execution_runner_test_sources
    tests/aur_update_execution_runner_test.cpp
    source/aur_update_execution_runner.cpp
    source/aur_update_execution_preparation.cpp
    source/build_plan_artifact_target_projection.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/package_relation.cpp
    source/package_relation_presentation.cpp
    source/source_install_preparation.cpp
    source/reviewed_source_state.cpp
    source/source_package_identity.cpp
    source/source_environment.cpp
    source/shell_words.cpp
    source/package_identifier.cpp
    tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
    tests/stubs/aur-update-execution-runner/execution_stub.cpp
)
set(
    _moguet_aur_update_execution_runner_forbidden_sources
    source/process.cpp
    source/checkout_fetch.cpp
    source/persistent_checkout.cpp
    source/artifact_workspace.cpp
    source/separated_source_build.cpp
    source/separated_package_base_source_build.cpp
    source/artifact_install_executor.cpp
    source/package_base_artifact_install_executor.cpp
    source/source_build.cpp
    source/source_install.cpp
)
moguet_add_cpp_test(
    aur-update-execution-runner-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_aur_update_execution_runner_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS
        MOGUET_ENABLE_AUR_UPDATE_EXECUTION_RUNNER_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_aur_update_execution_runner_forbidden_sources}
)

set(
    _moguet_aur_update_operation_result_test_sources
    tests/aur_update_operation_result_test.cpp
    source/aur_update_operation_result.cpp
    source/devel_package_classification.cpp
    source/devel_update_model.cpp
    source/vcs_source_identity.cpp
    source/aur_update_execution_preparation.cpp
    source/build_plan_artifact_target_projection.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/package_relation.cpp
    source/package_relation_presentation.cpp
    source/source_install_preparation.cpp
    source/source_package_identity.cpp
    source/source_environment.cpp
    source/shell_words.cpp
    source/package_identifier.cpp
    tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
)
set(
    _moguet_aur_update_operation_result_forbidden_sources
    ${_moguet_aur_update_execution_runner_forbidden_sources}
)
moguet_add_cpp_test(
    aur-update-operation-result-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_aur_update_operation_result_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_aur_update_operation_result_forbidden_sources}
)

set(
    _moguet_filtered_aur_update_operation_test_sources
    tests/filtered_aur_update_operation_test.cpp
    source/app_config.cpp
    source/provider_selection.cpp
    source/filtered_aur_update_operation.cpp
    source/upgrade_all_plan.cpp
    source/aur_update_query.cpp
    source/aur_update_plan.cpp
    source/devel_package_classification.cpp
    source/devel_update_model.cpp
    source/vcs_source_identity.cpp
    source/aur_update_execution_preflight.cpp
    source/aur_update_execution_preparation.cpp
    source/build_plan_artifact_target_projection.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/package_relation.cpp
    source/package_relation_presentation.cpp
    source/aur_update_execution_runner.cpp
    source/aur_update_operation_result.cpp
    source/source_install_preparation.cpp
    source/source_package_identity.cpp
    source/source_environment.cpp
    source/shell_words.cpp
    source/package_identifier.cpp
    source/dependency_spec.cpp
    source/logging.cpp
    tests/stubs/filtered-aur-update-operation/query_stub.cpp
    tests/stubs/aur-update-execution-preflight/preflight_stub.cpp
    tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
    tests/stubs/aur-update-execution-runner/execution_stub.cpp
)
set(
    _moguet_filtered_aur_update_operation_forbidden_sources
    source/aur_rpc.cpp
    source/package_metadata.cpp
    source/process.cpp
    source/checkout_fetch.cpp
    source/persistent_checkout.cpp
    source/artifact_workspace.cpp
    source/separated_source_build.cpp
    source/separated_package_base_source_build.cpp
    source/artifact_install_executor.cpp
    source/package_base_artifact_install_executor.cpp
    source/source_build.cpp
    source/source_install.cpp
)
moguet_add_cpp_test(
    filtered-aur-update-operation-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_filtered_aur_update_operation_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS
        MOGUET_ENABLE_AUR_UPDATE_EXECUTION_RUNNER_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_filtered_aur_update_operation_forbidden_sources}
)

set(
    _moguet_upgrade_all_operation_test_sources
    tests/upgrade_all_operation_test.cpp
    source/app_config.cpp
    source/provider_selection.cpp
    source/upgrade_all_operation.cpp
    source/upgrade_all_operation_result.cpp
    source/operation_state_model.cpp
    source/diagnostic_projection.cpp
    source/presentation_projection.cpp
    source/upgrade_all_presentation_projection.cpp
    source/system_source_upgrade.cpp
    source/unified_plan_projection.cpp
    source/unified_plan_observation.cpp
    source/cache_authority.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/filtered_aur_update_operation.cpp
    source/upgrade_all_plan.cpp
    source/aur_update_query.cpp
    source/aur_update_plan.cpp
    source/devel_package_classification.cpp
    source/devel_update_model.cpp
    source/vcs_source_identity.cpp
    source/aur_update_execution_preflight.cpp
    source/aur_update_execution_preparation.cpp
    source/build_plan_artifact_target_projection.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/package_relation.cpp
    source/package_relation_presentation.cpp
    source/aur_update_execution_runner.cpp
    source/aur_update_operation_result.cpp
    source/source_install_preparation.cpp
    source/source_package_identity.cpp
    source/source_environment.cpp
    source/shell_words.cpp
    source/package_identifier.cpp
    source/dependency_spec.cpp
    source/logging.cpp
    tests/stubs/upgrade-all-operation/operation_stub.cpp
)
_moguet_test_production_complement(
    _moguet_upgrade_all_operation_forbidden_sources
    ${_moguet_upgrade_all_operation_test_sources}
)
moguet_add_cpp_test(
    upgrade-all-operation-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_upgrade_all_operation_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_UPGRADE_ALL_OPERATION_TEST_HOOKS
        MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
    FORBIDDEN_SOURCES ${_moguet_upgrade_all_operation_forbidden_sources}
)

set(
    _moguet_cli_diagnostic_model_test_sources
    tests/cli_diagnostic_model_test.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/package_relation_presentation.cpp
    source/package_relation.cpp
    source/diagnostic_projection.cpp
    source/operation_state_model.cpp
    source/presentation_projection.cpp
)
_moguet_test_production_complement(
    _moguet_cli_diagnostic_model_forbidden_sources
    ${_moguet_cli_diagnostic_model_test_sources}
)
moguet_add_cpp_test(
    cli-diagnostic-model-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_cli_diagnostic_model_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_cli_diagnostic_model_forbidden_sources}
)

set(
    _moguet_runtime_cli_connection_test_sources
    tests/runtime_cli_connection_test.cpp
    source/cli_runtime_contract.cpp
    source/cli_public_projection.cpp
    source/runtime_diagnostic.cpp
    source/source_environment.cpp
)
_moguet_test_production_complement(
    _moguet_runtime_cli_connection_forbidden_sources
    ${_moguet_runtime_cli_connection_test_sources}
)
moguet_add_cpp_test(
    runtime-cli-connection-test
    FIREWALL
    SOURCES ${_moguet_runtime_cli_connection_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
    FORBIDDEN_SOURCES ${_moguet_runtime_cli_connection_forbidden_sources}
)

set(
    _moguet_dependency_plan_model_test_sources
    tests/dependency_plan_model_test.cpp
    source/dependency_plan.cpp
    source/dependency_plan_model.cpp
    source/package_relation_presentation.cpp
    source/aur_constraint_metadata.cpp
    source/package_relation.cpp
    source/package_relation_observation.cpp
    source/package_relation_observation_adapter.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_spec.cpp
    source/package_identifier.cpp
    source/logging.cpp
    tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
    tests/stubs/dependency-plan/aur_rpc_stub.cpp
    tests/stubs/dependency-plan/repository_query_stub.cpp
)
_moguet_test_production_complement(
    _moguet_dependency_plan_model_forbidden_sources
    ${_moguet_dependency_plan_model_test_sources}
)
moguet_add_cpp_test(
    dependency-plan-model-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_dependency_plan_model_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_dependency_plan_model_forbidden_sources}
)

set(
    _moguet_build_plan_artifact_target_projection_test_sources
    tests/build_plan_artifact_target_projection_test.cpp
    source/build_plan_artifact_target_projection.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/package_relation.cpp
    source/package_relation_presentation.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_build_plan_artifact_target_projection_forbidden_sources
    ${_moguet_build_plan_artifact_target_projection_test_sources}
)
moguet_add_cpp_test(
    build-plan-artifact-target-projection-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_build_plan_artifact_target_projection_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_build_plan_artifact_target_projection_forbidden_sources}
)

set(
    _moguet_unified_plan_observation_test_sources
    tests/unified_plan_observation_test.cpp
    source/unified_plan_observation.cpp
    source/package_relation.cpp
    source/dependency_constraint.cpp
)
_moguet_test_production_complement(
    _moguet_unified_plan_observation_forbidden_sources
    ${_moguet_unified_plan_observation_test_sources}
)
moguet_add_cpp_test(
    unified-plan-observation-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_unified_plan_observation_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_unified_plan_observation_forbidden_sources}
)

set(
    _moguet_unified_plan_projection_test_sources
    tests/unified_plan_projection_test.cpp
    source/unified_plan_observation.cpp
    source/build_plan_artifact_target_projection.cpp
    source/root_package_route_projection.cpp
    source/root_package_selection.cpp
    source/root_package_candidate.cpp
    source/local_source_root.cpp
    source/local_package_metadata.cpp
    source/local_dependency_plan_projection.cpp
    source/aur_constraint_metadata.cpp
    source/package_relation.cpp
    source/package_relation_observation.cpp
    source/package_relation_observation_adapter.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan.cpp
    source/dependency_plan_model.cpp
    source/package_relation_presentation.cpp
    source/dependency_spec.cpp
    source/package_identifier.cpp
    source/logging.cpp
    tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
    tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
    tests/stubs/local-dependency-plan/repository_query_stub.cpp
)
_moguet_test_production_complement(
    _moguet_unified_plan_projection_forbidden_sources
    ${_moguet_unified_plan_projection_test_sources}
    source/unified_plan_projection.cpp
)
moguet_add_cpp_test(
    unified-plan-projection-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_unified_plan_projection_test_sources}
    OBJECTS $<TARGET_OBJECTS:moguet_unified_plan_projection_object>
    OBJECT_PRODUCTION_SOURCES source/unified_plan_projection.cpp
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_unified_plan_projection_forbidden_sources}
)

set(
    _moguet_unified_plan_renderer_test_sources
    tests/unified_plan_renderer_test.cpp
    source/unified_plan_observation.cpp
    source/local_dependency_plan_projection.cpp
    source/aur_constraint_metadata.cpp
    source/package_relation.cpp
    source/package_relation_observation.cpp
    source/package_relation_observation_adapter.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan.cpp
    source/dependency_plan_model.cpp
    source/package_relation_presentation.cpp
    source/dependency_spec.cpp
    source/package_identifier.cpp
    source/logging.cpp
    tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
    tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
    tests/stubs/local-dependency-plan/repository_query_stub.cpp
)
_moguet_test_production_complement(
    _moguet_unified_plan_renderer_forbidden_sources
    ${_moguet_unified_plan_renderer_test_sources}
    source/unified_plan_renderer.cpp
)
moguet_add_cpp_test(
    unified-plan-renderer-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_unified_plan_renderer_test_sources}
    OBJECTS $<TARGET_OBJECTS:moguet_unified_plan_renderer_object>
    OBJECT_PRODUCTION_SOURCES source/unified_plan_renderer.cpp
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_unified_plan_renderer_forbidden_sources}
)

moguet_add_cpp_test(
    repository-query-test
    ALPM_COMPILE
    SOURCES
        tests/repository_query_test.cpp
        source/repository_query.cpp
        source/package_constraint_metadata.cpp
        source/dependency_constraint.cpp
        source/package_metadata.cpp
        source/dependency_spec.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/repository-query/process_stub.cpp
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

set(
    _moguet_artifact_install_plan_production_sources
    source/artifact_install_plan.cpp
    source/package_identifier.cpp
)
moguet_add_cpp_test(
    artifact-install-plan-test
    SOURCES
        tests/artifact_install_plan_test.cpp
        ${_moguet_artifact_install_plan_production_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_artifact_selection_model_test_sources
    tests/artifact_selection_model_test.cpp
    ${_moguet_artifact_install_plan_production_sources}
)
_moguet_test_production_complement(
    _moguet_artifact_selection_model_forbidden_sources
    ${_moguet_artifact_selection_model_test_sources}
)
moguet_add_cpp_test(
    artifact-selection-model-test
    FIREWALL
    SOURCES ${_moguet_artifact_selection_model_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_artifact_selection_model_forbidden_sources}
)

set(
    _moguet_artifact_identity_selection_test_sources
    tests/artifact_identity_selection_test.cpp
    source/artifact_identity_selection.cpp
    source/artifact_identity_set.cpp
    source/artifact_install_plan.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_artifact_identity_selection_forbidden_sources
    ${_moguet_artifact_identity_selection_test_sources}
)
moguet_add_cpp_test(
    artifact-identity-selection-test
    FIREWALL
    SOURCES ${_moguet_artifact_identity_selection_test_sources}
    DEFINITIONS MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_artifact_identity_selection_forbidden_sources}
)

moguet_add_cpp_test(
    package-metadata-test
    ALPM_COMPILE
    SOURCES
        tests/package_metadata_test.cpp
        source/package_metadata.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/package-metadata/process_stub.cpp
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
)

set(
    _moguet_provider_installed_state_test_sources
    tests/provider_installed_state_test.cpp
    source/provider_installed_state.cpp
    source/package_metadata.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
    tests/stubs/package-metadata/process_stub.cpp
)
_moguet_test_production_complement(
    _moguet_provider_installed_state_forbidden_sources
    ${_moguet_provider_installed_state_test_sources}
)
moguet_add_cpp_test(
    provider-installed-state-test
    FIREWALL
    ALPM_COMPILE
    SOURCES ${_moguet_provider_installed_state_test_sources}
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_provider_installed_state_forbidden_sources}
)

set(
    _moguet_dependency_constraint_test_sources
    tests/dependency_constraint_test.cpp
    source/dependency_constraint.cpp
)
_moguet_test_production_complement(
    _moguet_dependency_constraint_forbidden_sources
    ${_moguet_dependency_constraint_test_sources}
)
moguet_add_cpp_test(
    dependency-constraint-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_dependency_constraint_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_dependency_constraint_forbidden_sources}
)

set(
    _moguet_package_relation_test_sources
    tests/package_relation_test.cpp
    source/package_relation.cpp
    source/dependency_constraint.cpp
)
_moguet_test_production_complement(
    _moguet_package_relation_forbidden_sources
    ${_moguet_package_relation_test_sources}
)
moguet_add_cpp_test(
    package-relation-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_package_relation_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_package_relation_forbidden_sources}
)

set(
    _moguet_package_relation_observation_test_sources
    tests/package_relation_observation_test.cpp
    source/package_relation_observation.cpp
    source/package_relation.cpp
    source/dependency_constraint.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_package_relation_observation_forbidden_sources
    ${_moguet_package_relation_observation_test_sources}
)
moguet_add_cpp_test(
    package-relation-observation-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_package_relation_observation_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_package_relation_observation_forbidden_sources}
)

set(
    _moguet_package_relation_assessment_test_sources
    tests/package_relation_assessment_test.cpp
    source/package_relation_assessment.cpp
    source/package_relation_observation.cpp
    source/package_relation.cpp
    source/dependency_constraint.cpp
    source/package_identifier.cpp
)
_moguet_test_production_complement(
    _moguet_package_relation_assessment_forbidden_sources
    ${_moguet_package_relation_assessment_test_sources}
)
moguet_add_cpp_test(
    package-relation-assessment-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_package_relation_assessment_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_package_relation_assessment_forbidden_sources}
)

set(
    _moguet_package_constraint_metadata_test_sources
    tests/package_constraint_metadata_test.cpp
    source/installed_package_relation_inventory.cpp
    source/package_constraint_metadata.cpp
    source/package_metadata.cpp
    source/package_relation_observation_adapter.cpp
    source/package_relation_observation.cpp
    source/package_relation.cpp
    source/dependency_constraint.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
    tests/stubs/package-metadata/process_stub.cpp
)
_moguet_test_production_complement(
    _moguet_package_constraint_metadata_forbidden_sources
    ${_moguet_package_constraint_metadata_test_sources}
)
moguet_add_cpp_test(
    package-constraint-metadata-test
    FIREWALL
    ALPM_COMPILE
    SOURCES ${_moguet_package_constraint_metadata_test_sources}
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_package_constraint_metadata_forbidden_sources}
)

set(
    _moguet_aur_constraint_metadata_test_sources
    tests/aur_constraint_metadata_test.cpp
    source/aur_constraint_metadata.cpp
    source/package_relation.cpp
    source/dependency_constraint.cpp
)
_moguet_test_production_complement(
    _moguet_aur_constraint_metadata_forbidden_sources
    ${_moguet_aur_constraint_metadata_test_sources}
)
moguet_add_cpp_test(
    aur-constraint-metadata-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    SOURCES ${_moguet_aur_constraint_metadata_test_sources}
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_aur_constraint_metadata_forbidden_sources}
)

moguet_add_cpp_test(
    package-metadata-integration-test
    ALPM_COMPILE
    REAL_ALPM
    SOURCES
        tests/package_metadata_integration_test.cpp
        source/package_metadata.cpp
        source/package_identifier.cpp
        source/shell_words.cpp
        source/process.cpp
        source/logging.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    MOGUET_EXPECTED_CPP_TEST_TARGETS
    moguet-aur-update-command-test
    moguet-upgrade-all-command-test
    moguet-commands-sync-test
    moguet-commands-inspect-test
    moguet-test
    moguet-cli-localization-test
    moguet-app-config-test
    moguet-aur-rpc-validation-test
    moguet-source-install-characterization-test
    moguet-upgrade-baseline-metadata-test
    application-identity-test
    interactive-confirmation-test
    localization-test
    localization-missing-catalog-test
    xdg-paths-test
    xdg-directory-safety-test
    xdg-state-log-test
    trusted-cache-test
    moguet-root-execution-identity-test
    aur-rpc-envelope-validation-test
    app-config-test
    provider-selection-test
    root-package-candidate-test
    root-package-search-test
    root-package-selection-test
    root-package-route-projection-test
    local-package-metadata-test
    local-source-root-test
    local-dependency-plan-projection-test
    local-source-workspace-test
    local-source-build-test
    user-config-test
    package-identifier-test
    source-package-identity-test
    source-package-identity-projection-test
    source-package-compatibility-test
    invocation-owned-cleanup-model-test
    reviewed-source-state-test
    reviewed-source-state-store-test
    reviewed-source-lifecycle-test
    reviewed-source-acceptance-test
    reviewed-source-pinned-build-test
    reviewed-source-production-connection-test
    reviewed-source-projection-test
    reviewed-source-review-test
    reviewed-source-patch-test
    reviewed-source-presentation-test
    reviewed-source-git-test
    shell-words-test
    source-environment-test
    artifact-workspace-test
    multiple-artifact-workspace-test
    makepkg-assignment-precedence-test
    artifact-identity-test
    multiple-artifact-identity-test
    package-base-artifact-install-plan-test
    artifact-install-executor-test
    package-base-artifact-install-executor-test
    separated-source-build-test
    separated-package-base-source-build-test
    production-source-build-test
    process-capture-test
    process-stdin-fd-test
    aur-update-plan-test
    upgrade-all-plan-test
    system-source-upgrade-test
    aur-update-query-test
    aur-update-execution-preflight-test
    aur-update-execution-preflight-integration-test
    aur-update-execution-preparation-test
    aur-update-execution-preparation-integration-test
    aur-update-execution-runner-test
    aur-update-operation-result-test
    filtered-aur-update-operation-test
    upgrade-all-operation-test
    cli-diagnostic-model-test
    runtime-cli-connection-test
    dependency-plan-model-test
    build-plan-artifact-target-projection-test
    unified-plan-observation-test
    unified-plan-projection-test
    unified-plan-renderer-test
    repository-query-test
    artifact-install-plan-test
    artifact-selection-model-test
    artifact-identity-selection-test
    package-metadata-test
    provider-installed-state-test
    dependency-constraint-test
    package-relation-test
    package-relation-observation-test
    package-relation-assessment-test
    package-constraint-metadata-test
    aur-constraint-metadata-test
    package-metadata-integration-test
)

set(
    MOGUET_EXPECTED_CPP_TEST_SUPPORT_SOURCES
    tests/commands_inspect_aur_stub.cpp
    tests/stubs/artifact-identity/process_stub.cpp
    tests/stubs/artifact-install-executor/process_stub.cpp
    tests/stubs/aur-update-command/operation_stub.cpp
    tests/stubs/aur-update-execution-preflight-integration/integration_stub.cpp
    tests/stubs/aur-update-execution-preflight/preflight_stub.cpp
    tests/stubs/aur-update-execution-preparation-integration/preparation_stub.cpp
    tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
    tests/stubs/aur-update-execution-runner/execution_stub.cpp
    tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
    tests/stubs/commands-inspect/repository_query_stub.cpp
    tests/stubs/commands-sync/aur_rpc_stub.cpp
    tests/stubs/commands-sync/root_package_search_stub.cpp
    tests/stubs/dependency-plan/aur_rpc_stub.cpp
    tests/stubs/dependency-plan/repository_query_stub.cpp
    tests/stubs/filtered-aur-update-operation/query_stub.cpp
    tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
    tests/stubs/local-dependency-plan/repository_query_stub.cpp
    tests/stubs/local-source-build/process_stub.cpp
    tests/stubs/package-metadata/alpm_stub.cpp
    tests/stubs/package-metadata/process_stub.cpp
    tests/stubs/repository-query/process_stub.cpp
    tests/stubs/reviewed-source-production/execution_stub.cpp
    tests/stubs/root-package-search/search_stub.cpp
    tests/stubs/runtime-identity/geteuid_stub.cpp
    tests/stubs/system-source-upgrade/phase_stub.cpp
    tests/stubs/trusted-git/process_stub.cpp
    tests/stubs/upgrade-all-command/operation_stub.cpp
    tests/stubs/upgrade-all-operation/operation_stub.cpp
)

set(
    MOGUET_EXPECTED_CPP_TEST_FIREWALL_TARGETS
    moguet-aur-update-command-test
    moguet-upgrade-all-command-test
    moguet-commands-sync-test
    moguet-commands-inspect-test
    moguet-test
    moguet-cli-localization-test
    moguet-app-config-test
    moguet-aur-rpc-validation-test
    moguet-source-install-characterization-test
    moguet-upgrade-baseline-metadata-test
    root-package-candidate-test
    root-package-search-test
    root-package-selection-test
    root-package-route-projection-test
    local-package-metadata-test
    local-source-root-test
    local-dependency-plan-projection-test
    local-source-workspace-test
    local-source-build-test
    source-package-identity-projection-test
    multiple-artifact-workspace-test
    makepkg-assignment-precedence-test
    multiple-artifact-identity-test
    package-base-artifact-install-plan-test
    package-base-artifact-install-executor-test
    separated-package-base-source-build-test
    upgrade-all-plan-test
    system-source-upgrade-test
    aur-update-execution-preflight-test
    aur-update-execution-runner-test
    aur-update-operation-result-test
    filtered-aur-update-operation-test
    upgrade-all-operation-test
    cli-diagnostic-model-test
    runtime-cli-connection-test
    dependency-plan-model-test
    build-plan-artifact-target-projection-test
    unified-plan-observation-test
    unified-plan-projection-test
    unified-plan-renderer-test
    artifact-selection-model-test
    artifact-identity-selection-test
    provider-installed-state-test
    dependency-constraint-test
    package-relation-test
    package-relation-observation-test
    package-relation-assessment-test
    package-constraint-metadata-test
    aur-constraint-metadata-test
)

# These hashes are an independent fail-closed ledger for the 49 legacy link
# firewalls.  Before changing any entry, compare the target's complete CMake
# source/link profile with the legacy Make closure and re-establish parity;
# never derive or update this expected ledger from the configure-time actual
# descriptors automatically.
set(
    MOGUET_EXPECTED_CPP_TEST_FIREWALL_DESCRIPTORS
    moguet-aur-update-command-test=0974e3a3ec501a12d4c59ba12261560ea24eb9366e9f07332f66c9f3257613af
    moguet-upgrade-all-command-test=88c77e92e072839aab7dd166873ee658bdb548ffd02b1302867c4ec2cec87481
    moguet-commands-sync-test=a6d09d85d7fb2b2c8ad4bb55bd8d40533a7c5f319024fa9a9b2c7deca13aa0ba
    moguet-commands-inspect-test=7691fc0830de54b8ab70810cd932f44f4a05a9ce970c49abd6e9eea3d1a16d93
    moguet-test=2186fb1ef17ef34bab3fc365e79ae71a475d80115909a7449e43b3ae2353ca6d
    moguet-cli-localization-test=7a5315f83de16b6526828aff5fc543cb658359966c3272d35d133ac4481cd521
    moguet-app-config-test=c359ed137f8f7870aa00e8cf5dd987536f4fd27edda3f2f6512ff2dc9d201e05
    moguet-aur-rpc-validation-test=cc41e6a925bee982bef13ebcf4b7dc43b94ba7a2a8e3e5d32d10469f77cad495
    moguet-source-install-characterization-test=b1221702488b32a9fc72b4dc75ad0da864a65023886838475c355b26bcb91ad0
    moguet-upgrade-baseline-metadata-test=840e7079dbd6d3d83bee442bb6a0fea18c4fdbc81ae2afab1a7a972a83b1e297
    root-package-candidate-test=a7189f83e7cd79568c55572b0215a841cba8106d79235f936e6542660e06f8c3
    root-package-search-test=bff84a81f65eb748fd8ac1449bfb3f83614d1a1a4dfe196741870c9a9ff76c05
    root-package-selection-test=d36173f4334efb926d7d36a4c8a092863894f2e8821ec2cd6cd7d0318be9a88f
    root-package-route-projection-test=4117c67899064ef2956ba0a7257a6e05ad8a07786a53fa4e1e38791e80e27071
    local-package-metadata-test=301eeb327efac71d5c9e127766f519f6853a498ea048db1fd3ed0f117bd36568
    local-source-root-test=6e5c77b740706a2535578991385f315ea652d2f0ca3699b78eaab68589a90867
    local-dependency-plan-projection-test=1267a76fd82dcffb083f1a90f281747277400c6f030e130ea17dbe5583d8cedd
    local-source-workspace-test=bdfb3397f56646769abbf99537c50a3974cc5641908bf38eb9560e6963cf4a06
    local-source-build-test=f7d191a831e80a6da50ac4181df05a122e61ab17150fc02334a9e2e9e6caa0d9
    source-package-identity-projection-test=2d9378c56c8b3f943ec32e9eb75cc148ad1aaffad65bf045d7d87fc6cfe83bd5
    multiple-artifact-workspace-test=8ed7df7c56d2d7a85237c8dbfbe8599fabeff9c48c39106cf3bf51a1188c0c5f
    makepkg-assignment-precedence-test=4419bb8858790736f9cd54dda82b4dd6408ddfdf67c90afef8e36326dc096ab5
    multiple-artifact-identity-test=011fc89230edc02e265cd452772dfedb65edfc8fb19dbe552f6f3c8ce8edc9ac
    package-base-artifact-install-plan-test=76ee63385978516455bf2bef37debcb502209e95311e1ce2972192dd6e509f1a
    package-base-artifact-install-executor-test=388ddfc6f31dbf68d8fb0af232d2f2bd642cacdd3475a923094a38376f3c47f5
    separated-package-base-source-build-test=fda571d20da8f18151c1ff391e881d992d4ca44f1c834f2b3c5e2a8125501fc5
    upgrade-all-plan-test=ac7d2c6218f2d866926571605ea6c16c604819a71c92d7e716ed52bec8987390
    system-source-upgrade-test=d527a55046fe712c01cd3e9b1fe21d0aa016dff3e4a466b75cf267ff68549478
    aur-update-execution-preflight-test=167eca6cef76a54712dd3281a015d38e406ae9fde1a264919d69b115a743cfd3
    aur-update-execution-runner-test=6bc5943cf6a1bdd3dacb4956d45aa9e670ae0467dc417176b9c4b9c0b2001818
    aur-update-operation-result-test=794c70c37241de19fa40d3e5369fadebe282fb321fd0e3771b8b3d89e0a369d2
    filtered-aur-update-operation-test=7fbff061914929873f29f271c3dc7608304895c6be1eb71817e5ad77d75d0372
    upgrade-all-operation-test=cf1be1fce88b4ee0f17731e8ad5e8a2dd6a18ded32e30821f52a1ea14f2f73f0
    cli-diagnostic-model-test=75c32ef969ab17a05da8a161df94a7c84c976b982d2e2a294d5fd0a7ed69ecb9
    runtime-cli-connection-test=a30d7a2085869c424d70e1242af9c9dc5bf37a6d64de528dfd2340b9307047c7
    dependency-plan-model-test=6446653967682d345cd78a2bcc9706ecbdbbb99274c9458de6f4c5e6238720c6
    build-plan-artifact-target-projection-test=0637ac389121a42485c696fb4b4cbeacf66d456de03f217d2bede1209234e60e
    unified-plan-observation-test=986a8ec6d2e193abdf0e0329d055bef6b55cf84381dfe677f228fa5c5c297cdb
    unified-plan-projection-test=23b9c12b41c9df5ffbaa101cd39ec2b5c0102003b8e3040bcd0905b7d9a17240
    unified-plan-renderer-test=3fee8691e2ac576a4e3c724f715f401f5bf63bafd4253127438c638d6c0da9a9
    artifact-selection-model-test=2c47b82222a05cac3c82b8f74043f59cee1cd67f47fc256752c34630b4b85f3a
    artifact-identity-selection-test=bfe66e14b60ec23dfd8d4e0af1a2e74aa34e50a3e5fa2020abda26b649a6640b
    provider-installed-state-test=b7e33920ac6226ab93caf31294fbbc9f8d9f9f316a1b818031c4f2084b8f420a
    dependency-constraint-test=83364aa059eff3f6b8eb8c16fbc3f2600ad2ac946d82cffc9db8309a5fc534e6
    package-relation-test=68298fc6dfb6ab8db2878e1e73004d331b6e9b91a408f5e08efcbb2461ed4d92
    package-relation-observation-test=ec3385ff867cd41107b6a292896ca19ad1570bc9c430092fdafa95116745cc74
    package-relation-assessment-test=2dba05f85f23c06ff6a06350867ab916e5b6b942c6a9390d933f0b220feda20d
    package-constraint-metadata-test=73a00ed74ae86e56adaa96469e957622efa11cfdefe4b73fa54e4f156b8e194e
    aur-constraint-metadata-test=721773c88e9ed9cab107dce7cb712ef7d8d949a545b69aec41f141e2aeeecdfa
)
