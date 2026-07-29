# --- プロジェクト情報 ---
TARGET    := moguet
LEGACY_PRODUCTION_TARGET := jpacker
PACKAGE_NAME := jpacker
VERSION_FILE := VERSION
VERSION   := $(strip $(shell cat $(VERSION_FILE) 2>/dev/null))
ifeq ($(VERSION),)
VERSION   := unknown
endif
SRC_DIR   := src
BUILD_DIR := build
MANPAGE   := man/jpacker.8
MANPAGE_IN := man/jpacker.8.in
TEST_TARGET := build/tests/jpacker-test
APPLICATION_IDENTITY_TEST_TARGET := $(BUILD_DIR)/tests/application-identity-test
ROOT_EXECUTION_IDENTITY_TEST_TARGET := $(BUILD_DIR)/tests/jpacker-root-execution-identity-test
COMMANDS_INSPECT_TEST_TARGET := build/tests/jpacker-commands-inspect-test
AUR_UPDATE_COMMAND_TEST_TARGET := build/tests/jpacker-aur-update-command-test
UPGRADE_ALL_COMMAND_TEST_TARGET := build/tests/jpacker-upgrade-all-command-test
AUR_RPC_VALIDATION_TEST_TARGET := build/tests/jpacker-aur-rpc-validation-test
AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET := build/tests/aur-rpc-envelope-validation-test
COMMANDS_SYNC_TEST_TARGET := build/tests/jpacker-commands-sync-test
SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET := build/tests/jpacker-source-install-characterization-test
APP_CONFIG_MODULE_TEST_TARGET := build/tests/app-config-test
APP_CONFIG_INTEGRATION_TEST_TARGET := build/tests/jpacker-app-config-test
PACKAGE_IDENTIFIER_TEST_TARGET := build/tests/package-identifier-test
SHELL_WORDS_TEST_TARGET := build/tests/shell-words-test
SOURCE_ENVIRONMENT_TEST_TARGET := build/tests/source-environment-test
ARTIFACT_WORKSPACE_TEST_TARGET := build/tests/artifact-workspace-test
MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET := build/tests/multiple-artifact-workspace-test
ARTIFACT_IDENTITY_TEST_TARGET := build/tests/artifact-identity-test
MULTIPLE_ARTIFACT_IDENTITY_TEST_TARGET := build/tests/multiple-artifact-identity-test
ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET := build/tests/artifact-install-executor-test
PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_TARGET := build/tests/package-base-artifact-install-plan-test
PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET := build/tests/package-base-artifact-install-executor-test
SEPARATED_SOURCE_BUILD_TEST_TARGET := build/tests/separated-source-build-test
SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET := build/tests/separated-package-base-source-build-test
PRODUCTION_SOURCE_BUILD_TEST_TARGET := build/tests/production-source-build-test
PROCESS_CAPTURE_TEST_TARGET := build/tests/process-capture-test
PROCESS_STDIN_FD_TEST_TARGET := build/tests/process-stdin-fd-test
AUR_UPDATE_PLAN_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-plan-test
UPGRADE_ALL_PLAN_TEST_TARGET := $(BUILD_DIR)/tests/upgrade-all-plan-test
SYSTEM_SOURCE_UPGRADE_TEST_TARGET := $(BUILD_DIR)/tests/system-source-upgrade-test
AUR_UPDATE_QUERY_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-query-test
AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-execution-preflight-test
AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-execution-preflight-integration-test
AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-execution-preparation-test
AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-execution-preparation-integration-test
AUR_UPDATE_EXECUTION_RUNNER_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-execution-runner-test
AUR_UPDATE_OPERATION_RESULT_TEST_TARGET := $(BUILD_DIR)/tests/aur-update-operation-result-test
FILTERED_AUR_UPDATE_OPERATION_TEST_TARGET := $(BUILD_DIR)/tests/filtered-aur-update-operation-test
UPGRADE_ALL_OPERATION_TEST_TARGET := $(BUILD_DIR)/tests/upgrade-all-operation-test
DEPENDENCY_PLAN_MODEL_TEST_TARGET := $(BUILD_DIR)/tests/dependency-plan-model-test
BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_TARGET := $(BUILD_DIR)/tests/build-plan-artifact-target-projection-test
REPOSITORY_QUERY_TEST_TARGET := $(BUILD_DIR)/tests/repository-query-test
ARTIFACT_INSTALL_PLAN_TEST_TARGET := $(BUILD_DIR)/tests/artifact-install-plan-test
ARTIFACT_SELECTION_MODEL_TEST_TARGET := $(BUILD_DIR)/tests/artifact-selection-model-test
ARTIFACT_IDENTITY_SELECTION_TEST_TARGET := $(BUILD_DIR)/tests/artifact-identity-selection-test
PACKAGE_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/package-metadata-test
PACKAGE_METADATA_INTEGRATION_TEST_TARGET := $(BUILD_DIR)/tests/package-metadata-integration-test
UPGRADE_BASELINE_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/jpacker-upgrade-baseline-metadata-test

# --- インストール先設定 ---
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
SYSCONFDIR  ?= /etc
COMPDIR     ?= /usr/share/bash-completion/completions
ZSHCOMPDIR  ?= /usr/share/zsh/site-functions
FISHCOMPDIR ?= /usr/share/fish/vendor_completions.d
MANDIR      ?= $(PREFIX)/share/man/man8
LICENSEDIR  ?= $(PREFIX)/share/licenses/$(PACKAGE_NAME)
DOCDIR      ?= $(PREFIX)/share/doc/$(PACKAGE_NAME)

PROJECT_LICENSE_FILES := \
	LICENSE \
	LICENSES/jpacker-MIT-legacy.txt \
	LICENSES/curl.txt \
	LICENSES/nlohmann-json-MIT.txt
COMPLIANCE_DOC_FILES := \
	THIRD_PARTY_NOTICES.md \
	docs/LICENSING.md

