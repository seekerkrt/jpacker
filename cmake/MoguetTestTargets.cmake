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
        source/source_package_identity.cpp
        source/vcs_source_identity.cpp
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
        source/shell_words.cpp
        source/logging.cpp
    INCLUDE_DIRECTORIES "${_moguet_test_source_include_dir}"
)

set(
    _moguet_aur_update_execution_preflight_test_sources
    tests/aur_update_execution_preflight_test.cpp
    source/aur_update_execution_preflight.cpp
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
        source/aur_update_execution_preflight.cpp
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
    moguet-aur-update-command-test=2e308d8c9da6bf1cb2856e1486b5c38664c741f1d203523c4cf18e6d4f5963f2
    moguet-upgrade-all-command-test=a7d78669f7b845653938e2c1fbfabfdbc608974c13b088acdc5df2f6122545f0
    moguet-commands-sync-test=bc135fbf35c6722450363d4bb0475cefcd54cb34e04dad73541179e2fc2b6f13
    moguet-commands-inspect-test=649a7851bf31d8db49cea74bab8e84a57e84fafbda71d7a047fe0537424b3d14
    moguet-test=0d6db977b3171f1633b3356a00c1049d1d16afed703a846364b7e82359e81f63
    moguet-cli-localization-test=98261001bada77ab64970dd70abfaace922cd05b72a3e8919458be7d0c26b057
    moguet-app-config-test=9b15ed8401bac10ba088289e8d501f34aa3c16a1932cdc3f414bd1c822d0483f
    moguet-aur-rpc-validation-test=22f0ae230742eea695dfd4478c2f3c96e7514fd46affcdad5cde5f142f2a7f77
    moguet-source-install-characterization-test=bfbbc289cddae5a464a674d1658b446f98e7fedcef744928968444da97c9ed4d
    moguet-upgrade-baseline-metadata-test=d0a65d07df6efe9239b768e0ed04a57aab51873854e556ec95a69b60e6640a05
    root-package-candidate-test=f63c30a07a9ccdec09edab7147adb3dd1dd6304359498ab039f38c067eed65c4
    root-package-search-test=c2691c5fe143f3bd1002588ed07dc883beab8662da55b6c413b27ca20ad28af8
    root-package-selection-test=c7a49a0873421a314fdd6865f8198f93847b14138c96f737e75d112fc1472671
    root-package-route-projection-test=3b857ab8d225f6385e474fe2e861eff18804fc040b41eaf38c011a20bd30c5d4
    local-package-metadata-test=5243159ca7ac1e150dc288d93a9e00160d4f78a4347d5c015ae5dff771b3b8d3
    local-source-root-test=29e90bd896059457af0754e327e5c4227f941c73a138aaf68a461a693f46ca4e
    local-dependency-plan-projection-test=8cc24340b18fa752b59a4482495396506cfe77be8f60fa003d77453bf060a9be
    local-source-workspace-test=75c5b64e1c2bead81ef9970066856c343866c5da19126adacff442b45677f8d2
    local-source-build-test=150b26a5530d76b07d45387c5bce7dadcf06f8afe16cce3e360a81b74af3a176
    source-package-identity-projection-test=e22d949cba8e2dd8f9e6191b03433672bc13b679caef69583748fd7a2fbc1796
    multiple-artifact-workspace-test=78c794db0aaa5dbe431c7c790d50be7bf92df6fcc31ba5b96a795e483986f498
    makepkg-assignment-precedence-test=3056d03d23c703e9d894adf80e47b813dde6b8f466bf869325764246efd422bf
    multiple-artifact-identity-test=1aeecf6c104259c08cbd708da46ccad97dbd094080cbcdf53d3ea5b92822866c
    package-base-artifact-install-plan-test=c3de90a15533ad7ac075ae343cb5f9ed7d1e1db5666071db08c8815579e16b1b
    package-base-artifact-install-executor-test=f31e05692f66a9ebacbabc0f76696a9eb736a46c934117b8408450d641873a60
    separated-package-base-source-build-test=155965c059c35c2774fe9f631c4859eab8323bb12899a479f4e6449111f83170
    upgrade-all-plan-test=aeca65f85f8d9766c660de3c1ed8861141582f5dbaafb9c61ffaf24b28e74618
    system-source-upgrade-test=b4f2879cab6b494c07ac97e567ffa019df0b3e1c2f3af3abda2780243d192dee
    aur-update-execution-preflight-test=6ecb6a78abd6d3a9c14e4df18b853f32e291e5fa16d2214a6cf0936a18ce04b8
    aur-update-execution-runner-test=6bc5943cf6a1bdd3dacb4956d45aa9e670ae0467dc417176b9c4b9c0b2001818
    aur-update-operation-result-test=f7db95c91026dccdb6527c396a1d231d08f215c2331b7c275d1e369007d3f850
    filtered-aur-update-operation-test=f5aef3d993ab04820c2fea1987a4ef26a8b0427769df0d7146d0021320427a76
    upgrade-all-operation-test=202897cb66b41e9e03debd28287a0c05766f281d0db4751e3c96165e9caddde5
    cli-diagnostic-model-test=73a3bf953bde92576c122437cac1489894111011bc523bfbf4082fc669f25466
    runtime-cli-connection-test=5adb56ef410a274adf3ec3267cce492b358d28e3ed80bc2f2dae3c8f0ec8cd9f
    dependency-plan-model-test=87415bf548494778d1589802bed02ad72158f0c62ddced2f3348a64c958c822c
    build-plan-artifact-target-projection-test=3f9b6f35f2e27fd16296547494aa07e33904c9adf2506db232a22a83346be8f3
    unified-plan-observation-test=329de828c2a59bd835f4c7bd5785815bac594a4bc88aed20b14d4a7d08c82521
    unified-plan-projection-test=054d6ce3bbf6279bc57e1491c3d9df9fa4e7c139d0fb04c1ec3ce293bb412e9e
    unified-plan-renderer-test=0a291910cbd52c75be0058bc3188c1ed3194d74a4ab103650a3d03a702e8583f
    artifact-selection-model-test=da6eff24cb0efdbc69271b0445b5affa4d1058c48448156c3b81604099a47b39
    artifact-identity-selection-test=7eaed0a2488c6d9b3dd349273d1576aea314de76ce76fc182e871ab0d39ddc37
    provider-installed-state-test=0133ac95ec1d463c286c69b9d5ede74a86fec0b0fd5662697e1b5f7fd5918d4d
    dependency-constraint-test=19e68002462c96d956321105a91c7c5afbe7ea4207d49607e84d0cfa611b6327
    package-relation-test=2704f2270e7f515c071ee2cf1923c9d7160f7b97a24d255da17cbaa5c248523b
    package-relation-observation-test=ac834a8d598274a68344573a3fd3597de186260fcd40f06eded37cf0b74986fe
    package-relation-assessment-test=1de63f73aef9b870c5e3cd7245bbe79a4fd215e5e7cbfdbda9196b83795770e4
    package-constraint-metadata-test=02288a51f21bf9349dd51a7fc6f8af1ea11e6eafd50f8bd315e71297a36c84b6
    aur-constraint-metadata-test=b8587fd7297e451eb27d8a62d236c5d34347644daac50640a12dc84a9946f578
)
