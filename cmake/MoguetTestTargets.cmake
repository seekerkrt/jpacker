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
    source/artifact_archive_metadata.cpp
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
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_TEST_CONFIG_PATH
        MOGUET_ENABLE_SYSTEM_AUR_UPDATE_PRESENTATION_TEST_HOOKS
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
    source/artifact_archive_metadata.cpp
    source/upgrade_all_operation.cpp
)
list(
    APPEND _moguet_upgrade_all_command_test_sources
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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
    source/artifact_archive_metadata.cpp
    source/aur_rpc.cpp
    source/root_package_search.cpp
)
list(
    APPEND _moguet_commands_sync_test_sources
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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
    source/artifact_archive_metadata.cpp
    source/aur_rpc.cpp
    source/repository_query.cpp
)
list(
    APPEND _moguet_commands_inspect_test_sources
    tests/commands_inspect_aur_stub.cpp
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
        "MOGUET_LOCALE_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/locale\""
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    FORBIDDEN_SOURCES ${_moguet_commands_inspect_forbidden_sources}
)

set(_moguet_full_cli_test_sources ${MOGUET_PRODUCTION_SOURCES})
list(
    REMOVE_ITEM _moguet_full_cli_test_sources
    source/artifact_archive_metadata.cpp
)
list(
    APPEND _moguet_full_cli_test_sources
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
)