# --- コンパイラ設定 ---
CXX       ?= g++
CXXFLAGS  ?= -O2 -pipe
LDFLAGS   ?=
CPPFLAGS  ?=
PKG_CONFIG ?= pkg-config
LIBALPM_CPPFLAGS = $(shell $(PKG_CONFIG) --cflags libalpm)
LIBALPM_LDLIBS   = $(shell $(PKG_CONFIG) --libs libalpm)
MY_CXXFLAGS := -std=c++20 -Wall -Wextra -DMOGUET_VERSION=\"$(VERSION)\"
MY_LDLIBS   := -lcurl
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
HEADERS   := $(wildcard $(SRC_DIR)/*.hpp)
COMMANDS_INSPECT_TEST_SRCS := \
	$(filter-out $(SRC_DIR)/aur_rpc.cpp $(SRC_DIR)/repository_query.cpp,$(SRCS)) \
	tests/commands_inspect_aur_stub.cpp \
	tests/stubs/commands-inspect/repository_query_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
# POLICY(#267): CLI integration binaryはoperation APIだけをscenario stubへ差し替え、
# parser/routing/command presentationをproductionと同じtranslation unitで通す。
AUR_UPDATE_COMMAND_TEST_SRCS := \
	$(filter-out \
		$(SRC_DIR)/aur_update_query.cpp \
		$(SRC_DIR)/aur_update_execution_preflight.cpp \
		$(SRC_DIR)/aur_update_execution_preparation.cpp \
		$(SRC_DIR)/aur_update_execution_runner.cpp \
		$(SRC_DIR)/aur_update_operation_result.cpp \
		$(SRC_DIR)/filtered_aur_update_operation.cpp \
		$(SRC_DIR)/upgrade_all_operation.cpp, \
		$(SRCS)) \
	tests/stubs/aur-update-command/operation_stub.cpp \
	tests/stubs/upgrade-all-command/operation_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
# POLICY(#281): final CLI testはparser/routing/presentationをproductionのままlinkし、
# aggregate operation capabilityだけをscenario stubへ差し替える。
UPGRADE_ALL_COMMAND_TEST_SRCS := \
	$(filter-out $(SRC_DIR)/upgrade_all_operation.cpp,$(SRCS)) \
	tests/stubs/upgrade-all-command/operation_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
UPGRADE_ALL_COMMAND_REQUIRED_TEST_SRCS := \
	$(SRC_DIR)/moguet.cpp \
	$(SRC_DIR)/commands_upgrade_all.cpp \
	$(SRC_DIR)/aur_update_cli_presentation.cpp \
	$(SRC_DIR)/cli_parser.cpp \
	$(SRC_DIR)/cli_routing.cpp \
	$(SRC_DIR)/upgrade_all_operation_result.cpp
UPGRADE_ALL_COMMAND_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/upgrade_all_operation.cpp
AUR_RPC_VALIDATION_TEST_SRCS := \
	$(SRCS) \
	tests/stubs/package-metadata/alpm_stub.cpp
AUR_RPC_ENVELOPE_VALIDATION_TEST_SRCS := \
	tests/aur_rpc_validation_test.cpp \
	$(SRC_DIR)/aur_rpc.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp
COMMANDS_SYNC_TEST_SRCS := $(filter-out $(SRC_DIR)/aur_rpc.cpp,$(SRCS)) tests/stubs/commands-sync/aur_rpc_stub.cpp
SOURCE_INSTALL_CHARACTERIZATION_TEST_SRCS := $(filter-out $(SRC_DIR)/moguet.cpp,$(SRCS))
AUR_UPDATE_PLAN_TEST_SRCS := \
	tests/aur_update_plan_test.cpp \
	$(SRC_DIR)/aur_update_plan.cpp
# POLICY(#281): integrated planner testはowned inputを扱うpure modelだけをlinkし、
# command/query/mutation TUへ依存しない境界を固定する。
UPGRADE_ALL_PLAN_TEST_SRCS := \
	tests/test-upgrade-all-plan.cpp \
	$(SRC_DIR)/upgrade_all_plan.cpp
UPGRADE_ALL_PLAN_FORBIDDEN_TEST_SRCS := \
	$(filter-out $(SRC_DIR)/upgrade_all_plan.cpp,$(SRCS))
# POLICY(#281): phase testはactual orchestrationだけをlinkし、preference IO、
# package metadata、system command、source lifecycleをfake symbolへ切る。
SYSTEM_SOURCE_UPGRADE_TEST_SRCS := \
	tests/system_source_upgrade_test.cpp \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	tests/stubs/system-source-upgrade/phase_stub.cpp
SYSTEM_SOURCE_UPGRADE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp
SYSTEM_SOURCE_UPGRADE_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(SYSTEM_SOURCE_UPGRADE_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
AUR_UPDATE_QUERY_TEST_SRCS := \
	tests/aur_update_query_test.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_plan.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp
AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_SRCS := \
	tests/aur_update_execution_preflight_test.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/aur-update-execution-preflight/preflight_stub.cpp
AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_SRCS := \
	tests/aur_update_execution_preflight_integration_test.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/repository_query.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/aur-update-execution-preflight-integration/integration_stub.cpp
# POLICY(#267): このallowlistはpreparationからexecution/mutation TUを切るlink firewall。
# $(SRCS)へ広げず、必要なpure symbolだけを専用stubまたはpreparation TUで与える。
AUR_UPDATE_EXECUTION_PREPARATION_TEST_SRCS := \
	tests/aur_update_execution_preparation_test.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_SRCS := \
	tests/aur_update_execution_preparation_integration_test.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_preference.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/aur-update-execution-preparation-integration/preparation_stub.cpp
# POLICY(#267): runner testはreal preparationでcorrelated invocationを作る一方、
# execution primitiveだけをfake symbolへ差し替え、mutation TUをlinkしない。
AUR_UPDATE_EXECUTION_RUNNER_TEST_SRCS := \
	tests/aur_update_execution_runner_test.cpp \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/aur-update-execution-preparation/preparation_stub.cpp \
	tests/stubs/aur-update-execution-runner/execution_stub.cpp
AUR_UPDATE_EXECUTION_RUNNER_REQUIRED_TEST_SRCS := \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	tests/stubs/aur-update-execution-runner/execution_stub.cpp
AUR_UPDATE_EXECUTION_RUNNER_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/checkout_fetch.cpp \
	$(SRC_DIR)/persistent_checkout.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/separated_source_build.cpp \
	$(SRC_DIR)/separated_package_base_source_build.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/package_base_artifact_install_executor.cpp \
	$(SRC_DIR)/source_build.cpp \
	$(SRC_DIR)/source_install.cpp
# POLICY(#267): reducer testはactual moved-from preparationだけをsafe
# preparation seamで作り、execution/mutation TUをlinkしない。
AUR_UPDATE_OPERATION_RESULT_TEST_SRCS := \
	tests/aur_update_operation_result_test.cpp \
	$(SRC_DIR)/aur_update_operation_result.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
AUR_UPDATE_OPERATION_RESULT_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/checkout_fetch.cpp \
	$(SRC_DIR)/persistent_checkout.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/separated_source_build.cpp \
	$(SRC_DIR)/separated_package_base_source_build.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/package_base_artifact_install_executor.cpp \
	$(SRC_DIR)/source_build.cpp \
	$(SRC_DIR)/source_install.cpp
# POLICY(#281): filtered operation testはactual orchestrationをlinkし、query
# transport、BuildPlan resolver、preparation IO、source lifecycleだけをstub化する。
FILTERED_AUR_UPDATE_OPERATION_TEST_SRCS := \
	tests/filtered_aur_update_operation_test.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/upgrade_all_plan.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_plan.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	$(SRC_DIR)/aur_update_operation_result.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/filtered-aur-update-operation/query_stub.cpp \
	tests/stubs/aur-update-execution-preflight/preflight_stub.cpp \
	tests/stubs/aur-update-execution-preparation/preparation_stub.cpp \
	tests/stubs/aur-update-execution-runner/execution_stub.cpp
FILTERED_AUR_UPDATE_OPERATION_REQUIRED_TEST_SRCS := \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	$(SRC_DIR)/aur_update_operation_result.cpp
FILTERED_AUR_UPDATE_OPERATION_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/aur_rpc.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/checkout_fetch.cpp \
	$(SRC_DIR)/persistent_checkout.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/separated_source_build.cpp \
	$(SRC_DIR)/separated_package_base_source_build.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/package_base_artifact_install_executor.cpp \
	$(SRC_DIR)/source_build.cpp \
	$(SRC_DIR)/source_install.cpp
# POLICY(#281): aggregate testはPR2/PR3を含むproduction orchestrationをlinkし、
# command/transport/libalpm/source mutationの外部境界だけを統合stubへ切る。
UPGRADE_ALL_OPERATION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/upgrade_all_operation.cpp \
	$(SRC_DIR)/upgrade_all_operation_result.cpp \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/upgrade_all_plan.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_plan.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	$(SRC_DIR)/aur_update_operation_result.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/logging.cpp
UPGRADE_ALL_OPERATION_TEST_SRCS := \
	tests/upgrade_all_operation_test.cpp \
	$(UPGRADE_ALL_OPERATION_ALLOWED_PRODUCTION_TEST_SRCS) \
	tests/stubs/upgrade-all-operation/operation_stub.cpp
UPGRADE_ALL_OPERATION_REQUIRED_TEST_SRCS := \
	$(SRC_DIR)/upgrade_all_operation.cpp \
	$(SRC_DIR)/upgrade_all_operation_result.cpp \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	$(SRC_DIR)/aur_update_operation_result.cpp
UPGRADE_ALL_OPERATION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(UPGRADE_ALL_OPERATION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#268): dependency resolver model testはresolverとpure model supportだけを
# productionからlinkし、metadata/process/source-build execution ownerを持ち込まない。
DEPENDENCY_PLAN_MODEL_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp
DEPENDENCY_PLAN_MODEL_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/dependency-plan/aur_rpc_stub.cpp \
	tests/stubs/dependency-plan/repository_query_stub.cpp
DEPENDENCY_PLAN_MODEL_TEST_SRCS := \
	tests/dependency_plan_model_test.cpp \
	$(DEPENDENCY_PLAN_MODEL_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(DEPENDENCY_PLAN_MODEL_REQUIRED_TEST_SUPPORT_SRCS)
DEPENDENCY_PLAN_MODEL_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(DEPENDENCY_PLAN_MODEL_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#268): BuildPlan required artifact target projection testはpure adapterと
# reducer/identifierだけをlinkし、filesystem/process/metadata/executor/stubを持ち込まない。
BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_identifier.cpp
BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS := \
	tests/build_plan_artifact_target_projection_test.cpp \
	$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS)
BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
REPOSITORY_QUERY_TEST_SRCS := \
	tests/repository_query_test.cpp \
	$(SRC_DIR)/repository_query.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/repository-query/process_stub.cpp
ARTIFACT_INSTALL_PLAN_TEST_SRCS := \
	tests/artifact_install_plan_test.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/package_identifier.cpp
# POLICY(#268): selection model testはpure ownerとvalidatorだけをlinkし、filesystem、
# external command、install executionのtranslation unitを持ち込まない。
ARTIFACT_SELECTION_MODEL_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/package_identifier.cpp
ARTIFACT_SELECTION_MODEL_TEST_SRCS := \
	tests/artifact_selection_model_test.cpp \
	$(ARTIFACT_SELECTION_MODEL_ALLOWED_PRODUCTION_TEST_SRCS)
ARTIFACT_SELECTION_MODEL_FORBIDDEN_TEST_SRCS := \
	$(filter-out $(ARTIFACT_SELECTION_MODEL_ALLOWED_PRODUCTION_TEST_SRCS),$(SRCS))
# POLICY(#268): identity correlation testはpure adapter/value model、
# PR1 selector、validatorだけをlinkし、process/filesystem/install lifecycle TUを持ち込まない。
ARTIFACT_IDENTITY_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/artifact_identity_selection.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/package_identifier.cpp
ARTIFACT_IDENTITY_SELECTION_TEST_SRCS := \
	tests/artifact_identity_selection_test.cpp \
	$(ARTIFACT_IDENTITY_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS)
ARTIFACT_IDENTITY_SELECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(ARTIFACT_IDENTITY_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
ARTIFACT_WORKSPACE_TEST_SRCS := \
	tests/artifact_workspace_test.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/logging.cpp
# POLICY(#268): multiple workspace testはfilesystem capability ownerと、その
# 既存support translation unitだけをlinkする。
MULTIPLE_ARTIFACT_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/logging.cpp
MULTIPLE_ARTIFACT_WORKSPACE_TEST_SRCS := \
	tests/multiple_artifact_workspace_test.cpp \
	$(MULTIPLE_ARTIFACT_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS)
MULTIPLE_ARTIFACT_WORKSPACE_FORBIDDEN_TEST_SRCS := \
	$(filter-out $(MULTIPLE_ARTIFACT_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS),$(SRCS))
ARTIFACT_IDENTITY_TEST_SRCS := \
	tests/artifact_identity_test.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/artifact-identity/process_stub.cpp
# POLICY(#268): multiple identity query testはaggregate filesystem capability、
# identity query owner、その既存support TUだけをlinkする。
MULTIPLE_ARTIFACT_IDENTITY_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp
MULTIPLE_ARTIFACT_IDENTITY_TEST_SRCS := \
	tests/multiple_artifact_identity_test.cpp \
	$(MULTIPLE_ARTIFACT_IDENTITY_ALLOWED_PRODUCTION_TEST_SRCS) \
	tests/stubs/artifact-identity/process_stub.cpp
MULTIPLE_ARTIFACT_IDENTITY_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(MULTIPLE_ARTIFACT_IDENTITY_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#268): transaction-wide reason policyはpure TUと既存per-artifact
# reducer/validatorだけをlinkし、filesystem/process/metadata ownerを持ち込まない。
PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/package_base_artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/package_identifier.cpp
PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS := \
	tests/package_base_artifact_install_plan_test.cpp \
	$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_ALLOWED_PRODUCTION_TEST_SRCS)
PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS := \
	tests/artifact_install_executor_test.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/artifact-install-executor/process_stub.cpp
# POLICY(#268): fused preparation/executor testはmultiple capabilityの最小closureを
# exact linkし、production source-build/CLI/orchestration ownerをallowlist外へ置く。
PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/package_base_artifact_install_executor.cpp \
	$(SRC_DIR)/package_base_artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_identity_selection.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp
PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SUPPORT_SRCS := \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/artifact-install-executor/process_stub.cpp
PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS := \
	tests/package_base_artifact_install_executor_test.cpp \
	$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SUPPORT_SRCS)
PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
SEPARATED_SOURCE_BUILD_TEST_SRCS := \
	tests/separated_source_build_test.cpp \
	$(SRC_DIR)/separated_source_build.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/artifact-install-executor/process_stub.cpp
# POLICY(#268): set-based PackageBase lifecycle testは新ownerとPR4
# preparation/executor closureだけをexact linkし、production ownerやCLIを含めない。
SEPARATED_PACKAGE_BASE_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/separated_package_base_source_build.cpp \
	$(SRC_DIR)/package_base_artifact_install_executor.cpp \
	$(SRC_DIR)/package_base_artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_identity_selection.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp
SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SUPPORT_SRCS := \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/artifact-install-executor/process_stub.cpp
SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS := \
	tests/separated_package_base_source_build_test.cpp \
	$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SUPPORT_SRCS)
SEPARATED_PACKAGE_BASE_SOURCE_BUILD_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
PRODUCTION_SOURCE_BUILD_TEST_SRCS := \
	tests/production_source_build_test.cpp \
	$(SRC_DIR)/source_install.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_build.cpp \
	$(SRC_DIR)/separated_source_build.cpp \
	$(SRC_DIR)/separated_package_base_source_build.cpp \
	$(SRC_DIR)/package_base_artifact_install_executor.cpp \
	$(SRC_DIR)/package_base_artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_install_executor.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_identity_selection.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/persistent_checkout.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/source_preference.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/repository_query.cpp \
	$(SRC_DIR)/aur_rpc.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/artifact-install-executor/process_stub.cpp
PROCESS_CAPTURE_TEST_SRCS := \
	tests/process_capture_test.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp
PACKAGE_METADATA_TEST_SRCS := \
	tests/package_metadata_test.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/package-metadata/process_stub.cpp
PACKAGE_METADATA_INTEGRATION_TEST_SRCS := \
	tests/package_metadata_integration_test.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/logging.cpp
UPGRADE_BASELINE_METADATA_TEST_SRCS := \
	$(SRCS) \
	tests/stubs/package-metadata/alpm_stub.cpp
OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)
LIBALPM_BUILD_TARGETS := \
	$(TARGET) \
	$(ROOT_EXECUTION_IDENTITY_TEST_TARGET) \
	$(TEST_TARGET) \
	$(COMMANDS_INSPECT_TEST_TARGET) \
	$(AUR_UPDATE_COMMAND_TEST_TARGET) \
	$(UPGRADE_ALL_COMMAND_TEST_TARGET) \
	$(AUR_RPC_VALIDATION_TEST_TARGET) \
	$(COMMANDS_SYNC_TEST_TARGET) \
	$(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET) \
	$(APP_CONFIG_INTEGRATION_TEST_TARGET) \
	$(PACKAGE_METADATA_TEST_TARGET) \
	$(PACKAGE_METADATA_INTEGRATION_TEST_TARGET) \
	$(ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET) \
	$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET) \
	$(SEPARATED_SOURCE_BUILD_TEST_TARGET) \
	$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET) \
	$(PRODUCTION_SOURCE_BUILD_TEST_TARGET) \
	$(REPOSITORY_QUERY_TEST_TARGET) \
	$(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET) \
	$(UPGRADE_BASELINE_METADATA_TEST_TARGET)

.PHONY: all check-libalpm clean check-upgrade-all-plan-link-firewall check-system-source-upgrade-link-firewall check-aur-update-execution-runner-link-firewall check-aur-update-operation-result-link-firewall check-filtered-aur-update-operation-link-firewall check-upgrade-all-operation-link-firewall check-upgrade-all-command-link-firewall check-dependency-plan-model-link-firewall check-build-plan-artifact-target-projection-link-firewall check-artifact-selection-model-link-firewall check-artifact-identity-selection-link-firewall check-multiple-artifact-workspace-link-firewall check-multiple-artifact-identity-link-firewall check-package-base-artifact-install-plan-link-firewall check-package-base-artifact-install-executor-link-firewall check-separated-package-base-source-build-link-firewall test test-application-identity test-runtime-identity test-app-config test-package-identifier test-package-metadata test-package-metadata-integration test-repository-query test-shell-words test-source-environment test-artifact-workspace test-multiple-artifact-workspace test-artifact-identity test-multiple-artifact-identity test-artifact-install-executor test-package-base-artifact-install-plan test-package-base-artifact-install-executor test-separated-source-build test-separated-package-base-source-build test-production-source-build test-process-capture test-aur-update-plan test-upgrade-all-plan test-system-source-upgrade test-aur-update-query test-aur-update-command test-upgrade-all-command test-aur-update-execution-preflight test-aur-update-execution-preflight-integration test-aur-update-execution-preparation test-aur-update-execution-runner test-aur-update-operation-result test-filtered-aur-update-operation test-upgrade-all-operation test-dependency-plan-model test-build-plan-artifact-target-projection test-artifact-install-plan test-artifact-selection-model test-artifact-identity-selection test-command-stub-contract test-markdown-links test-aur-rpc-validation test-build-cache-symlink test-cli-parser test-commands-inspect test-commands-source-maintenance test-commands-sync test-conflicts-replaces test-install-layout test-needed-contract test-pacman-routing test-pkgbuild-export test-source-build test-source-selection release-check install uninstall

all: $(TARGET) $(MANPAGE)

check-libalpm:
	@test -n "$(strip $(PKG_CONFIG))" && command -v "$(firstword $(PKG_CONFIG))" >/dev/null 2>&1 || { \
		echo "error: pkg-config provider is required (Arch package: pkgconf)" >&2; \
		exit 1; \
	}
	@$(PKG_CONFIG) --exists libalpm >/dev/null 2>&1 || { \
		echo "error: libalpm development metadata was not found via pkg-config" >&2; \
		exit 1; \
	}
	@$(PKG_CONFIG) --cflags libalpm >/dev/null 2>&1 && \
		$(PKG_CONFIG) --libs libalpm >/dev/null 2>&1 || { \
		echo "error: failed to read libalpm build flags via pkg-config" >&2; \
		exit 1; \
	}

$(OBJS) $(LIBALPM_BUILD_TARGETS): | check-libalpm

$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(VERSION_FILE)
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $< (v$(VERSION))"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -MMD -MP -c $< -o $@

$(MANPAGE): $(MANPAGE_IN) $(VERSION_FILE)
	@echo ":: Generating $@ (v$(VERSION))"
	sed 's/@VERSION@/$(VERSION)/g' $(MANPAGE_IN) > $@

clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET) $(LEGACY_PRODUCTION_TARGET)

$(APPLICATION_IDENTITY_TEST_TARGET): tests/application_identity_test.cpp $(SRC_DIR)/application_identity.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling application identity test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		tests/application_identity_test.cpp \
		-o $@

$(ROOT_EXECUTION_IDENTITY_TEST_TARGET): $(OBJS) tests/stubs/runtime-identity/geteuid_stub.cpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Linking root execution identity test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) $(LDFLAGS) \
		$(OBJS) tests/stubs/runtime-identity/geteuid_stub.cpp \
		-Wl,--wrap=geteuid -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(TEST_TARGET): $(SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling isolated integration test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES $(SRCS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(COMMANDS_INSPECT_TEST_TARGET): $(COMMANDS_INSPECT_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling command inspection characterization test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -I$(SRC_DIR) -Itests/stubs/package-metadata $(COMMANDS_INSPECT_TEST_SRCS) -o $@ $(MY_LDLIBS)

$(AUR_UPDATE_COMMAND_TEST_TARGET): $(AUR_UPDATE_COMMAND_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update command integration test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -DJPACKER_ENABLE_TEST_CONFIG_PATH -I$(SRC_DIR) -Itests/stubs/package-metadata $(AUR_UPDATE_COMMAND_TEST_SRCS) -o $@ $(MY_LDLIBS)

$(UPGRADE_ALL_COMMAND_TEST_TARGET): $(UPGRADE_ALL_COMMAND_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling upgrade-all command integration test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_TEST_OVERRIDES \
		-DJPACKER_ENABLE_TEST_CONFIG_PATH \
		-I$(SRC_DIR) \
		-Itests/stubs/package-metadata \
		$(UPGRADE_ALL_COMMAND_TEST_SRCS) \
		-o $@ $(MY_LDLIBS)

$(AUR_RPC_VALIDATION_TEST_TARGET): $(AUR_RPC_VALIDATION_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR RPC validation fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -DJPACKER_ENABLE_AUR_RPC_TEST_HOOKS -I$(SRC_DIR) -Itests/stubs/package-metadata $(AUR_RPC_VALIDATION_TEST_SRCS) -o $@ $(MY_LDLIBS)

$(AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET): $(AUR_RPC_ENVELOPE_VALIDATION_TEST_SRCS) $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR RPC envelope validation test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -DJPACKER_ENABLE_AUR_RPC_TEST_HOOKS -I$(SRC_DIR) $(AUR_RPC_ENVELOPE_VALIDATION_TEST_SRCS) -o $@ $(MY_LDLIBS)

$(COMMANDS_SYNC_TEST_TARGET): $(COMMANDS_SYNC_TEST_SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling sync command characterization test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -DJPACKER_ENABLE_TEST_CONFIG_PATH -I$(SRC_DIR) $(COMMANDS_SYNC_TEST_SRCS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET): tests/source_install_characterization.cpp $(SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling shared source-install characterization test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -I$(SRC_DIR) tests/source_install_characterization.cpp $(SOURCE_INSTALL_CHARACTERIZATION_TEST_SRCS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(APP_CONFIG_MODULE_TEST_TARGET): tests/app_config_test.cpp $(SRC_DIR)/app_config.cpp $(SRC_DIR)/app_config.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling app config module test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) tests/app_config_test.cpp $(SRC_DIR)/app_config.cpp -o $@

$(APP_CONFIG_INTEGRATION_TEST_TARGET): $(SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling app config integration test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -DJPACKER_ENABLE_TEST_CONFIG_PATH -DJPACKER_ENABLE_APP_CONFIG_TEST_HOOKS $(SRCS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(PACKAGE_IDENTIFIER_TEST_TARGET): tests/package_identifier_test.cpp $(SRC_DIR)/package_identifier.cpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package identifier test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) tests/package_identifier_test.cpp $(SRC_DIR)/package_identifier.cpp -o $@

$(SHELL_WORDS_TEST_TARGET): tests/shell_words_test.cpp $(SRC_DIR)/shell_words.cpp $(SRC_DIR)/shell_words.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling shell word serialization test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		tests/shell_words_test.cpp \
		$(SRC_DIR)/shell_words.cpp \
		-o $@

$(SOURCE_ENVIRONMENT_TEST_TARGET): tests/source_environment_test.cpp $(SRC_DIR)/source_environment.cpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/source_preference.cpp $(SRC_DIR)/source_preference.hpp $(SRC_DIR)/package_identifier.cpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.cpp $(SRC_DIR)/shell_words.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling source environment test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_TEST_OVERRIDES \
		-DJPACKER_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS \
		-I$(SRC_DIR) \
		tests/source_environment_test.cpp \
		$(SRC_DIR)/source_environment.cpp \
		$(SRC_DIR)/source_preference.cpp \
		$(SRC_DIR)/package_identifier.cpp \
		$(SRC_DIR)/shell_words.cpp \
		-o $@

$(ARTIFACT_WORKSPACE_TEST_TARGET): $(ARTIFACT_WORKSPACE_TEST_SRCS) $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp tests/stubs/makepkg $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact workspace test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS \
		-DJPACKER_ENABLE_TRUSTED_CACHE_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(ARTIFACT_WORKSPACE_TEST_SRCS) \
		-o $@

$(MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET): $(MULTIPLE_ARTIFACT_WORKSPACE_TEST_SRCS) $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp tests/stubs/makepkg $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling multiple artifact workspace test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS \
		-DJPACKER_ENABLE_TRUSTED_CACHE_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(MULTIPLE_ARTIFACT_WORKSPACE_TEST_SRCS) \
		-o $@

$(ARTIFACT_IDENTITY_TEST_TARGET): $(ARTIFACT_IDENTITY_TEST_SRCS) $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp tests/stubs/artifact-identity/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact identity test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		$(ARTIFACT_IDENTITY_TEST_SRCS) \
		-o $@

$(MULTIPLE_ARTIFACT_IDENTITY_TEST_TARGET): $(MULTIPLE_ARTIFACT_IDENTITY_TEST_SRCS) $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp tests/stubs/artifact-identity/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling multiple artifact identity test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		$(MULTIPLE_ARTIFACT_IDENTITY_TEST_SRCS) \
		-o $@

$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_TARGET): $(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS) $(SRC_DIR)/package_base_artifact_install_plan.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling PackageBase artifact install reason plan test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS) \
		-o $@

$(ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET): $(ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) $(SRC_DIR)/artifact_install_executor.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact install executor fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) \
		-o $@

$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET): $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling PackageBase artifact install executor fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_HOOKS \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) \
		-o $@

$(SEPARATED_SOURCE_BUILD_TEST_TARGET): $(SEPARATED_SOURCE_BUILD_TEST_SRCS) $(SRC_DIR)/separated_source_build.hpp $(SRC_DIR)/artifact_install_executor.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling separated source-build lifecycle fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(SEPARATED_SOURCE_BUILD_TEST_SRCS) \
		-o $@

$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET): $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling separated PackageBase source-build lifecycle fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS) \
		-o $@

$(PRODUCTION_SOURCE_BUILD_TEST_TARGET): $(PRODUCTION_SOURCE_BUILD_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling production source-build fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS \
		-DJPACKER_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS \
		-DJPACKER_ENABLE_TEST_OVERRIDES \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(PRODUCTION_SOURCE_BUILD_TEST_SRCS) \
		-o $@ $(MY_LDLIBS)

$(PROCESS_CAPTURE_TEST_TARGET): $(PROCESS_CAPTURE_TEST_SRCS) $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling process capture test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		$(PROCESS_CAPTURE_TEST_SRCS) \
		-o $@

$(PROCESS_STDIN_FD_TEST_TARGET): tests/process_stdin_fd_test.cpp $(SRC_DIR)/process.cpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.cpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling process stdin fd test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) tests/process_stdin_fd_test.cpp $(SRC_DIR)/process.cpp $(SRC_DIR)/logging.cpp -o $@

$(AUR_UPDATE_PLAN_TEST_TARGET): $(AUR_UPDATE_PLAN_TEST_SRCS) $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/installed_package.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update plan model test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(AUR_UPDATE_PLAN_TEST_SRCS) -o $@

$(UPGRADE_ALL_PLAN_TEST_TARGET): $(UPGRADE_ALL_PLAN_TEST_SRCS) $(SRC_DIR)/upgrade_all_plan.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling upgrade-all plan model test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(UPGRADE_ALL_PLAN_TEST_SRCS) -o $@

$(SYSTEM_SOURCE_UPGRADE_TEST_TARGET): $(SYSTEM_SOURCE_UPGRADE_TEST_SRCS) $(HEADERS) tests/stubs/system-source-upgrade/phase_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling system/source upgrade phase fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(SYSTEM_SOURCE_UPGRADE_TEST_SRCS) \
		-o $@

$(AUR_UPDATE_QUERY_TEST_TARGET): $(AUR_UPDATE_QUERY_TEST_SRCS) $(SRC_DIR)/aur_update_query.hpp $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update query fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(AUR_UPDATE_QUERY_TEST_SRCS) -o $@

$(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_SRCS) $(SRC_DIR)/aur_update_execution_preflight.hpp $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/installed_package.hpp tests/stubs/aur-update-execution-preflight/preflight_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preflight fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_SRCS) -o $@

$(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_SRCS) $(SRC_DIR)/aur_update_execution_preflight.hpp $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/logging.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/aur-update-execution-preflight-integration/integration_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preflight production composition test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		-Itests/stubs/package-metadata \
		-Itests/stubs/aur-update-execution-preflight-integration \
		$(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_SRCS) \
		-o $@

$(AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREPARATION_TEST_SRCS) $(HEADERS) tests/stubs/aur-update-execution-preparation/preparation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preparation fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(AUR_UPDATE_EXECUTION_PREPARATION_TEST_SRCS) \
		-o $@

$(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_SRCS) $(HEADERS) tests/stubs/aur-update-execution-preparation-integration/preparation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preparation production-reader composition test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_TEST_OVERRIDES \
		-DJPACKER_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS \
		-DJPACKER_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_SRCS) \
		-o $@

$(AUR_UPDATE_EXECUTION_RUNNER_TEST_TARGET): $(AUR_UPDATE_EXECUTION_RUNNER_TEST_SRCS) $(HEADERS) tests/stubs/aur-update-execution-preparation/preparation_stub.hpp tests/stubs/aur-update-execution-runner/execution_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution runner fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS \
		-DJPACKER_ENABLE_AUR_UPDATE_EXECUTION_RUNNER_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(AUR_UPDATE_EXECUTION_RUNNER_TEST_SRCS) \
		-o $@

$(AUR_UPDATE_OPERATION_RESULT_TEST_TARGET): $(AUR_UPDATE_OPERATION_RESULT_TEST_SRCS) $(HEADERS) tests/stubs/aur-update-execution-preparation/preparation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling pure AUR update operation result test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		$(AUR_UPDATE_OPERATION_RESULT_TEST_SRCS) \
		-o $@

$(FILTERED_AUR_UPDATE_OPERATION_TEST_TARGET): $(FILTERED_AUR_UPDATE_OPERATION_TEST_SRCS) $(HEADERS) tests/stubs/filtered-aur-update-operation/query_stub.hpp tests/stubs/aur-update-execution-preflight/preflight_stub.hpp tests/stubs/aur-update-execution-preparation/preparation_stub.hpp tests/stubs/aur-update-execution-runner/execution_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling filtered AUR update operation production-composition test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS \
		-DJPACKER_ENABLE_AUR_UPDATE_EXECUTION_RUNNER_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(FILTERED_AUR_UPDATE_OPERATION_TEST_SRCS) \
		-o $@

$(UPGRADE_ALL_OPERATION_TEST_TARGET): $(UPGRADE_ALL_OPERATION_TEST_SRCS) $(HEADERS) tests/stubs/upgrade-all-operation/operation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling upgrade-all operation production-composition test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_UPGRADE_ALL_OPERATION_TEST_HOOKS \
		-DJPACKER_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(UPGRADE_ALL_OPERATION_TEST_SRCS) \
		-o $@

$(DEPENDENCY_PLAN_MODEL_TEST_TARGET): $(DEPENDENCY_PLAN_MODEL_TEST_SRCS) $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling dependency plan model test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(DEPENDENCY_PLAN_MODEL_TEST_SRCS) -o $@

$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_TARGET): $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS) $(SRC_DIR)/build_plan_artifact_target_projection.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling BuildPlan artifact target projection test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS) -o $@

$(REPOSITORY_QUERY_TEST_TARGET): $(REPOSITORY_QUERY_TEST_SRCS) $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/repository-query/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling repository query fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_REPOSITORY_QUERY_TEST_HOOKS \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(REPOSITORY_QUERY_TEST_SRCS) \
		-o $@

$(ARTIFACT_INSTALL_PLAN_TEST_TARGET): $(ARTIFACT_INSTALL_PLAN_TEST_SRCS) $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact install plan test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(ARTIFACT_INSTALL_PLAN_TEST_SRCS) -o $@

$(ARTIFACT_SELECTION_MODEL_TEST_TARGET): $(ARTIFACT_SELECTION_MODEL_TEST_SRCS) $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact selection model test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(ARTIFACT_SELECTION_MODEL_TEST_SRCS) -o $@

$(ARTIFACT_IDENTITY_SELECTION_TEST_TARGET): $(ARTIFACT_IDENTITY_SELECTION_TEST_SRCS) $(SRC_DIR)/artifact_identity_selection.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact identity selection test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS \
		-I$(SRC_DIR) \
		$(ARTIFACT_IDENTITY_SELECTION_TEST_SRCS) \
		-o $@

$(PACKAGE_METADATA_TEST_TARGET): $(PACKAGE_METADATA_TEST_SRCS) $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/package-metadata/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package metadata fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(PACKAGE_METADATA_TEST_SRCS) \
		-o $@

$(PACKAGE_METADATA_INTEGRATION_TEST_TARGET): $(PACKAGE_METADATA_INTEGRATION_TEST_SRCS) $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package metadata integration test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		$(PACKAGE_METADATA_INTEGRATION_TEST_SRCS) \
		-o $@ $(LIBALPM_LDLIBS)

$(UPGRADE_BASELINE_METADATA_TEST_TARGET): $(UPGRADE_BASELINE_METADATA_TEST_SRCS) $(HEADERS) tests/stubs/package-metadata/alpm_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling upgrade baseline metadata fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-DJPACKER_ENABLE_TEST_OVERRIDES \
		-DJPACKER_ENABLE_TEST_CONFIG_PATH \
		-DJPACKER_ENABLE_APP_CONFIG_TEST_HOOKS \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(UPGRADE_BASELINE_METADATA_TEST_SRCS) \
		-o $@ $(MY_LDLIBS)

test-application-identity: $(APPLICATION_IDENTITY_TEST_TARGET)
	$(abspath $(APPLICATION_IDENTITY_TEST_TARGET)) "$(VERSION)"

test-runtime-identity: $(TARGET) $(ROOT_EXECUTION_IDENTITY_TEST_TARGET)
	sh tests/test-runtime-identity.sh \
		$(abspath $(TARGET)) \
		$(abspath $(ROOT_EXECUTION_IDENTITY_TEST_TARGET))

test-app-config: $(APP_CONFIG_MODULE_TEST_TARGET) $(APP_CONFIG_INTEGRATION_TEST_TARGET)
	sh tests/test-app-config.sh $(abspath $(APP_CONFIG_MODULE_TEST_TARGET)) $(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET))

test-package-identifier: $(PACKAGE_IDENTIFIER_TEST_TARGET)
	$(abspath $(PACKAGE_IDENTIFIER_TEST_TARGET))

test-package-metadata: $(PACKAGE_METADATA_TEST_TARGET)
	$(abspath $(PACKAGE_METADATA_TEST_TARGET))

test-package-metadata-integration: $(PACKAGE_METADATA_INTEGRATION_TEST_TARGET)
	$(abspath $(PACKAGE_METADATA_INTEGRATION_TEST_TARGET))

test-shell-words: $(SHELL_WORDS_TEST_TARGET)
	$(abspath $(SHELL_WORDS_TEST_TARGET))

test-source-environment: $(SOURCE_ENVIRONMENT_TEST_TARGET)
	JPACKER_TEST_PACKAGE_BUILD_DIR=$(abspath $(BUILD_DIR)/tests/source-environment-fixture) \
		$(abspath $(SOURCE_ENVIRONMENT_TEST_TARGET)) \
		$(abspath $(BUILD_DIR)/tests/source-environment-fixture)

test-artifact-workspace: $(ARTIFACT_WORKSPACE_TEST_TARGET)
	JPACKER_TEST_MAKEPKG_STUB=$(abspath tests/stubs/makepkg) \
		$(abspath $(ARTIFACT_WORKSPACE_TEST_TARGET))

check-multiple-artifact-workspace-link-firewall:
	@echo ":: Checking multiple artifact workspace link firewall"
	@set -e; for source in $(MULTIPLE_ARTIFACT_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(MULTIPLE_ARTIFACT_WORKSPACE_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: multiple artifact workspace test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(MULTIPLE_ARTIFACT_WORKSPACE_FORBIDDEN_TEST_SRCS),$(MULTIPLE_ARTIFACT_WORKSPACE_TEST_SRCS))" || { \
		echo "error: multiple artifact workspace test links a forbidden production source" >&2; \
		exit 1; \
	}

test-multiple-artifact-workspace: check-multiple-artifact-workspace-link-firewall $(MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET)
	JPACKER_TEST_MAKEPKG_STUB=$(abspath tests/stubs/makepkg) \
		$(abspath $(MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET))

test-artifact-identity: $(ARTIFACT_IDENTITY_TEST_TARGET)
	$(abspath $(ARTIFACT_IDENTITY_TEST_TARGET))

check-multiple-artifact-identity-link-firewall:
	@echo ":: Checking multiple artifact identity link firewall"
	@set -e; for source in $(MULTIPLE_ARTIFACT_IDENTITY_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(MULTIPLE_ARTIFACT_IDENTITY_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: multiple artifact identity test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(MULTIPLE_ARTIFACT_IDENTITY_FORBIDDEN_TEST_SRCS),$(MULTIPLE_ARTIFACT_IDENTITY_TEST_SRCS))" || { \
		echo "error: multiple artifact identity test links a forbidden production source" >&2; \
		exit 1; \
	}

test-multiple-artifact-identity: check-multiple-artifact-identity-link-firewall $(MULTIPLE_ARTIFACT_IDENTITY_TEST_TARGET)
	$(abspath $(MULTIPLE_ARTIFACT_IDENTITY_TEST_TARGET))

check-package-base-artifact-install-plan-link-firewall:
	@echo ":: Checking PackageBase artifact install reason plan link firewall"
	@set -e; for source in $(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: PackageBase artifact install plan test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_FORBIDDEN_TEST_SRCS),$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS))" || { \
		echo "error: PackageBase artifact install plan test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS))" || { \
		echo "error: PackageBase artifact install plan test links a test stub" >&2; \
		exit 1; \
	}

test-package-base-artifact-install-plan: check-package-base-artifact-install-plan-link-firewall $(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_TARGET)
	$(abspath $(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_TARGET))

test-artifact-install-executor: $(ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET)
	$(abspath $(ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET))

check-package-base-artifact-install-executor-link-firewall:
	@echo ":: Checking PackageBase artifact install executor link firewall"
	@set -e; for source in $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: PackageBase artifact install executor test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: PackageBase artifact install executor test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_FORBIDDEN_TEST_SRCS),$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS))" || { \
		echo "error: PackageBase artifact install executor test links a forbidden production source" >&2; \
		exit 1; \
	}

test-package-base-artifact-install-executor: check-package-base-artifact-install-executor-link-firewall $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET)
	$(abspath $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET))

test-separated-source-build: $(SEPARATED_SOURCE_BUILD_TEST_TARGET)
	$(abspath $(SEPARATED_SOURCE_BUILD_TEST_TARGET))

check-separated-package-base-source-build-link-firewall:
	@echo ":: Checking separated PackageBase source-build lifecycle link firewall"
	@set -e; for source in $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: separated PackageBase source-build test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: separated PackageBase source-build test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_FORBIDDEN_TEST_SRCS),$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS))" || { \
		echo "error: separated PackageBase source-build test links a forbidden production source" >&2; \
		exit 1; \
	}

test-separated-package-base-source-build: check-separated-package-base-source-build-link-firewall $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET)
	$(abspath $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET))

test-production-source-build: $(PRODUCTION_SOURCE_BUILD_TEST_TARGET)
	JPACKER_TEST_PACKAGE_BUILD_DIR=$(abspath $(BUILD_DIR)/tests/production-source-build-preferences) \
		$(abspath $(PRODUCTION_SOURCE_BUILD_TEST_TARGET))

test-process-capture: $(PROCESS_CAPTURE_TEST_TARGET)
	$(abspath $(PROCESS_CAPTURE_TEST_TARGET))

test-aur-update-plan: $(AUR_UPDATE_PLAN_TEST_TARGET)
	$(abspath $(AUR_UPDATE_PLAN_TEST_TARGET))

check-upgrade-all-plan-link-firewall:
	@echo ":: Checking upgrade-all plan link firewall"
	@test -z "$(filter $(UPGRADE_ALL_PLAN_FORBIDDEN_TEST_SRCS),$(UPGRADE_ALL_PLAN_TEST_SRCS))" || { \
		echo "error: upgrade-all plan test links a production source outside upgrade_all_plan.cpp" >&2; \
		exit 1; \
	}

test-upgrade-all-plan: check-upgrade-all-plan-link-firewall $(UPGRADE_ALL_PLAN_TEST_TARGET)
	$(abspath $(UPGRADE_ALL_PLAN_TEST_TARGET))

check-system-source-upgrade-link-firewall:
	@echo ":: Checking system/source upgrade phase link firewall"
	@test "$(words $(filter $(SRC_DIR)/system_source_upgrade.cpp,$(SYSTEM_SOURCE_UPGRADE_TEST_SRCS)))" -eq 1 || { \
		echo "error: system/source upgrade phase test must link actual orchestration exactly once" >&2; \
		exit 1; \
	}
	@test -z "$(filter $(SYSTEM_SOURCE_UPGRADE_FORBIDDEN_TEST_SRCS),$(SYSTEM_SOURCE_UPGRADE_TEST_SRCS))" || { \
		echo "error: system/source upgrade phase test links a forbidden production source" >&2; \
		exit 1; \
	}

test-system-source-upgrade: check-system-source-upgrade-link-firewall $(SYSTEM_SOURCE_UPGRADE_TEST_TARGET)
	$(abspath $(SYSTEM_SOURCE_UPGRADE_TEST_TARGET))

test-aur-update-query: $(AUR_UPDATE_QUERY_TEST_TARGET)
	$(abspath $(AUR_UPDATE_QUERY_TEST_TARGET))

test-aur-update-command: $(AUR_UPDATE_COMMAND_TEST_TARGET)
	sh tests/test-aur-update-command.sh $(abspath $(AUR_UPDATE_COMMAND_TEST_TARGET))

check-upgrade-all-command-link-firewall:
	@echo ":: Checking upgrade-all command link firewall"
	@set -e; for source in $(UPGRADE_ALL_COMMAND_REQUIRED_TEST_SRCS); do \
		count=$$(printf '%s\n' $(UPGRADE_ALL_COMMAND_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: upgrade-all command test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(UPGRADE_ALL_COMMAND_FORBIDDEN_TEST_SRCS),$(UPGRADE_ALL_COMMAND_TEST_SRCS))" || { \
		echo "error: upgrade-all command test links the production aggregate operation boundary" >&2; \
		exit 1; \
	}

test-upgrade-all-command: check-upgrade-all-command-link-firewall $(UPGRADE_ALL_COMMAND_TEST_TARGET) tests/test-upgrade-all-completion.sh
	sh tests/test-upgrade-all-command.sh $(abspath $(UPGRADE_ALL_COMMAND_TEST_TARGET))
	bash tests/test-upgrade-all-completion.sh $(abspath completions/jpacker_completion.bash)

test-aur-update-execution-preflight: $(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET)
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET))

test-aur-update-execution-preflight-integration: $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) simple
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) repository-failure
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) aur-failure

test-aur-update-execution-preparation: $(AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET) $(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET)
	$(abspath $(AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET))
	JPACKER_TEST_PACKAGE_BUILD_DIR=$(abspath $(BUILD_DIR)/tests/aur-update-execution-preparation-fixture) \
		$(abspath $(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET)) \
		$(abspath $(BUILD_DIR)/tests/aur-update-execution-preparation-fixture)

check-aur-update-execution-runner-link-firewall:
	@echo ":: Checking AUR update execution runner link firewall"
	@set -e; for source in $(AUR_UPDATE_EXECUTION_RUNNER_REQUIRED_TEST_SRCS); do \
		count=$$(printf '%s\n' $(AUR_UPDATE_EXECUTION_RUNNER_TEST_SRCS) | \
			grep -Fxc "$$source"); \
		test "$$count" -eq 1 || { \
			echo "error: AUR update execution runner test must link $$source exactly once (found $$count)" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(AUR_UPDATE_EXECUTION_RUNNER_FORBIDDEN_TEST_SRCS),$(AUR_UPDATE_EXECUTION_RUNNER_TEST_SRCS))" || { \
		echo "error: AUR update execution runner test links a forbidden mutation source" >&2; \
		exit 1; \
	}

test-aur-update-execution-runner: check-aur-update-execution-runner-link-firewall $(AUR_UPDATE_EXECUTION_RUNNER_TEST_TARGET)
	$(abspath $(AUR_UPDATE_EXECUTION_RUNNER_TEST_TARGET))

check-aur-update-operation-result-link-firewall:
	@echo ":: Checking AUR update operation result link firewall"
	@test -z "$(filter $(AUR_UPDATE_OPERATION_RESULT_FORBIDDEN_TEST_SRCS),$(AUR_UPDATE_OPERATION_RESULT_TEST_SRCS))" || { \
		echo "error: AUR update operation result test links a forbidden mutation source" >&2; \
		exit 1; \
	}

test-aur-update-operation-result: check-aur-update-operation-result-link-firewall $(AUR_UPDATE_OPERATION_RESULT_TEST_TARGET)
	$(abspath $(AUR_UPDATE_OPERATION_RESULT_TEST_TARGET))

check-filtered-aur-update-operation-link-firewall:
	@echo ":: Checking filtered AUR update operation link firewall"
	@set -e; for source in $(FILTERED_AUR_UPDATE_OPERATION_REQUIRED_TEST_SRCS); do \
		count=$$(printf '%s\n' $(FILTERED_AUR_UPDATE_OPERATION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: filtered AUR operation test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(FILTERED_AUR_UPDATE_OPERATION_FORBIDDEN_TEST_SRCS),$(FILTERED_AUR_UPDATE_OPERATION_TEST_SRCS))" || { \
		echo "error: filtered AUR operation test links a forbidden transport/mutation source" >&2; \
		exit 1; \
	}

test-filtered-aur-update-operation: check-filtered-aur-update-operation-link-firewall $(FILTERED_AUR_UPDATE_OPERATION_TEST_TARGET)
	$(abspath $(FILTERED_AUR_UPDATE_OPERATION_TEST_TARGET))

check-upgrade-all-operation-link-firewall:
	@echo ":: Checking upgrade-all operation link firewall"
	@set -e; for source in $(UPGRADE_ALL_OPERATION_REQUIRED_TEST_SRCS); do \
		count=$$(printf '%s\n' $(UPGRADE_ALL_OPERATION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: upgrade-all operation test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(UPGRADE_ALL_OPERATION_FORBIDDEN_TEST_SRCS),$(UPGRADE_ALL_OPERATION_TEST_SRCS))" || { \
		echo "error: upgrade-all operation test links a forbidden command/transport/mutation source" >&2; \
		exit 1; \
	}

test-upgrade-all-operation: check-upgrade-all-operation-link-firewall $(UPGRADE_ALL_OPERATION_TEST_TARGET)
	$(abspath $(UPGRADE_ALL_OPERATION_TEST_TARGET))

check-dependency-plan-model-link-firewall:
	@echo ":: Checking dependency plan model link firewall"
	@set -e; for source in $(DEPENDENCY_PLAN_MODEL_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(DEPENDENCY_PLAN_MODEL_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: dependency plan model test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(DEPENDENCY_PLAN_MODEL_REQUIRED_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(DEPENDENCY_PLAN_MODEL_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: dependency plan model test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(DEPENDENCY_PLAN_MODEL_FORBIDDEN_TEST_SRCS),$(DEPENDENCY_PLAN_MODEL_TEST_SRCS))" || { \
		echo "error: dependency plan model test links a forbidden production source" >&2; \
		exit 1; \
	}

test-dependency-plan-model: check-dependency-plan-model-link-firewall $(DEPENDENCY_PLAN_MODEL_TEST_TARGET)
	$(abspath $(DEPENDENCY_PLAN_MODEL_TEST_TARGET))

check-build-plan-artifact-target-projection-link-firewall:
	@echo ":: Checking BuildPlan artifact target projection link firewall"
	@set -e; for source in $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: BuildPlan artifact target projection test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_FORBIDDEN_TEST_SRCS),$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS))" || { \
		echo "error: BuildPlan artifact target projection test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS))" || { \
		echo "error: BuildPlan artifact target projection test links a test stub" >&2; \
		exit 1; \
	}

test-build-plan-artifact-target-projection: check-build-plan-artifact-target-projection-link-firewall $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_TARGET)
	$(abspath $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_TARGET))

test-repository-query: $(REPOSITORY_QUERY_TEST_TARGET)
	@set -e; for test_case in \
		success \
		repository-named-aur \
		legacy-malformed-candidates \
		configuration-command-failure \
		configuration-parse-failure \
		unsafe-repository-name \
		missing-sync-directory \
		empty-repository-configuration \
		missing-configured-database \
		non-regular-configured-database \
		database-read-failure \
		empty-database \
		malformed-database \
		invalid-provided-dependency \
		partial-snapshot; do \
		$(abspath $(REPOSITORY_QUERY_TEST_TARGET)) $$test_case; \
	done

test-artifact-install-plan: $(ARTIFACT_INSTALL_PLAN_TEST_TARGET)
	$(abspath $(ARTIFACT_INSTALL_PLAN_TEST_TARGET))

check-artifact-selection-model-link-firewall:
	@echo ":: Checking artifact selection model link firewall"
	@set -e; for source in $(ARTIFACT_SELECTION_MODEL_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(ARTIFACT_SELECTION_MODEL_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: artifact selection model test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(ARTIFACT_SELECTION_MODEL_FORBIDDEN_TEST_SRCS),$(ARTIFACT_SELECTION_MODEL_TEST_SRCS))" || { \
		echo "error: artifact selection model test links a forbidden production source" >&2; \
		exit 1; \
	}

test-artifact-selection-model: check-artifact-selection-model-link-firewall $(ARTIFACT_SELECTION_MODEL_TEST_TARGET)
	$(abspath $(ARTIFACT_SELECTION_MODEL_TEST_TARGET))

check-artifact-identity-selection-link-firewall:
	@echo ":: Checking artifact identity selection link firewall"
	@set -e; for source in $(ARTIFACT_IDENTITY_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(ARTIFACT_IDENTITY_SELECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: artifact identity selection test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(ARTIFACT_IDENTITY_SELECTION_FORBIDDEN_TEST_SRCS),$(ARTIFACT_IDENTITY_SELECTION_TEST_SRCS))" || { \
		echo "error: artifact identity selection test links a forbidden production source" >&2; \
		exit 1; \
	}

test-artifact-identity-selection: check-artifact-identity-selection-link-firewall $(ARTIFACT_IDENTITY_SELECTION_TEST_TARGET)
	$(abspath $(ARTIFACT_IDENTITY_SELECTION_TEST_TARGET))

test-command-stub-contract:
	sh tests/test-command-stub-contract.sh

test-markdown-links:
	sh tests/test-markdown-links.sh \
		$(abspath scripts/check-markdown-links.sh)

test-conflicts-replaces: $(TEST_TARGET)
	sh tests/test-conflicts-replaces.sh $(abspath $(TEST_TARGET))

test-aur-rpc-validation: $(AUR_RPC_VALIDATION_TEST_TARGET) $(AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET)
	sh tests/test-aur-rpc-validation.sh \
		$(abspath $(AUR_RPC_VALIDATION_TEST_TARGET)) \
		$(abspath $(AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET))

test-cli-parser: $(TEST_TARGET) $(MANPAGE)
	sh tests/test-cli-parser.sh $(abspath $(TEST_TARGET))
	sh tests/test-help-man-completion.sh $(abspath $(TEST_TARGET))

test-commands-inspect: $(COMMANDS_INSPECT_TEST_TARGET)
	sh tests/test-commands-inspect.sh $(abspath $(COMMANDS_INSPECT_TEST_TARGET))

test-commands-source-maintenance: $(APP_CONFIG_INTEGRATION_TEST_TARGET) $(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET) $(PROCESS_STDIN_FD_TEST_TARGET) $(UPGRADE_BASELINE_METADATA_TEST_TARGET)
	$(abspath $(PROCESS_STDIN_FD_TEST_TARGET))
	sh tests/test-commands-source-maintenance.sh \
		$(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET)) \
		$(abspath $(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET)) \
		$(abspath $(UPGRADE_BASELINE_METADATA_TEST_TARGET))

test-commands-sync: $(COMMANDS_SYNC_TEST_TARGET)
	sh tests/test-commands-sync.sh $(abspath $(COMMANDS_SYNC_TEST_TARGET))

test-pacman-routing: $(TEST_TARGET)
	sh tests/test-pacman-routing.sh $(abspath $(TEST_TARGET))

test-build-cache-symlink: $(TEST_TARGET)
	sh tests/test-build-cache-symlink.sh $(abspath $(TEST_TARGET))

test-source-build: $(TEST_TARGET) $(APP_CONFIG_INTEGRATION_TEST_TARGET) $(UPGRADE_BASELINE_METADATA_TEST_TARGET)
	sh tests/test-source-build.sh \
		$(abspath $(TEST_TARGET)) \
		$(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET)) \
		$(abspath $(UPGRADE_BASELINE_METADATA_TEST_TARGET))

test-source-selection: $(TEST_TARGET)
	sh tests/test-source-selection.sh $(abspath $(TEST_TARGET))

test-install-layout: $(TARGET) $(MANPAGE) $(PROJECT_LICENSE_FILES) $(COMPLIANCE_DOC_FILES)
	sh tests/test-install-layout.sh

test-needed-contract: $(TEST_TARGET)
	sh tests/test-needed-contract.sh $(abspath $(TEST_TARGET))

test-pkgbuild-export: $(TEST_TARGET)
	sh tests/test-pkgbuild-export.sh $(abspath $(TEST_TARGET))

test: \
	test-application-identity \
	test-runtime-identity \
	test-app-config \
	test-package-identifier \
	test-package-metadata \
	test-package-metadata-integration \
	test-repository-query \
	test-shell-words \
	test-source-environment \
	test-artifact-workspace \
	test-multiple-artifact-workspace \
	test-artifact-identity \
	test-multiple-artifact-identity \
	test-artifact-install-executor \
	test-package-base-artifact-install-executor \
	test-separated-source-build \
	test-separated-package-base-source-build \
	test-production-source-build \
	test-process-capture \
	test-aur-update-plan \
	test-upgrade-all-plan \
	test-system-source-upgrade \
	test-aur-update-query \
	test-aur-update-command \
	test-upgrade-all-command \
	test-aur-update-execution-preflight \
	test-aur-update-execution-preflight-integration \
	test-aur-update-execution-preparation \
	test-aur-update-execution-runner \
	test-aur-update-operation-result \
	test-filtered-aur-update-operation \
	test-upgrade-all-operation \
	test-dependency-plan-model \
	test-build-plan-artifact-target-projection \
	test-artifact-install-plan \
	test-package-base-artifact-install-plan \
	test-artifact-selection-model \
	test-artifact-identity-selection \
	test-command-stub-contract \
	test-markdown-links \
	test-aur-rpc-validation \
	test-build-cache-symlink \
	test-cli-parser \
	test-commands-inspect \
	test-commands-source-maintenance \
	test-commands-sync \
	test-conflicts-replaces \
	test-install-layout \
	test-needed-contract \
	test-pacman-routing \
	test-pkgbuild-export \
	test-source-build \
	test-source-selection

release-check: test-application-identity test-runtime-identity
	@echo ":: Checking release version consistency"
	sh scripts/check-release-version.sh
	@echo ":: Checking license compliance"
	sh scripts/check-license-compliance.sh
	@echo ":: Checking packaging metadata and payload"
	sh scripts/check-packaging-metadata.sh
	@echo ":: Checking tracked Markdown links"
	sh scripts/check-markdown-links.sh

install: $(TARGET) $(MANPAGE) $(PROJECT_LICENSE_FILES) $(COMPLIANCE_DOC_FILES)
	@echo ":: Installing binary..."
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Installing configs..."
	install -Dm644 config/jpacker.conf $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf
	install -d $(DESTDIR)$(SYSCONFDIR)/jpacker/package.build

	@echo ":: Installing bash completion..."
	install -Dm644 completions/jpacker_completion.bash $(DESTDIR)$(COMPDIR)/jpacker

	@echo ":: Installing zsh completion..."
	install -Dm644 completions/_jpacker $(DESTDIR)$(ZSHCOMPDIR)/_jpacker

	@echo ":: Installing fish completion..."
	install -Dm644 completions/jpacker.fish $(DESTDIR)$(FISHCOMPDIR)/jpacker.fish

	@echo ":: Installing man page..."
	install -Dm644 $(MANPAGE) $(DESTDIR)$(MANDIR)/jpacker.8

	@echo ":: Installing license files..."
	install -Dm644 LICENSE $(DESTDIR)$(LICENSEDIR)/LICENSE
	install -Dm644 LICENSES/jpacker-MIT-legacy.txt $(DESTDIR)$(LICENSEDIR)/jpacker-MIT-legacy.txt
	install -Dm644 LICENSES/curl.txt $(DESTDIR)$(LICENSEDIR)/curl.txt
	install -Dm644 LICENSES/nlohmann-json-MIT.txt $(DESTDIR)$(LICENSEDIR)/nlohmann-json-MIT.txt

	@echo ":: Installing compliance documentation..."
	install -Dm644 THIRD_PARTY_NOTICES.md $(DESTDIR)$(DOCDIR)/THIRD_PARTY_NOTICES.md
	install -Dm644 docs/LICENSING.md $(DESTDIR)$(DOCDIR)/LICENSING.md

uninstall:
	@echo ":: Removing binary..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Preserving configs and removing empty config directories..."
	@rmdir $(DESTDIR)$(SYSCONFDIR)/jpacker/package.build 2>/dev/null || true
	@rmdir $(DESTDIR)$(SYSCONFDIR)/jpacker 2>/dev/null || true

	@echo ":: Removing bash completion..."
	rm -f $(DESTDIR)$(COMPDIR)/jpacker

	@echo ":: Removing zsh completion..."
	rm -f $(DESTDIR)$(ZSHCOMPDIR)/_jpacker

	@echo ":: Removing fish completion..."
	rm -f $(DESTDIR)$(FISHCOMPDIR)/jpacker.fish

	@echo ":: Removing man page..."
	rm -f $(DESTDIR)$(MANDIR)/jpacker.8

	@echo ":: Removing license files..."
	rm -f $(DESTDIR)$(LICENSEDIR)/LICENSE
	rm -f $(DESTDIR)$(LICENSEDIR)/jpacker-MIT-legacy.txt
	rm -f $(DESTDIR)$(LICENSEDIR)/curl.txt
	rm -f $(DESTDIR)$(LICENSEDIR)/nlohmann-json-MIT.txt
	@rmdir $(DESTDIR)$(LICENSEDIR) 2>/dev/null || true

	@echo ":: Removing compliance documentation..."
	rm -f $(DESTDIR)$(DOCDIR)/THIRD_PARTY_NOTICES.md
	rm -f $(DESTDIR)$(DOCDIR)/LICENSING.md
	@rmdir $(DESTDIR)$(DOCDIR) 2>/dev/null || true

-include $(DEPS)