moguet_add_cpp_test(
    moguet-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    CURL
    SOURCES ${_moguet_full_cli_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    moguet-cli-localization-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    CURL
    SOURCES ${_moguet_full_cli_test_sources}
    DEFINITIONS
        "MOGUET_LOCALE_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/locale\""
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    moguet-app-config-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    CURL
    SOURCES ${_moguet_full_cli_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_TEST_CONFIG_PATH
        MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_aur_rpc_validation_test_sources
    ${MOGUET_PRODUCTION_SOURCES}
    tests/stubs/package-metadata/alpm_stub.cpp
)
list(
    REMOVE_ITEM _moguet_aur_rpc_validation_test_sources
    source/artifact_archive_metadata.cpp
)
list(
    APPEND _moguet_aur_rpc_validation_test_sources
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
)
moguet_add_cpp_test(
    moguet-aur-rpc-validation-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_aur_rpc_validation_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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
    source/artifact_archive_metadata.cpp
    source/moguet.cpp
)
list(
    APPEND _moguet_source_install_characterization_test_sources
    tests/source_install_characterization.cpp
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
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
list(
    REMOVE_ITEM _moguet_upgrade_baseline_metadata_test_sources
    source/artifact_archive_metadata.cpp
)
list(
    APPEND _moguet_upgrade_baseline_metadata_test_sources
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
)
moguet_add_cpp_test(
    moguet-upgrade-baseline-metadata-test
    FIREWALL
    ALPM_COMPILE
    CURL
    SOURCES ${_moguet_upgrade_baseline_metadata_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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
    xdg-generation-store-test
    SOURCES
        tests/xdg_generation_store_test.cpp
        source/xdg_generation_store.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
    DEFINITIONS MOGUET_ENABLE_XDG_GENERATION_STORE_TEST_HOOKS
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
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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

moguet_add_cpp_test(
    installed-artifact-binding-test
    SOURCES
        tests/installed_artifact_binding_test.cpp
        source/installed_artifact_binding.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    DEFINITIONS MOGUET_ENABLE_INSTALLED_ARTIFACT_BINDING_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    devel-build-provenance-test
    SOURCES
        tests/devel_build_provenance_test.cpp
        source/devel_build_provenance.cpp
        source/installed_artifact_binding.cpp
        source/vcs_source_identity.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
    DEFINITIONS
        MOGUET_ENABLE_DEVEL_BUILD_PROVENANCE_TEST_HOOKS
        MOGUET_ENABLE_INSTALLED_ARTIFACT_BINDING_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    devel-build-provenance-store-test
    SOURCES
        tests/devel_build_provenance_store_test.cpp
        source/devel_build_provenance_store.cpp
        source/devel_build_provenance_codec.cpp
        source/devel_build_provenance.cpp
        source/installed_artifact_binding.cpp
        source/vcs_source_identity.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
        source/xdg_generation_store.cpp
        source/xdg_directory_safety.cpp
        source/xdg_paths.cpp
    DEFINITIONS
        MOGUET_ENABLE_XDG_GENERATION_STORE_TEST_HOOKS
        MOGUET_ENABLE_DEVEL_BUILD_PROVENANCE_TEST_HOOKS
        MOGUET_ENABLE_INSTALLED_ARTIFACT_BINDING_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    git-remote-revision-observer-test
    CURL
    SOURCES
        tests/git_remote_revision_observer_test.cpp
        source/git_remote_revision_observer.cpp
        source/vcs_source_identity.cpp
        source/source_package_identity.cpp
        source/package_identifier.cpp
        source/process.cpp
        source/logging.cpp
        source/trusted_git_process_policy.cpp
    DEFINITIONS MOGUET_ENABLE_GIT_REMOTE_REVISION_OBSERVER_TEST_HOOKS
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
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
    DEFINITIONS MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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
        tests/remote_aur_cleanup_candidate_collector_test.cpp
        tests/source_artifact_install_receipt_evidence_test.cpp
        tests/trusted_alpm_receipt_test.cpp
        source/invocation_owned_cleanup_adapter.cpp
        source/remote_aur_cleanup_candidate_collector.cpp
        source/invocation_owned_cleanup_model.cpp
        source/package_metadata.cpp
        source/source_artifact_install_receipt_evidence.cpp
        source/build_plan_artifact_target_projection.cpp
        source/dependency_constraint_presentation.cpp
        source/dependency_plan_model.cpp
        source/package_relation.cpp
        source/package_relation_presentation.cpp
        source/source_package_identity.cpp
        source/source_package_identity_projection.cpp
        source/dependency_constraint.cpp
        source/package_identifier.cpp
        source/trusted_alpm_receipt_helper_state.cpp
        source/trusted_alpm_receipt_protocol.cpp
        source/trusted_alpm_receipt_transport.cpp
        source/logging.cpp
        source/shell_words.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_CLEANUP_INVOCATION_SESSION_TEST_HOOKS
        MOGUET_ENABLE_INVOCATION_TRANSACTION_LEDGER_TEST_HOOKS
        MOGUET_ENABLE_REMOTE_AUR_CLEANUP_COLLECTOR_TEST_HOOKS
        MOGUET_ENABLE_SOURCE_ARTIFACT_INSTALL_RECEIPT_TEST_HOOKS
        MOGUET_ENABLE_TRUSTED_ALPM_RECEIPT_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
)

set(
    _moguet_source_artifact_install_trusted_transport_test_sources
    tests/source_artifact_install_trusted_transport_test.cpp
    source/source_artifact_install_trusted_protocol.cpp
    source/source_artifact_install_trusted_helper_state.cpp
    source/source_artifact_install_trusted_transport.cpp
    source/source_artifact_install_receipt_evidence.cpp
    source/package_base_artifact_install_executor.cpp
    source/package_base_artifact_install_plan.cpp
    source/artifact_install_executor.cpp
    source/artifact_install_plan.cpp
    source/artifact_archive_metadata.cpp
    source/artifact_identity.cpp
    source/artifact_identity_set.cpp
    source/artifact_identity_selection.cpp
    source/artifact_workspace.cpp
    source/package_metadata.cpp
    source/source_package_identity.cpp
    source/source_package_identity_projection.cpp
    source/invocation_owned_cleanup_model.cpp
    source/trusted_alpm_receipt_helper_state.cpp
    source/trusted_alpm_receipt_protocol.cpp
    source/trusted_alpm_receipt_transport.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
)
moguet_add_cpp_test(
    source-artifact-install-trusted-transport-test
    ALPM_COMPILE
    REAL_ALPM
    SOURCES
        ${_moguet_source_artifact_install_trusted_transport_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_SOURCE_ARTIFACT_INSTALL_TRUSTED_TRANSPORT_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
)

# Installed-container evidence invokes the production transport with real
# process and libalpm boundaries. It is deliberately not a CTest or ordinary
# all target: only the owner-specific container lane builds and runs it.
set(
    _moguet_source_artifact_install_installed_fixture_sources
    tests/source_artifact_install_installed_fixture.cpp
    source/source_install.cpp
    source/source_install_preparation.cpp
    source/cache_authority.cpp
    source/separated_package_base_source_build.cpp
    source/separated_source_build.cpp
    source/reviewed_source_production_failure.cpp
    source/reviewed_source_production_outcome.cpp
    source/build_plan_artifact_target_projection.cpp
    source/dependency_constraint.cpp
    source/dependency_constraint_presentation.cpp
    source/dependency_plan_model.cpp
    source/invocation_owned_cleanup_adapter.cpp
    source/remote_aur_cleanup_candidate_collector.cpp
    source/package_relation.cpp
    source/package_relation_presentation.cpp
    source/source_artifact_install_trusted_protocol.cpp
    source/source_artifact_install_trusted_transport.cpp
    source/source_artifact_install_receipt_evidence.cpp
    source/package_base_artifact_install_executor.cpp
    source/package_base_artifact_install_plan.cpp
    source/artifact_install_executor.cpp
    source/artifact_install_plan.cpp
    source/artifact_archive_metadata.cpp
    source/artifact_identity.cpp
    source/artifact_identity_set.cpp
    source/artifact_identity_selection.cpp
    source/artifact_workspace.cpp
    source/package_metadata.cpp
    source/source_package_identity.cpp
    source/source_package_identity_projection.cpp
    source/invocation_owned_cleanup_model.cpp
    source/trusted_alpm_receipt_protocol.cpp
    source/trusted_alpm_receipt_transport.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_environment.cpp
    source/package_identifier.cpp
    source/shell_words.cpp
    source/logging.cpp
    source/process.cpp
)
add_executable(
    moguet-source-artifact-install-installed-fixture
    EXCLUDE_FROM_ALL
    ${_moguet_source_artifact_install_installed_fixture_sources}
)
set_target_properties(
    moguet-source-artifact-install-installed-fixture
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
target_link_libraries(
    moguet-source-artifact-install-installed-fixture
    PRIVATE
        moguet_external_cppflags
        moguet_build_contract
        moguet_alpm_compile_contract
        moguet_test_real_alpm_link_contract
)
target_include_directories(
    moguet-source-artifact-install-installed-fixture
    PRIVATE "${_moguet_test_source_include_dir}"
)
target_compile_definitions(
    moguet-source-artifact-install-installed-fixture
    PRIVATE MOGUET_ENABLE_REMOTE_AUR_CLEANUP_RUNNER_TEST_HOOKS
)
target_compile_options(
    moguet-source-artifact-install-installed-fixture
    PRIVATE -ffunction-sections -fdata-sections
)
target_link_options(
    moguet-source-artifact-install-installed-fixture
    PRIVATE LINKER:--gc-sections
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
        source/xdg_generation_store.cpp
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
        source/devel_build_provenance_reviewed_binding.cpp
        source/devel_build_provenance.cpp
        source/installed_artifact_binding.cpp
        source/vcs_source_identity.cpp
        source/reviewed_source_acceptance.cpp
        source/reviewed_source_lifecycle.cpp
        source/reviewed_source_trusted_review.cpp
        source/reviewed_source_presentation.cpp
        source/reviewed_source_review.cpp
        source/reviewed_source_patch.cpp
        source/reviewed_source_projection.cpp
        source/reviewed_source_git_parser.cpp
        source/reviewed_source_state_store.cpp
        source/xdg_generation_store.cpp
        source/reviewed_source_state.cpp
        source/trusted_git.cpp
        source/trusted_git_process_policy.cpp
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

set(
    _moguet_invocation_owned_source_build_context_test_sources
    tests/invocation_owned_source_build_context_test.cpp
    source/invocation_owned_source_build_context.cpp
    source/reviewed_source_package_base_lease.cpp
    source/reviewed_source_pinned_build.cpp
    source/devel_build_provenance_reviewed_binding.cpp
    source/devel_build_provenance.cpp
    source/installed_artifact_binding.cpp
    source/vcs_source_identity.cpp
    source/reviewed_source_acceptance.cpp
    source/reviewed_source_lifecycle.cpp
    source/reviewed_source_trusted_review.cpp
    source/reviewed_source_presentation.cpp
    source/reviewed_source_review.cpp
    source/reviewed_source_patch.cpp
    source/reviewed_source_projection.cpp
    source/reviewed_source_git_parser.cpp
    source/reviewed_source_state_store.cpp
    source/xdg_generation_store.cpp
    source/reviewed_source_state.cpp
    source/trusted_git.cpp
    source/trusted_git_process_policy.cpp
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
)
set(
    _moguet_invocation_owned_source_build_context_forbidden_sources
    ${MOGUET_PRODUCTION_SOURCES}
)
list(
    REMOVE_ITEM
    _moguet_invocation_owned_source_build_context_forbidden_sources
    ${_moguet_invocation_owned_source_build_context_test_sources}
)
moguet_add_cpp_test(
    invocation-owned-source-build-context-test
    SOURCES ${_moguet_invocation_owned_source_build_context_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        MOGUET_ENABLE_INVOCATION_OWNED_SOURCE_BUILD_CONTEXT_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_invocation_owned_source_build_context_forbidden_sources}
)

set(
    _moguet_evaluated_devel_source_build_test_sources
    tests/evaluated_devel_source_build_test.cpp
    source/evaluated_devel_source_build.cpp
    source/artifact_archive_metadata.cpp
    source/artifact_workspace.cpp
    source/invocation_owned_source_build_context.cpp
    source/reviewed_source_package_base_lease.cpp
    source/reviewed_source_pinned_build.cpp
    source/devel_build_provenance_reviewed_binding.cpp
    source/devel_build_provenance.cpp
    source/installed_artifact_binding.cpp
    source/vcs_source_identity.cpp
    source/reviewed_source_acceptance.cpp
    source/reviewed_source_lifecycle.cpp
    source/reviewed_source_trusted_review.cpp
    source/reviewed_source_presentation.cpp
    source/reviewed_source_review.cpp
    source/reviewed_source_patch.cpp
    source/reviewed_source_projection.cpp
    source/reviewed_source_git_parser.cpp
    source/reviewed_source_state_store.cpp
    source/xdg_generation_store.cpp
    source/reviewed_source_state.cpp
    source/trusted_git.cpp
    source/trusted_git_process_policy.cpp
    source/git_remote_revision_observer.cpp
    source/persistent_checkout.cpp
    source/trusted_cache.cpp
    source/xdg_directory_safety.cpp
    source/xdg_paths.cpp
    source/source_package_identity.cpp
    source/package_identifier.cpp
    source/local_package_metadata.cpp
    source/srcinfo_source_metadata.cpp
    source/source_entry_parser.cpp
    source/source_environment.cpp
    source/shell_words.cpp
    source/interactive_confirmation.cpp
    source/process.cpp
    source/logging.cpp
    source/localization.cpp
)
set(
    _moguet_evaluated_devel_source_build_forbidden_sources
    ${MOGUET_PRODUCTION_SOURCES}
)
list(
    REMOVE_ITEM
    _moguet_evaluated_devel_source_build_forbidden_sources
    ${_moguet_evaluated_devel_source_build_test_sources}
)
moguet_add_cpp_test(
    evaluated-devel-source-build-test
    FIREWALL
    ALPM_COMPILE
    REAL_ALPM
    CURL
    SOURCES ${_moguet_evaluated_devel_source_build_test_sources}
    DEFINITIONS
        MOGUET_ENABLE_TEST_OVERRIDES
        MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
        MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        MOGUET_ENABLE_INVOCATION_OWNED_SOURCE_BUILD_CONTEXT_TEST_HOOKS
        MOGUET_ENABLE_EVALUATED_DEVEL_SOURCE_BUILD_TEST_HOOKS
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
    FORBIDDEN_SOURCES
        ${_moguet_evaluated_devel_source_build_forbidden_sources}
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
        source/xdg_generation_store.cpp
        source/reviewed_source_state.cpp
        source/trusted_git.cpp
        source/trusted_git_process_policy.cpp
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
        source/trusted_git_process_policy.cpp
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
    ALPM_COMPILE
    REAL_ALPM
    SOURCES
        tests/artifact_identity_test.cpp
        ${_moguet_artifact_identity_production_sources}
        source/artifact_archive_metadata.cpp
        tests/stubs/artifact-identity/archive_metadata_stub.cpp
        tests/stubs/artifact-identity/process_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_REAL_RETAINED_DESCRIPTOR_ARCHIVE_QUERY
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_multiple_artifact_identity_test_sources
    tests/multiple_artifact_identity_test.cpp
    ${_moguet_artifact_identity_production_sources}
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
    DEFINITIONS MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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
        tests/stubs/artifact-identity/archive_metadata_stub.cpp
        tests/stubs/artifact-install-executor/process_stub.cpp
    DEFINITIONS MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
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
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_HOOKS
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
        tests/stubs/artifact-identity/archive_metadata_stub.cpp
        tests/stubs/artifact-install-executor/process_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS
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
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS
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
        source/xdg_generation_store.cpp
        source/reviewed_source_state.cpp
        source/source_package_identity.cpp
        source/source_package_identity_projection.cpp
        source/interactive_confirmation.cpp
        source/invocation_owned_cleanup_model.cpp
        source/invocation_owned_cleanup_adapter.cpp
        source/remote_aur_cleanup_candidate_collector.cpp
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
        source/trusted_alpm_receipt_protocol.cpp
        tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
        tests/stubs/local-dependency-plan/aur_rpc_stub.cpp
        tests/stubs/local-dependency-plan/repository_query_stub.cpp
        source/shell_words.cpp
        source/logging.cpp
        tests/stubs/package-metadata/alpm_stub.cpp
        tests/stubs/artifact-identity/archive_metadata_stub.cpp
        tests/stubs/artifact-install-executor/process_stub.cpp
    DEFINITIONS
        MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        MOGUET_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS
        MOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS
        MOGUET_ENABLE_CLEANUP_INVOCATION_SESSION_TEST_HOOKS
        MOGUET_ENABLE_REMOTE_AUR_CLEANUP_COLLECTOR_STUB
        MOGUET_ENABLE_REVIEWED_SOURCE_PRODUCTION_TEST_HOOKS
        MOGUET_ENABLE_TEST_OVERRIDES
    INCLUDE_DIRECTORIES
        "${_moguet_test_source_include_dir}"
        "${_moguet_test_support_include_dir}"
        "${_moguet_test_alpm_stub_include_dir}"
    COMPILE_OPTIONS -ffunction-sections -fdata-sections
    LINK_OPTIONS LINKER:--gc-sections
)

moguet_add_cpp_test(
    process-capture-test
    SOURCES
        tests/process_capture_test.cpp
        source/process.cpp
        source/shell_words.cpp
        source/logging.cpp
    DEFINITIONS MOGUET_ENABLE_PROCESS_TEST_HOOKS
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    bounded-process-test
    SOURCES
        tests/bounded_process_test.cpp
        source/process.cpp
        source/logging.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

moguet_add_cpp_test(
    makepkg-devel-phase-characterization-test
    SOURCES
        tests/makepkg_devel_phase_characterization_test.cpp
        source/process.cpp
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
        source/devel_package_classification.cpp
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
    source/system_aur_update_operation.cpp
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
    source/commands_sync.cpp
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
    source/cross_source_version_lock.cpp
    source/cross_source_version_lock_observation.cpp
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
    source/package_relation_observation.cpp
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
    source/cli_parser.cpp
    source/cli_routing.cpp
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
    source/source_package_identity.cpp
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

moguet_add_cpp_test(
    cross-source-version-lock-test
    ALPM_COMPILE
    REAL_ALPM
    SOURCES
        tests/cross_source_version_lock_test.cpp
        tests/cross_source_version_lock_observation_test.cpp
        source/cross_source_version_lock.cpp
        source/cross_source_version_lock_observation.cpp
        source/package_relation_observation.cpp
        source/package_relation.cpp
        source/dependency_constraint.cpp
        source/package_identifier.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
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
    xdg-generation-store-test
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
    installed-artifact-binding-test
    devel-build-provenance-test
    devel-build-provenance-store-test
    git-remote-revision-observer-test
    source-package-identity-projection-test
    source-package-compatibility-test
    invocation-owned-cleanup-model-test
    source-artifact-install-trusted-transport-test
    reviewed-source-state-test
    reviewed-source-state-store-test
    reviewed-source-lifecycle-test
    reviewed-source-acceptance-test
    reviewed-source-pinned-build-test
    invocation-owned-source-build-context-test
    evaluated-devel-source-build-test
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
    bounded-process-test
    makepkg-devel-phase-characterization-test
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
    cross-source-version-lock-test
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
    tests/stubs/artifact-identity/archive_metadata_stub.cpp
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
    evaluated-devel-source-build-test
)

# These hashes are an independent fail-closed ledger for the 50 link
# firewalls.  Before changing any entry, compare the target's complete CMake
# source/link profile with the legacy Make closure and re-establish parity;
# never derive or update this expected ledger from the configure-time actual
# descriptors automatically.
set(
    MOGUET_EXPECTED_CPP_TEST_FIREWALL_DESCRIPTORS
    evaluated-devel-source-build-test=c210d6468b63c70258e52af3aec136364dbdaeeb14f1d12e2a81376413d2716f
    moguet-aur-update-command-test=98cfa75955ff62cc6e4b12a2aecc74238bdb36ab161d60a4e4179c401dee8565
    moguet-upgrade-all-command-test=b5d656f23b0801b02d6dc7ed26feee3dc48fb95b178569a82d0b54374f9506c3
    moguet-commands-sync-test=862e06d35dd6a15149f6a6dd0113cd1d179eb76ed0ec4b21e30e50135ed3f456
    moguet-commands-inspect-test=e830b65acc922a1c3154052771b71c7a2aa203356a5e832aed9c6075dcdbb650
    moguet-test=0db9b6b3e3411198b07525321e71c76f78a4c313dc81a509dbf1588319bdee23
    moguet-cli-localization-test=b1007c183e4093251afb007c5b5d70d738ea05241b9964ee90c0fd0a3ebdd325
    moguet-app-config-test=40e09215ca7e063a97f60193051a9e7db180011af0e5ce0cef20498a3deb5529
    moguet-aur-rpc-validation-test=ac3a5028623483fe9a4bff8a6349fd94c3f686e098cdccd91b220a998fbf3170
    moguet-source-install-characterization-test=f015d27655cb18f839ee0c328b6b1a6382e142b83b485c96efa0f90a39ffdf75
    moguet-upgrade-baseline-metadata-test=0b567edcb76487172bec19f60fe2ef088c12d4e965e8c26328a428bd24434262
    root-package-candidate-test=f179fe83bc6672f3364577687a005e2e1d503e951b9edc5d72204940a4d7799d
    root-package-search-test=dd2ab42f7e8a4de023c2a03f5915bc8e321204b0b104487ce84b3bc5e90e2033
    root-package-selection-test=bacac2caeeb85e80a4a12ba8fe196f61891522563bd9a7f181bd6fb46030b834
    root-package-route-projection-test=f71b357ddb45c9033fe25dd3792008fbb4c233d5b3658aeea35aa8e4fe886bc0
    local-package-metadata-test=3d4c913a773b11fe9701e623991b2ca85b1ce4b77b0125e8a47687a317e8dd41
    local-source-root-test=23ed7ec650ba374106ecda91fddb031deb3a8b0f8a8fb58ab6a6850cba4b7d22
    local-dependency-plan-projection-test=be226e7468189a05387fc6799c6d7815c4cf686d54a637ba59a638c1a9385278
    local-source-workspace-test=19de51220bf7bc69dde2b9cb6ab5a149bd9058f663e25b9c39f289b33383542e
    local-source-build-test=5aa277287651605e35a66d2b757278b9fe54f0c28f96bcc6f31aeeba63b9f9c3
    source-package-identity-projection-test=aa75791b61ea069c028044700aa05eb02890feff14b4cba6c4b4eae06b489af8
    multiple-artifact-workspace-test=e140d15c4a6d816e8c864e6915ceb4a69474d764e68627a62c00fe4add98d0f6
    makepkg-assignment-precedence-test=7e29da02b187a034aecf455116b9db9c14889dca464f3183f2b67475ddfa2299
    multiple-artifact-identity-test=d4f96960deadee50b667a09b0777d171ad19b932381f032813fbcc7aa4ac02ca
    package-base-artifact-install-plan-test=680c4564b8999ea8ca5715535fc918973f4665551e2b620956743654022dd6f4
    package-base-artifact-install-executor-test=2aafa0e060481a069d619e69ef77b509eb6e70365ea8d8c0ee7b664fe95a8a8e
    separated-package-base-source-build-test=35c6de355e4aa4ca0ec4c8663b24ac70a909901ed1abd3bad3f35fe985d3cc9c
    upgrade-all-plan-test=47498a1948a2d591c3f4ce25531651f9fec94a9d07bca2970bb5a6e9e8d8d8e3
    system-source-upgrade-test=aa9e8bf1e15a6cc1e682ac0c5a351b81ab286e58aa3d270523f83bbabed962b9
    aur-update-execution-preflight-test=167eca6cef76a54712dd3281a015d38e406ae9fde1a264919d69b115a743cfd3
    aur-update-execution-runner-test=6bc5943cf6a1bdd3dacb4956d45aa9e670ae0467dc417176b9c4b9c0b2001818
    aur-update-operation-result-test=794c70c37241de19fa40d3e5369fadebe282fb321fd0e3771b8b3d89e0a369d2
    filtered-aur-update-operation-test=52a7fa05e9c75c4115f2ceaf9541592073ccc6be3bf3483197262c272f1f3fdb
    upgrade-all-operation-test=82307b00b629eafa845fcbf9eabfe4fc3e61dadf7f9b31030b5d67245cb9765d
    cli-diagnostic-model-test=c17dec08e51260b14f85b1ddbb0fc79f88b4d9b01bbb9c67e7fd51abe3d18c12
    runtime-cli-connection-test=4f3a439ad31d7c5fdc49353b15bd73660e0bfa54019c4846ecf5cbde980906e2
    dependency-plan-model-test=5c869cd0c5a646722d0039e5e5ae35792db42cd25e7784c210a1cd571ecc59f8
    build-plan-artifact-target-projection-test=f1fc335f655e906a60b010632b68b23c46cbfad0cac973114c4a651a4f0bc7eb
    unified-plan-observation-test=5a75d1a7932f51a1548fafd83486daeea4068345af52f7403969e87de9607a8b
    unified-plan-projection-test=8bd65e7a30790e47b39617e69dd47aaa815979c04e90d80d351870287d2a4add
    unified-plan-renderer-test=28e6424ac56005369ec6a83f2e5133ef34fb858719b350f2b04175387dd5b866
    artifact-selection-model-test=15c841b43a64d62ee00315afd1a3520e32b80f0f71ec5856ab0b2e7661a4aab2
    artifact-identity-selection-test=be6f5f46e397efb1a343e91ef8a425b5ff6f2bdde9f43a90216e40b79ded369b
    provider-installed-state-test=b87878f9f86b91140a23b34bd6d06cb6a3a6704622560026b38444cb3344da8d
    dependency-constraint-test=b7aa6e1b830cc0bf81dfb9dcd72f4d269d1e7e9c628b89420edc09fde66c6fdc
    package-relation-test=9402017614eb63511b5df3ebe5346c86b431e002c2c068625b491478c3987d38
    package-relation-observation-test=47be5d77e24ad14aedd010f6e050e59dd49814f5bfa6fcef0570f35013e54305
    package-relation-assessment-test=d3308fa7711ecf674281ab451b038941c13fe769910d28249696aa475cebaa28
    package-constraint-metadata-test=b75c66686e588bba737db62d7c864c03748d2334dde06b2f2d09768167b16a76
    aur-constraint-metadata-test=06ad41a0c92131fd472e147a1e2862ff920089501cf70bab209a342f8ff9c1ab
)
