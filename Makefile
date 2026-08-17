# --- プロジェクト情報 ---
TARGET    := moguet
PACKAGE_NAME := moguet
DOCKER ?= docker
ARCH_VALIDATION_IMAGE ?= moguet-arch-validation:local
ARCH_LIVE_VALIDATION_IMAGE ?= moguet-arch-live-validation:local
ARCH_LIVE_AUR_VALIDATION_IMAGE ?= moguet-arch-live-aur-validation:local
ARCH_LIVE_LOCAL_VALIDATION_IMAGE ?= moguet-arch-live-local-validation:local
VERSION_FILE := VERSION
VERSION   := $(strip $(shell cat $(VERSION_FILE) 2>/dev/null))
ifeq ($(VERSION),)
VERSION   := unknown
endif
SRC_DIR   := src
BUILD_DIR := build
MANPAGE_EN := man/moguet.1
MANPAGE_EN_IN := man/moguet.1.in
MANPAGE_JA := man/ja/moguet.1
MANPAGE_JA_IN := man/ja/moguet.1.in
MANPAGES := $(MANPAGE_EN) $(MANPAGE_JA)
BASH_COMPLETION := completions/moguet.bash
ZSH_COMPLETION := completions/_moguet
FISH_COMPLETION := completions/moguet.fish
COMPLETION_FILES := $(BASH_COMPLETION) $(ZSH_COMPLETION) $(FISH_COMPLETION)
TEST_TARGET := build/tests/moguet-test
APPLICATION_IDENTITY_TEST_TARGET := $(BUILD_DIR)/tests/application-identity-test
INTERACTIVE_CONFIRMATION_TEST_TARGET := $(BUILD_DIR)/tests/interactive-confirmation-test
LOCALIZATION_TEST_TARGET := $(BUILD_DIR)/tests/localization-test
LOCALIZATION_MISSING_CATALOG_TEST_TARGET := $(BUILD_DIR)/tests/localization-missing-catalog-test
CLI_LOCALIZATION_TEST_TARGET := $(BUILD_DIR)/tests/moguet-cli-localization-test
XDG_PATHS_TEST_TARGET := $(BUILD_DIR)/tests/xdg-paths-test
XDG_DIRECTORY_SAFETY_TEST_TARGET := $(BUILD_DIR)/tests/xdg-directory-safety-test
XDG_STATE_LOG_TEST_TARGET := $(BUILD_DIR)/tests/xdg-state-log-test
TRUSTED_CACHE_TEST_TARGET := $(BUILD_DIR)/tests/trusted-cache-test
TRUSTED_CACHE_SUPPORT_HEADER := tests/trusted_cache_test_support.hpp
ROOT_EXECUTION_IDENTITY_TEST_TARGET := $(BUILD_DIR)/tests/moguet-root-execution-identity-test
COMMANDS_INSPECT_TEST_TARGET := build/tests/moguet-commands-inspect-test
AUR_UPDATE_COMMAND_TEST_TARGET := build/tests/moguet-aur-update-command-test
UPGRADE_ALL_COMMAND_TEST_TARGET := build/tests/moguet-upgrade-all-command-test
AUR_RPC_VALIDATION_TEST_TARGET := build/tests/moguet-aur-rpc-validation-test
AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET := build/tests/aur-rpc-envelope-validation-test
COMMANDS_SYNC_TEST_TARGET := build/tests/moguet-commands-sync-test
SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET := build/tests/moguet-source-install-characterization-test
APP_CONFIG_MODULE_TEST_TARGET := build/tests/app-config-test
APP_CONFIG_INTEGRATION_TEST_TARGET := build/tests/moguet-app-config-test
PROVIDER_SELECTION_TEST_TARGET := $(BUILD_DIR)/tests/provider-selection-test
PROVIDER_INSTALLED_STATE_TEST_TARGET := $(BUILD_DIR)/tests/provider-installed-state-test
DEPENDENCY_CONSTRAINT_TEST_TARGET := $(BUILD_DIR)/tests/dependency-constraint-test
PACKAGE_RELATION_TEST_TARGET := $(BUILD_DIR)/tests/package-relation-test
PACKAGE_RELATION_OBSERVATION_TEST_TARGET := $(BUILD_DIR)/tests/package-relation-observation-test
PACKAGE_RELATION_ASSESSMENT_TEST_TARGET := $(BUILD_DIR)/tests/package-relation-assessment-test
PACKAGE_CONSTRAINT_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/package-constraint-metadata-test
BUILD_PLAN_RELATION_ASSESSMENT_STUB_SOURCE := tests/stubs/build-plan-relation-assessment/assessment_stub.cpp
AUR_CONSTRAINT_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/aur-constraint-metadata-test
ROOT_PACKAGE_CANDIDATE_TEST_TARGET := $(BUILD_DIR)/tests/root-package-candidate-test
ROOT_PACKAGE_SEARCH_TEST_TARGET := $(BUILD_DIR)/tests/root-package-search-test
ROOT_PACKAGE_SELECTION_TEST_TARGET := $(BUILD_DIR)/tests/root-package-selection-test
ROOT_PACKAGE_ROUTE_PROJECTION_TEST_TARGET := $(BUILD_DIR)/tests/root-package-route-projection-test
LOCAL_PACKAGE_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/local-package-metadata-test
LOCAL_SOURCE_ROOT_TEST_TARGET := $(BUILD_DIR)/tests/local-source-root-test
LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_TARGET := $(BUILD_DIR)/tests/local-dependency-plan-projection-test
LOCAL_SOURCE_WORKSPACE_TEST_TARGET := $(BUILD_DIR)/tests/local-source-workspace-test
LOCAL_SOURCE_BUILD_TEST_TARGET := $(BUILD_DIR)/tests/local-source-build-test
USER_CONFIG_MODULE_TEST_TARGET := $(BUILD_DIR)/tests/user-config-test
PACKAGE_IDENTIFIER_TEST_TARGET := build/tests/package-identifier-test
SHELL_WORDS_TEST_TARGET := build/tests/shell-words-test
SOURCE_ENVIRONMENT_TEST_TARGET := build/tests/source-environment-test
ARTIFACT_WORKSPACE_TEST_TARGET := build/tests/artifact-workspace-test
MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET := build/tests/multiple-artifact-workspace-test
MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_TARGET := $(BUILD_DIR)/tests/makepkg-assignment-precedence-test
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
CLI_DIAGNOSTIC_MODEL_TEST_TARGET := $(BUILD_DIR)/tests/cli-diagnostic-model-test
RUNTIME_CLI_CONNECTION_TEST_TARGET := $(BUILD_DIR)/tests/runtime-cli-connection-test
DEPENDENCY_PLAN_MODEL_TEST_TARGET := $(BUILD_DIR)/tests/dependency-plan-model-test
BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_TARGET := $(BUILD_DIR)/tests/build-plan-artifact-target-projection-test
UNIFIED_PLAN_OBSERVATION_TEST_TARGET := $(BUILD_DIR)/tests/unified-plan-observation-test
UNIFIED_PLAN_PROJECTION_TEST_TARGET := $(BUILD_DIR)/tests/unified-plan-projection-test
UNIFIED_PLAN_RENDERER_TEST_TARGET := $(BUILD_DIR)/tests/unified-plan-renderer-test
REPOSITORY_QUERY_TEST_TARGET := $(BUILD_DIR)/tests/repository-query-test
ARTIFACT_INSTALL_PLAN_TEST_TARGET := $(BUILD_DIR)/tests/artifact-install-plan-test
ARTIFACT_SELECTION_MODEL_TEST_TARGET := $(BUILD_DIR)/tests/artifact-selection-model-test
ARTIFACT_IDENTITY_SELECTION_TEST_TARGET := $(BUILD_DIR)/tests/artifact-identity-selection-test
PACKAGE_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/package-metadata-test
PACKAGE_METADATA_INTEGRATION_TEST_TARGET := $(BUILD_DIR)/tests/package-metadata-integration-test
UPGRADE_BASELINE_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/moguet-upgrade-baseline-metadata-test

# --- インストール先設定 ---
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
COMPDIR     ?= /usr/share/bash-completion/completions
ZSHCOMPDIR  ?= /usr/share/zsh/site-functions
FISHCOMPDIR ?= /usr/share/fish/vendor_completions.d
MANDIR      ?= $(PREFIX)/share/man/man1
JAMANDIR    ?= $(PREFIX)/share/man/ja/man1
LICENSEDIR  ?= $(PREFIX)/share/licenses/$(PACKAGE_NAME)
DOCDIR      ?= $(PREFIX)/share/doc/$(PACKAGE_NAME)
LOCALEDIR   ?= /usr/share/locale

GETTEXT_DOMAIN := moguet
PO_DIR := po
POTFILES_FILE := $(PO_DIR)/POTFILES.in
LINGUAS_FILE := $(PO_DIR)/LINGUAS
POT_FILE := $(PO_DIR)/$(GETTEXT_DOMAIN).pot
LINGUAS := $(strip $(shell sed 's/[[:space:]]*\#.*//' $(LINGUAS_FILE) 2>/dev/null))
LOCALE_BUILD_DIR := $(BUILD_DIR)/locale
MO_FILES := $(foreach locale,$(LINGUAS),$(LOCALE_BUILD_DIR)/$(locale)/LC_MESSAGES/$(GETTEXT_DOMAIN).mo)
MOGUET_TEST_CATALOG_DIR := $(abspath $(LOCALE_BUILD_DIR))
LOCALIZATION_MISSING_CATALOG_DIR := $(abspath $(BUILD_DIR)/tests/missing-locale)
MOGUET_TEST_ZZ_PO := tests/fixtures/localization/zz.po
MOGUET_TEST_ZZ_MO := $(LOCALE_BUILD_DIR)/zz/LC_MESSAGES/$(GETTEXT_DOMAIN).mo
LOCALIZATION_INVALID_FORMAT_PO := tests/fixtures/localization/invalid-format.po
MOGUET_TEST_BROKEN_MO := $(LOCALE_BUILD_DIR)/broken/LC_MESSAGES/$(GETTEXT_DOMAIN).mo
LOCALIZATION_CONFIG_HEADER := $(BUILD_DIR)/generated/localization_config.hpp

PROJECT_LICENSE_FILES := \
	LICENSE \
	LICENSES/jpacker-MIT-legacy.txt \
	LICENSES/curl.txt \
	LICENSES/nlohmann-json-MIT.txt \
	LICENSES/tomlplusplus-MIT.txt \
	LICENSES/bjoern-hoehrmann-utf8-MIT.txt
COMPLIANCE_DOC_FILES := \
	THIRD_PARTY_NOTICES.md \
	docs/LICENSING.md
PUBLIC_DOC_FILES := \
	README.md \
	README.ja.md \
	docs/migration/v1-to-v2.md \
	docs/migration/v1-to-v2.ja.md

# --- コンパイラ設定 ---
CXX       ?= g++
CCACHE    ?=
CXXFLAGS  ?= -O2 -pipe
LDFLAGS   ?=
CPPFLAGS  ?=
PKG_CONFIG ?= pkg-config
XGETTEXT ?= xgettext
MSGMERGE ?= msgmerge
MSGCMP ?= msgcmp
MSGFMT ?= msgfmt
MSGGREP ?= msggrep
NM ?= nm
LIBALPM_CPPFLAGS = $(shell $(PKG_CONFIG) --cflags libalpm)
LIBALPM_LDLIBS   = $(shell $(PKG_CONFIG) --libs libalpm)
BASE_CXXFLAGS := -std=c++20 -Wall -Wextra -DMOGUET_VERSION=\"$(VERSION)\"
MY_CXXFLAGS = $(BASE_CXXFLAGS) -I$(BUILD_DIR)/generated
MY_LDLIBS   := -lcurl
XGETTEXT_OPTIONS := \
	--language=C++ \
	--from-code=UTF-8 \
	--keyword=translate_message:1 \
	--keyword=translate_plural_message:1,2 \
	--keyword=format_translated_message:1 \
	--keyword=format_translated_plural_message:1,2 \
	--keyword=add_reduction_issue:4 \
	--keyword=add_localized_operation_issue:3 \
	--keyword=make_localized_execution_issue:2 \
	--keyword=make_localized_preparation_issue:2 \
	--keyword=retain_localized_build_unit_selection_issue:3 \
	--flag=format_translated_message:1:c++-format \
	--flag=format_translated_plural_message:1:c++-format \
	--flag=format_translated_plural_message:2:c++-format \
	--add-comments=TRANSLATORS: \
	--package-name=Moguet \
	--package-version=$(VERSION) \
	--no-location \
	--no-wrap
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
TEST_SRCS := $(SRCS)
CLI_LOCALIZATION_TEST_SRCS := $(SRCS)
APP_CONFIG_INTEGRATION_TEST_SRCS := $(SRCS)
COMMANDS_INSPECT_TEST_SRCS := \
	$(filter-out $(SRC_DIR)/aur_rpc.cpp $(SRC_DIR)/repository_query.cpp,$(SRCS)) \
	tests/commands_inspect_aur_stub.cpp \
	tests/stubs/commands-inspect/repository_query_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
COMMANDS_INSPECT_REQUIRED_PRODUCTION_TEST_SRCS := \
	$(filter-out $(SRC_DIR)/aur_rpc.cpp $(SRC_DIR)/repository_query.cpp,$(SRCS))
COMMANDS_INSPECT_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/commands_inspect_aur_stub.cpp \
	tests/stubs/commands-inspect/repository_query_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
COMMANDS_INSPECT_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/aur_rpc.cpp \
	$(SRC_DIR)/repository_query.cpp
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
AUR_UPDATE_COMMAND_REQUIRED_PRODUCTION_TEST_SRCS = \
	$(filter-out $(AUR_UPDATE_COMMAND_FORBIDDEN_TEST_SRCS),$(SRCS))
AUR_UPDATE_COMMAND_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/aur-update-command/operation_stub.cpp \
	tests/stubs/upgrade-all-command/operation_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
AUR_UPDATE_COMMAND_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	$(SRC_DIR)/aur_update_operation_result.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/upgrade_all_operation.cpp
# POLICY(#281): final CLI testはparser/routing/presentationをproductionのままlinkし、
# aggregate operation capabilityだけをscenario stubへ差し替える。
UPGRADE_ALL_COMMAND_TEST_SRCS := \
	$(filter-out $(SRC_DIR)/upgrade_all_operation.cpp,$(SRCS)) \
	tests/stubs/upgrade-all-command/operation_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
UPGRADE_ALL_COMMAND_REQUIRED_PRODUCTION_TEST_SRCS = \
	$(filter-out $(UPGRADE_ALL_COMMAND_FORBIDDEN_TEST_SRCS),$(SRCS))
UPGRADE_ALL_COMMAND_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/upgrade-all-command/operation_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
UPGRADE_ALL_COMMAND_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/upgrade_all_operation.cpp

AUR_RPC_VALIDATION_TEST_SRCS := \
	$(SRCS) \
	tests/stubs/package-metadata/alpm_stub.cpp
AUR_RPC_ENVELOPE_VALIDATION_TEST_SRCS := \
	tests/aur_rpc_validation_test.cpp \
	$(SRC_DIR)/aur_rpc.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
# POLICY(#217): final sync CLI testはparser/routing/selection/executionを
# productionのままlinkし、candidate search transport、AUR RPC、libalpm APIを
# deterministic stubへ差し替える。repository adapter自体はproduction ownerを使う。
COMMANDS_SYNC_TEST_SRCS := \
	$(filter-out \
		$(SRC_DIR)/aur_rpc.cpp \
		$(SRC_DIR)/root_package_search.cpp, \
		$(SRCS)) \
	tests/stubs/commands-sync/aur_rpc_stub.cpp \
	tests/stubs/commands-sync/root_package_search_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
COMMANDS_SYNC_REQUIRED_PRODUCTION_TEST_SRCS = \
	$(filter-out $(COMMANDS_SYNC_FORBIDDEN_TEST_SRCS),$(SRCS))
COMMANDS_SYNC_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/commands-sync/aur_rpc_stub.cpp \
	tests/stubs/commands-sync/root_package_search_stub.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
COMMANDS_SYNC_FORBIDDEN_TEST_SRCS := \
	$(SRC_DIR)/aur_rpc.cpp \
	$(SRC_DIR)/root_package_search.cpp
SOURCE_INSTALL_CHARACTERIZATION_TEST_SRCS := \
	tests/source_install_characterization.cpp \
	$(filter-out $(SRC_DIR)/moguet.cpp,$(SRCS)) \
	tests/stubs/package-metadata/alpm_stub.cpp
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
# Blocked production resultをunified projectionへ直接通す。function sectionは
# このtestがsystem/source route以外のadapter dependencyを再実装しないために使う。
SYSTEM_SOURCE_UPGRADE_TEST_SRCS := \
	tests/system_source_upgrade_test.cpp \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/unified_plan_projection.cpp \
	$(SRC_DIR)/unified_plan_observation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/cache_authority.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/system-source-upgrade/phase_stub.cpp
SYSTEM_SOURCE_UPGRADE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/unified_plan_projection.cpp \
	$(SRC_DIR)/unified_plan_observation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/cache_authority.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp
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
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/aur-update-execution-preflight/preflight_stub.cpp
# POLICY(#353): preflight consumes an already-projected PlanReason. It must not
# own assessment, observation, installed inventory, or metadata query logic.
AUR_UPDATE_EXECUTION_PREFLIGHT_FORBIDDEN_RELATION_AUTHORITY_TEST_SRCS := \
	$(SRC_DIR)/build_plan_relation_assessment.cpp \
	$(SRC_DIR)/installed_package_relation_inventory.cpp \
	$(SRC_DIR)/package_relation_assessment.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/package_constraint_metadata.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/repository_query.cpp
AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_SRCS := \
	tests/aur_update_execution_preflight_integration_test.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/build_plan_relation_assessment.cpp \
	$(SRC_DIR)/installed_package_relation_inventory.cpp \
	$(SRC_DIR)/package_relation_assessment.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_constraint_metadata.cpp \
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
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	tests/stubs/aur-update-execution-preparation/preparation_stub.cpp
AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_SRCS := \
	tests/aur_update_execution_preparation_integration_test.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_preference.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
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
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
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
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
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
	$(SRC_DIR)/app_config.cpp \
	$(SRC_DIR)/provider_selection.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/upgrade_all_plan.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_plan.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
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
	$(SRC_DIR)/app_config.cpp \
	$(SRC_DIR)/provider_selection.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
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
	$(SRC_DIR)/app_config.cpp \
	$(SRC_DIR)/provider_selection.cpp \
	$(SRC_DIR)/upgrade_all_operation.cpp \
	$(SRC_DIR)/upgrade_all_operation_result.cpp \
	$(SRC_DIR)/operation_state_model.cpp \
	$(SRC_DIR)/diagnostic_projection.cpp \
	$(SRC_DIR)/presentation_projection.cpp \
	$(SRC_DIR)/upgrade_all_presentation_projection.cpp \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/unified_plan_projection.cpp \
	$(SRC_DIR)/unified_plan_observation.cpp \
	$(SRC_DIR)/cache_authority.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/upgrade_all_plan.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_plan.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
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
	$(SRC_DIR)/app_config.cpp \
	$(SRC_DIR)/provider_selection.cpp \
	$(SRC_DIR)/upgrade_all_operation.cpp \
	$(SRC_DIR)/upgrade_all_operation_result.cpp \
	$(SRC_DIR)/operation_state_model.cpp \
	$(SRC_DIR)/diagnostic_projection.cpp \
	$(SRC_DIR)/presentation_projection.cpp \
	$(SRC_DIR)/upgrade_all_presentation_projection.cpp \
	$(SRC_DIR)/system_source_upgrade.cpp \
	$(SRC_DIR)/unified_plan_projection.cpp \
	$(SRC_DIR)/unified_plan_observation.cpp \
	$(SRC_DIR)/cache_authority.cpp \
	$(SRC_DIR)/filtered_aur_update_operation.cpp \
	$(SRC_DIR)/aur_update_query.cpp \
	$(SRC_DIR)/aur_update_execution_preflight.cpp \
	$(SRC_DIR)/aur_update_execution_preparation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/aur_update_execution_runner.cpp \
	$(SRC_DIR)/aur_update_operation_result.cpp
UPGRADE_ALL_OPERATION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(UPGRADE_ALL_OPERATION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#217): root candidate testはpure value ownerとidentifierだけをlinkし、
# search、interaction、routing、external mutationのownerを持ち込まない。
ROOT_PACKAGE_CANDIDATE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/root_package_candidate.cpp \
	$(SRC_DIR)/package_identifier.cpp
ROOT_PACKAGE_CANDIDATE_TEST_SRCS := \
	tests/root_package_candidate_test.cpp \
	$(ROOT_PACKAGE_CANDIDATE_ALLOWED_PRODUCTION_TEST_SRCS)
ROOT_PACKAGE_CANDIDATE_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(ROOT_PACKAGE_CANDIDATE_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#217): root search testはcandidate aggregation ownerとpure modelだけを
# productionからlinkし、actual network/libalpm/CLI/selection/mutation境界をstubへ切る。
ROOT_PACKAGE_SEARCH_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/root_package_search.cpp \
	$(SRC_DIR)/root_package_candidate.cpp \
	$(SRC_DIR)/package_identifier.cpp
ROOT_PACKAGE_SEARCH_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/root-package-search/search_stub.cpp
ROOT_PACKAGE_SEARCH_TEST_SRCS := \
	tests/root_package_search_test.cpp \
	$(ROOT_PACKAGE_SEARCH_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(ROOT_PACKAGE_SEARCH_REQUIRED_TEST_SUPPORT_SRCS)
ROOT_PACKAGE_SEARCH_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(ROOT_PACKAGE_SEARCH_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#217): root selection testはpure expression / interaction ownerと
# validated candidate modelだけをlinkし、search adapter、provider、CLI、mutationを持ち込まない。
ROOT_PACKAGE_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/root_package_selection.cpp \
	$(SRC_DIR)/root_package_candidate.cpp \
	$(SRC_DIR)/package_identifier.cpp
ROOT_PACKAGE_SELECTION_TEST_SRCS := \
	tests/root_package_selection_test.cpp \
	$(ROOT_PACKAGE_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS)
ROOT_PACKAGE_SELECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(ROOT_PACKAGE_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#217): root route projection testはselection invariantとsource-aware
# projectionだけをlinkし、commands_syncやexternal execution ownerへ接続しない。
ROOT_PACKAGE_ROUTE_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/root_package_route_projection.cpp \
	$(SRC_DIR)/root_package_selection.cpp \
	$(SRC_DIR)/root_package_candidate.cpp \
	$(SRC_DIR)/package_identifier.cpp
ROOT_PACKAGE_ROUTE_PROJECTION_TEST_SRCS := \
	tests/root_package_route_projection_test.cpp \
	$(ROOT_PACKAGE_ROUTE_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS)
ROOT_PACKAGE_ROUTE_PROJECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(ROOT_PACKAGE_ROUTE_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#271): local metadata testはfilesystem / AUR / CLIから独立した
# .SRCINFO value modelとparserだけをlinkする。
LOCAL_PACKAGE_METADATA_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/local_package_metadata.cpp
LOCAL_PACKAGE_METADATA_TEST_SRCS := \
	tests/local_package_metadata_test.cpp \
	$(LOCAL_PACKAGE_METADATA_ALLOWED_PRODUCTION_TEST_SRCS)
LOCAL_PACKAGE_METADATA_FIXTURE_FILES := \
	$(wildcard tests/fixtures/local-package-metadata/*.srcinfo)
LOCAL_PACKAGE_METADATA_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(LOCAL_PACKAGE_METADATA_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#271): local root testはread-only filesystem capabilityとmetadata
# parserだけをlinkし、cache / process / CLI / source executionへ接続しない。
LOCAL_SOURCE_ROOT_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/local_source_root.cpp \
	$(SRC_DIR)/local_package_metadata.cpp
LOCAL_SOURCE_ROOT_TEST_SRCS := \
	tests/local_source_root_test.cpp \
	$(LOCAL_SOURCE_ROOT_ALLOWED_PRODUCTION_TEST_SRCS)
LOCAL_SOURCE_ROOT_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(LOCAL_SOURCE_ROOT_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#271): local dependency projection testはtyped metadata adapterと既存
# BuildPlan resolverだけをproductionからlinkし、filesystem / CLI / execution ownerを
# 持ち込まない。AUR / repository query boundaryは専用stubへ差し替える。
LOCAL_DEPENDENCY_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/local_dependency_plan_projection.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp
LOCAL_DEPENDENCY_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS := \
	$(BUILD_PLAN_RELATION_ASSESSMENT_STUB_SOURCE) \
	tests/stubs/local-dependency-plan/aur_rpc_stub.cpp \
	tests/stubs/local-dependency-plan/repository_query_stub.cpp
LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_SRCS := \
	tests/local_dependency_plan_projection_test.cpp \
	$(LOCAL_DEPENDENCY_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(LOCAL_DEPENDENCY_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS)
LOCAL_DEPENDENCY_PLAN_PROJECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(LOCAL_DEPENDENCY_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#271): local source workspace testはsource snapshot ownerとread-only
# local root/cache capability supportだけをlinkし、process / CLI / install ownerを
# 持ち込まない。
LOCAL_SOURCE_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/local_source_workspace.cpp \
	$(SRC_DIR)/local_source_root.cpp \
	$(SRC_DIR)/local_package_metadata.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/logging.cpp
LOCAL_SOURCE_WORKSPACE_TEST_SRCS := \
	tests/local_source_workspace_test.cpp \
	$(LOCAL_SOURCE_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS)
LOCAL_SOURCE_WORKSPACE_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(LOCAL_SOURCE_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#271): local source build composition testはsource/artifact workspace、
# local plan、artifact target/identity selectionの既存ownerだけをproductionから
# linkする。query/process境界は専用stubへ切り、install/CLI/AUR transportを除外する。
LOCAL_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/local_source_build.cpp \
	$(SRC_DIR)/local_source_workspace.cpp \
	$(SRC_DIR)/local_source_root.cpp \
	$(SRC_DIR)/local_package_metadata.cpp \
	$(SRC_DIR)/local_dependency_plan_projection.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_identity_selection.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/logging.cpp
LOCAL_SOURCE_BUILD_REQUIRED_TEST_SUPPORT_SRCS := \
	$(BUILD_PLAN_RELATION_ASSESSMENT_STUB_SOURCE) \
	tests/stubs/local-dependency-plan/aur_rpc_stub.cpp \
	tests/stubs/local-dependency-plan/repository_query_stub.cpp \
	tests/stubs/local-source-build/process_stub.cpp
LOCAL_SOURCE_BUILD_TEST_SRCS := \
	tests/local_source_build_test.cpp \
	$(LOCAL_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(LOCAL_SOURCE_BUILD_REQUIRED_TEST_SUPPORT_SRCS)
LOCAL_SOURCE_BUILD_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(LOCAL_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#350): Slice 2 contract test links only the pure value/projection
# owners. Production command, parser, renderer, transport, and executor owners
# remain outside this focused binary.
CLI_DIAGNOSTIC_MODEL_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/diagnostic_projection.cpp \
	$(SRC_DIR)/operation_state_model.cpp \
	$(SRC_DIR)/presentation_projection.cpp
CLI_DIAGNOSTIC_MODEL_TEST_SRCS := \
	tests/cli_diagnostic_model_test.cpp \
	$(CLI_DIAGNOSTIC_MODEL_ALLOWED_PRODUCTION_TEST_SRCS)
CLI_DIAGNOSTIC_MODEL_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(CLI_DIAGNOSTIC_MODEL_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#350): Slice 3 focused boundary links only the runtime CLI adapter,
# its existing assignment parser, and diagnostic presentation adapter.
RUNTIME_CLI_CONNECTION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/cli_runtime_contract.cpp \
	$(SRC_DIR)/cli_public_projection.cpp \
	$(SRC_DIR)/runtime_diagnostic.cpp \
	$(SRC_DIR)/source_environment.cpp
RUNTIME_CLI_CONNECTION_TEST_SRCS := \
	tests/runtime_cli_connection_test.cpp \
	$(RUNTIME_CLI_CONNECTION_ALLOWED_PRODUCTION_TEST_SRCS)
RUNTIME_CLI_CONNECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(RUNTIME_CLI_CONNECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#268, #353): dependency resolver/model testはresolverとpure model
# supportだけをproductionからlinkする。Slice 4 relation readiness casesは
# computed assessment stubを入力とし、assessment classifier、installed inventory、
# query、metadata/process/source-build execution ownerを持ち込まない。
DEPENDENCY_PLAN_MODEL_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp
DEPENDENCY_PLAN_MODEL_REQUIRED_TEST_SUPPORT_SRCS := \
	$(BUILD_PLAN_RELATION_ASSESSMENT_STUB_SOURCE) \
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
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/package_identifier.cpp
BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS := \
	tests/build_plan_artifact_target_projection_test.cpp \
	$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS)
BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#352): unified observation testはpure modelと既存constraint valueだけを
# linkし、transport、process、filesystem adapter、CLI、executorを持ち込まない。
UNIFIED_PLAN_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/unified_plan_observation.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp
UNIFIED_PLAN_OBSERVATION_TEST_SRCS := \
	tests/unified_plan_observation_test.cpp \
	$(UNIFIED_PLAN_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS)
UNIFIED_PLAN_OBSERVATION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(UNIFIED_PLAN_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#352): projection adapter test links read-only plan/root adapters only.
# Production transport, process, CLI, command builder, and executor owners are
# excluded; the two query stubs satisfy existing LocalBuildPlan resolver ABI.
UNIFIED_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/unified_plan_projection.cpp \
	$(SRC_DIR)/unified_plan_observation.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/root_package_route_projection.cpp \
	$(SRC_DIR)/root_package_selection.cpp \
	$(SRC_DIR)/root_package_candidate.cpp \
	$(SRC_DIR)/local_source_root.cpp \
	$(SRC_DIR)/local_package_metadata.cpp \
	$(SRC_DIR)/local_dependency_plan_projection.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp
UNIFIED_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS := \
	$(BUILD_PLAN_RELATION_ASSESSMENT_STUB_SOURCE) \
	tests/stubs/local-dependency-plan/aur_rpc_stub.cpp \
	tests/stubs/local-dependency-plan/repository_query_stub.cpp
UNIFIED_PLAN_PROJECTION_TEST_SRCS := \
	tests/unified_plan_projection_test.cpp \
	$(UNIFIED_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(UNIFIED_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS)
UNIFIED_PLAN_PROJECTION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(UNIFIED_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#352): renderer test links the observation model, presentation-only
# helpers, and the existing LocalBuildPlan resolver solely to construct a
# production typed-authority fixture. Query transport is replaced by the same
# dedicated stubs as the local projection test. The renderer object itself is
# kept behind the resolver/provider/constraint-evaluation symbol firewall.
UNIFIED_PLAN_RENDERER_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/unified_plan_renderer.cpp \
	$(SRC_DIR)/unified_plan_observation.cpp \
	$(SRC_DIR)/local_dependency_plan_projection.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp
UNIFIED_PLAN_RENDERER_REQUIRED_TEST_SUPPORT_SRCS := \
	$(BUILD_PLAN_RELATION_ASSESSMENT_STUB_SOURCE) \
	tests/stubs/local-dependency-plan/aur_rpc_stub.cpp \
	tests/stubs/local-dependency-plan/repository_query_stub.cpp
UNIFIED_PLAN_RENDERER_TEST_SRCS := \
	tests/unified_plan_renderer_test.cpp \
	$(UNIFIED_PLAN_RENDERER_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(UNIFIED_PLAN_RENDERER_REQUIRED_TEST_SUPPORT_SRCS)
UNIFIED_PLAN_RENDERER_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(UNIFIED_PLAN_RENDERER_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
REPOSITORY_QUERY_TEST_SRCS := \
	tests/repository_query_test.cpp \
	$(SRC_DIR)/repository_query.cpp \
	$(SRC_DIR)/package_constraint_metadata.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
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
TRUSTED_CACHE_SRCS := \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp
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
	$(TRUSTED_CACHE_SRCS) \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/logging.cpp
# POLICY(#268): multiple workspace testはfilesystem capability ownerと、その
# 既存support translation unitだけをlinkする。
MULTIPLE_ARTIFACT_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(TRUSTED_CACHE_SRCS) \
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
# POLICY(#425): real makepkg precedence testはlocal metadataとartifact commandの
# production owner、およびそのfilesystem/process closureだけをlinkする。
# Unused local lifecycle sectionsはGCし、makepkg/process境界をstubへ差し替えない。
MAKEPKG_ASSIGNMENT_PRECEDENCE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/local_source_metadata_evaluation.cpp \
	$(SRC_DIR)/local_source_build.cpp \
	$(SRC_DIR)/local_source_root.cpp \
	$(SRC_DIR)/local_package_metadata.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(TRUSTED_CACHE_SRCS) \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/logging.cpp
MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_SRCS := \
	tests/makepkg_assignment_precedence_test.cpp \
	$(MAKEPKG_ASSIGNMENT_PRECEDENCE_ALLOWED_PRODUCTION_TEST_SRCS)
MAKEPKG_ASSIGNMENT_PRECEDENCE_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(MAKEPKG_ASSIGNMENT_PRECEDENCE_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
ARTIFACT_IDENTITY_TEST_SRCS := \
	tests/artifact_identity_test.cpp \
	$(SRC_DIR)/artifact_identity.cpp \
	$(SRC_DIR)/artifact_identity_set.cpp \
	$(SRC_DIR)/artifact_workspace.cpp \
	$(TRUSTED_CACHE_SRCS) \
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
	$(TRUSTED_CACHE_SRCS) \
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
	$(TRUSTED_CACHE_SRCS) \
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
	$(TRUSTED_CACHE_SRCS) \
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
	$(TRUSTED_CACHE_SRCS) \
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
	$(TRUSTED_CACHE_SRCS) \
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
# POLICY(#271): actual LocalBuildPlanからproduction dependency preparationへ
# 接続するtestも同じbinaryで扱う。queryは既存local-dependency stubへ差し替え、
# repository_query / AUR RPC本体は各専用targetで検証する。
PRODUCTION_SOURCE_BUILD_TEST_SRCS := \
	tests/production_source_build_test.cpp \
	$(SRC_DIR)/app_config.cpp \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/provider_selection.cpp \
	$(SRC_DIR)/source_install.cpp \
	$(SRC_DIR)/local_source_build_dependency_preparation.cpp \
	$(SRC_DIR)/local_dependency_plan_projection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/dependency_constraint_presentation.cpp \
	$(SRC_DIR)/local_package_metadata.cpp \
	$(SRC_DIR)/cache_authority.cpp \
	$(SRC_DIR)/source_install_preparation.cpp \
	$(SRC_DIR)/source_build.cpp \
	$(SRC_DIR)/interactive_confirmation.cpp \
	$(SRC_DIR)/diagnostic_projection.cpp \
	$(SRC_DIR)/runtime_diagnostic.cpp \
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
	$(TRUSTED_CACHE_SRCS) \
	$(SRC_DIR)/persistent_checkout.cpp \
	tests/stubs/trusted-git/process_stub.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/source_preference.cpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_plan_model.cpp \
	$(SRC_DIR)/package_relation_presentation.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(LOCAL_DEPENDENCY_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS) \
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
	$(SRC_DIR)/shell_words.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/package-metadata/process_stub.cpp
# POLICY(#388): installed-state lookup testはlocal metadata read phaseだけをlinkし、
# provider identity、selection policy、plan、CLI、execution ownerを持ち込まない。
PROVIDER_INSTALLED_STATE_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/provider_installed_state.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp
PROVIDER_INSTALLED_STATE_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/package-metadata/process_stub.cpp
PROVIDER_INSTALLED_STATE_TEST_SRCS := \
	tests/provider_installed_state_test.cpp \
	$(PROVIDER_INSTALLED_STATE_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(PROVIDER_INSTALLED_STATE_REQUIRED_TEST_SUPPORT_SRCS)
PROVIDER_INSTALLED_STATE_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(PROVIDER_INSTALLED_STATE_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#351): constraint model testはpure typed grammar/result ownerと
# direct libalpm comparison adapterだけをlinkし、metadata query、provider
# selection、CLI、production executionを持ち込まない。
DEPENDENCY_CONSTRAINT_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/dependency_constraint.cpp
DEPENDENCY_CONSTRAINT_TEST_SRCS := \
	tests/dependency_constraint_test.cpp \
	$(DEPENDENCY_CONSTRAINT_ALLOWED_PRODUCTION_TEST_SRCS)
DEPENDENCY_CONSTRAINT_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(DEPENDENCY_CONSTRAINT_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#353): relation model testはdeclared relation grammar/valueとpure
# version predicate、既存libalpm comparison adapterだけをlinkし、observation、
# BuildPlan、preflight、transaction ownerを持ち込まない。
PACKAGE_RELATION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp
PACKAGE_RELATION_TEST_SRCS := \
	tests/package_relation_test.cpp \
	$(PACKAGE_RELATION_ALLOWED_PRODUCTION_TEST_SRCS)
PACKAGE_RELATION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(PACKAGE_RELATION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#353): Slice 3 matching test links the owned observation domain and
# declared relation/version owners only. Plan, CLI, renderer, query, and
# transaction translation units remain outside this binary.
PACKAGE_RELATION_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/package_identifier.cpp
PACKAGE_RELATION_OBSERVATION_TEST_SRCS := \
	tests/package_relation_observation_test.cpp \
	$(PACKAGE_RELATION_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS)
PACKAGE_RELATION_OBSERVATION_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(PACKAGE_RELATION_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#353): Slice 4 assessment test links only declaration, observation,
# and assessment owners. Query, BuildPlan, renderer, preflight, and transaction
# translation units remain behind the link firewall.
PACKAGE_RELATION_ASSESSMENT_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/package_relation_assessment.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/package_identifier.cpp
PACKAGE_RELATION_ASSESSMENT_TEST_SRCS := \
	tests/package_relation_assessment_test.cpp \
	$(PACKAGE_RELATION_ASSESSMENT_ALLOWED_PRODUCTION_TEST_SRCS)
PACKAGE_RELATION_ASSESSMENT_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(PACKAGE_RELATION_ASSESSMENT_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#351): Slice 3 adapter testはlibalpm read phase、owned adapter、
# pure constraint modelだけをlinkし、legacy repository query、provider
# selection、CLI、production executionへ接続しない。
PACKAGE_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/installed_package_relation_inventory.cpp \
	$(SRC_DIR)/package_constraint_metadata.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/package_relation_observation_adapter.cpp \
	$(SRC_DIR)/package_relation_observation.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp
PACKAGE_CONSTRAINT_METADATA_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/package-metadata/process_stub.cpp
PACKAGE_CONSTRAINT_METADATA_TEST_SRCS := \
	tests/package_constraint_metadata_test.cpp \
	$(PACKAGE_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS) \
	$(PACKAGE_CONSTRAINT_METADATA_REQUIRED_TEST_SUPPORT_SRCS)
PACKAGE_CONSTRAINT_METADATA_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(PACKAGE_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
# POLICY(#351): Slice 4 AUR projection testはraw AUR valueからowned
# constraint/provider metadataへのpure adapterだけをlinkする。repository query、
# selection policy、CLI、BuildPlan、source fallback、mutation ownerは持ち込まない。
AUR_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS := \
	$(SRC_DIR)/aur_constraint_metadata.cpp \
	$(SRC_DIR)/package_relation.cpp \
	$(SRC_DIR)/dependency_constraint.cpp
AUR_CONSTRAINT_METADATA_TEST_SRCS := \
	tests/aur_constraint_metadata_test.cpp \
	$(AUR_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS)
AUR_CONSTRAINT_METADATA_FORBIDDEN_TEST_SRCS := \
	$(filter-out \
		$(AUR_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS), \
		$(SRCS))
PACKAGE_METADATA_INTEGRATION_TEST_SRCS := \
	tests/package_metadata_integration_test.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/logging.cpp
APPLICATION_IDENTITY_TEST_SRCS := \
	tests/application_identity_test.cpp
INTERACTIVE_CONFIRMATION_TEST_SRCS := \
	tests/interactive_confirmation_test.cpp \
	$(SRC_DIR)/interactive_confirmation.cpp \
	$(SRC_DIR)/logging.cpp
LOCALIZATION_TEST_SRCS := \
	tests/localization_test.cpp \
	$(SRC_DIR)/localization.cpp
XDG_PATHS_TEST_SRCS := \
	tests/xdg_paths_test.cpp \
	$(SRC_DIR)/xdg_paths.cpp
XDG_DIRECTORY_SAFETY_TEST_SRCS := \
	tests/xdg_directory_safety_test.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp
XDG_STATE_LOG_TEST_SRCS := \
	tests/xdg_state_log_test.cpp \
	$(SRC_DIR)/xdg_state_log.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/logging.cpp
TRUSTED_CACHE_TEST_SRCS := \
	tests/trusted_cache_test.cpp \
	$(SRC_DIR)/trusted_cache.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/logging.cpp
ROOT_EXECUTION_IDENTITY_DIRECT_SRCS := \
	tests/stubs/runtime-identity/geteuid_stub.cpp
APP_CONFIG_MODULE_TEST_SRCS := \
	tests/app_config_test.cpp \
	$(SRC_DIR)/app_config.cpp \
	$(SRC_DIR)/provider_selection.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/localization.cpp
PROVIDER_SELECTION_TEST_SRCS := \
	tests/provider_selection_test.cpp \
	$(SRC_DIR)/provider_selection.cpp \
	$(SRC_DIR)/dependency_constraint.cpp \
	$(SRC_DIR)/provider_installed_state_presentation.cpp \
	$(SRC_DIR)/provider_installed_state.cpp \
	$(SRC_DIR)/package_metadata.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/localization.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp \
	tests/stubs/package-metadata/process_stub.cpp
USER_CONFIG_MODULE_TEST_SRCS := \
	tests/user_config_test.cpp \
	$(SRC_DIR)/user_config.cpp \
	$(SRC_DIR)/cli_parser.cpp
PACKAGE_IDENTIFIER_TEST_SRCS := \
	tests/package_identifier_test.cpp \
	$(SRC_DIR)/package_identifier.cpp
SHELL_WORDS_TEST_SRCS := \
	tests/shell_words_test.cpp \
	$(SRC_DIR)/shell_words.cpp
SOURCE_ENVIRONMENT_TEST_SRCS := \
	tests/source_environment_test.cpp \
	$(SRC_DIR)/source_environment.cpp \
	$(SRC_DIR)/source_preference.cpp \
	$(SRC_DIR)/xdg_directory_safety.cpp \
	$(SRC_DIR)/xdg_paths.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/shell_words.cpp
PROCESS_STDIN_FD_TEST_SRCS := \
	tests/process_stdin_fd_test.cpp \
	$(SRC_DIR)/process.cpp \
	$(SRC_DIR)/logging.cpp
UPGRADE_BASELINE_METADATA_TEST_SRCS := \
	$(SRCS) \
	tests/stubs/package-metadata/alpm_stub.cpp

# Issue #380: heavyweight integration binaries keep target-specific object
# trees while sharing only the compile/link recipe infrastructure.
AUR_UPDATE_COMMAND_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-DMOGUET_ENABLE_TEST_CONFIG_PATH \
	-I$(SRC_DIR) \
	-Itests/stubs/package-metadata
AUR_UPDATE_COMMAND_TEST_LDLIBS = $(MY_LDLIBS)
AUR_UPDATE_COMMAND_FORBIDDEN_TEST_LDLIBS = $(LIBALPM_LDLIBS)

UPGRADE_ALL_COMMAND_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-DMOGUET_ENABLE_TEST_CONFIG_PATH \
	-DMOGUET_LOCALE_DIRECTORY=\"$(MOGUET_TEST_CATALOG_DIR)\" \
	-I$(SRC_DIR) \
	-Itests/stubs/package-metadata
UPGRADE_ALL_COMMAND_TEST_LDLIBS = $(MY_LDLIBS)
UPGRADE_ALL_COMMAND_FORBIDDEN_TEST_LDLIBS = $(LIBALPM_LDLIBS)

COMMANDS_SYNC_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-DMOGUET_ENABLE_TEST_CONFIG_PATH \
	-I$(SRC_DIR) \
	-Itests/stubs/package-metadata
COMMANDS_SYNC_TEST_LDLIBS = $(MY_LDLIBS)
COMMANDS_SYNC_FORBIDDEN_TEST_LDLIBS = $(LIBALPM_LDLIBS)

COMMANDS_INSPECT_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-DMOGUET_LOCALE_DIRECTORY=\"$(MOGUET_TEST_CATALOG_DIR)\" \
	-I$(SRC_DIR) \
	-Itests/stubs/package-metadata
COMMANDS_INSPECT_TEST_LDLIBS = $(MY_LDLIBS)
COMMANDS_INSPECT_FORBIDDEN_TEST_LDLIBS = $(LIBALPM_LDLIBS)

TEST_CPPFLAGS = -DMOGUET_ENABLE_TEST_OVERRIDES
TEST_LDLIBS = $(MY_LDLIBS) $(LIBALPM_LDLIBS)
CORE_REQUIRED_PRODUCTION_TEST_SRCS := $(TEST_SRCS)
CORE_REQUIRED_TEST_SUPPORT_SRCS =
CORE_FORBIDDEN_TEST_SRCS =
CORE_FORBIDDEN_TEST_LDLIBS =

CLI_LOCALIZATION_TEST_CPPFLAGS = \
	-DMOGUET_LOCALE_DIRECTORY=\"$(MOGUET_TEST_CATALOG_DIR)\" \
	-DMOGUET_ENABLE_TEST_OVERRIDES
CLI_LOCALIZATION_TEST_LDLIBS = $(MY_LDLIBS) $(LIBALPM_LDLIBS)
CLI_LOCALIZATION_REQUIRED_PRODUCTION_TEST_SRCS := $(CLI_LOCALIZATION_TEST_SRCS)
CLI_LOCALIZATION_REQUIRED_TEST_SUPPORT_SRCS =
CLI_LOCALIZATION_FORBIDDEN_TEST_SRCS =
CLI_LOCALIZATION_FORBIDDEN_TEST_LDLIBS =

APP_CONFIG_INTEGRATION_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-DMOGUET_ENABLE_TEST_CONFIG_PATH \
	-DMOGUET_ENABLE_APP_CONFIG_TEST_HOOKS
APP_CONFIG_INTEGRATION_TEST_LDLIBS = $(MY_LDLIBS) $(LIBALPM_LDLIBS)
APP_CONFIG_INTEGRATION_REQUIRED_PRODUCTION_TEST_SRCS := \
	$(APP_CONFIG_INTEGRATION_TEST_SRCS)
APP_CONFIG_INTEGRATION_REQUIRED_TEST_SUPPORT_SRCS =
APP_CONFIG_INTEGRATION_FORBIDDEN_TEST_SRCS =
APP_CONFIG_INTEGRATION_FORBIDDEN_TEST_LDLIBS =

AUR_RPC_VALIDATION_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-DMOGUET_ENABLE_AUR_RPC_TEST_HOOKS \
	-I$(SRC_DIR) \
	-Itests/stubs/package-metadata
AUR_RPC_VALIDATION_TEST_LDLIBS = $(MY_LDLIBS)
AUR_RPC_VALIDATION_REQUIRED_PRODUCTION_TEST_SRCS := $(SRCS)
AUR_RPC_VALIDATION_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/package-metadata/alpm_stub.cpp
AUR_RPC_VALIDATION_FORBIDDEN_TEST_SRCS =
AUR_RPC_VALIDATION_FORBIDDEN_TEST_LDLIBS = $(LIBALPM_LDLIBS)

SOURCE_INSTALL_CHARACTERIZATION_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-I$(SRC_DIR) \
	-Itests/stubs/package-metadata
SOURCE_INSTALL_CHARACTERIZATION_TEST_LDLIBS = $(MY_LDLIBS)
SOURCE_INSTALL_CHARACTERIZATION_REQUIRED_PRODUCTION_TEST_SRCS := \
	$(filter-out $(SRC_DIR)/moguet.cpp,$(SRCS))
SOURCE_INSTALL_CHARACTERIZATION_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/source_install_characterization.cpp \
	tests/stubs/package-metadata/alpm_stub.cpp
SOURCE_INSTALL_CHARACTERIZATION_FORBIDDEN_TEST_SRCS := $(SRC_DIR)/moguet.cpp
SOURCE_INSTALL_CHARACTERIZATION_FORBIDDEN_TEST_LDLIBS = $(LIBALPM_LDLIBS)

UPGRADE_BASELINE_METADATA_TEST_CPPFLAGS = \
	-DMOGUET_ENABLE_TEST_OVERRIDES \
	-DMOGUET_ENABLE_TEST_CONFIG_PATH \
	-DMOGUET_ENABLE_APP_CONFIG_TEST_HOOKS \
	-I$(SRC_DIR) \
	-Itests/stubs/package-metadata
UPGRADE_BASELINE_METADATA_TEST_LDLIBS = $(MY_LDLIBS)
UPGRADE_BASELINE_METADATA_REQUIRED_PRODUCTION_TEST_SRCS := $(SRCS)
UPGRADE_BASELINE_METADATA_REQUIRED_TEST_SUPPORT_SRCS := \
	tests/stubs/package-metadata/alpm_stub.cpp
UPGRADE_BASELINE_METADATA_FORBIDDEN_TEST_SRCS =
UPGRADE_BASELINE_METADATA_FORBIDDEN_TEST_LDLIBS = $(LIBALPM_LDLIBS)

HEAVY_OBJECT_PREFIXES := \
	AUR_UPDATE_COMMAND_TEST \
	UPGRADE_ALL_COMMAND_TEST \
	COMMANDS_SYNC_TEST \
	COMMANDS_INSPECT_TEST \
	TEST \
	CLI_LOCALIZATION_TEST \
	APP_CONFIG_INTEGRATION_TEST \
	AUR_RPC_VALIDATION_TEST \
	SOURCE_INSTALL_CHARACTERIZATION_TEST \
	UPGRADE_BASELINE_METADATA_TEST

define define_heavy_object_paths
$(1)_OBJECT_DIR := $(BUILD_DIR)/tests/obj/$(notdir $($(1)_TARGET))
$(1)_OBJECTS := $$(patsubst %.cpp,$$($(1)_OBJECT_DIR)/%.o,$$($(1)_SRCS))
$(1)_LINK_OBJECTS := $$($(1)_OBJECTS)
$(1)_DEPS := $$($(1)_OBJECTS:.o=.d)
$(1)_COMPILE_SIGNATURE := $$($(1)_OBJECT_DIR)/compile.signature
$(1)_LINK_SIGNATURE := $$($(1)_OBJECT_DIR)/link.signature
endef

$(foreach prefix,$(HEAVY_OBJECT_PREFIXES),\
	$(eval $(call define_heavy_object_paths,$(prefix))))

ALL_HEAVY_OBJECTS := \
	$(foreach prefix,$(HEAVY_OBJECT_PREFIXES),$($(prefix)_OBJECTS))
ALL_HEAVY_DEPS := \
	$(foreach prefix,$(HEAVY_OBJECT_PREFIXES),$($(prefix)_DEPS))
HEAVY_LOCALIZATION_OBJECTS := \
	$(foreach prefix,$(HEAVY_OBJECT_PREFIXES),\
		$($(prefix)_OBJECT_DIR)/src/localization.o)
HEAVY_LINK_FIREWALLS := \
	check-aur-update-command-link-firewall \
	check-upgrade-all-command-link-firewall \
	check-commands-sync-link-firewall \
	check-commands-inspect-link-firewall \
	check-isolated-integration-link-firewall \
	check-cli-localization-link-firewall \
	check-app-config-integration-link-firewall \
	check-aur-rpc-validation-link-firewall \
	check-source-install-characterization-link-firewall \
	check-upgrade-baseline-metadata-link-firewall

OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)
PRODUCTION_SIGNATURE_DIR := $(BUILD_DIR)/production
PRODUCTION_COMPILE_SIGNATURE := \
	$(PRODUCTION_SIGNATURE_DIR)/compile.signature
PRODUCTION_LINK_SIGNATURE := \
	$(PRODUCTION_SIGNATURE_DIR)/link.signature
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
	$(PROVIDER_INSTALLED_STATE_TEST_TARGET) \
	$(PACKAGE_METADATA_INTEGRATION_TEST_TARGET) \
	$(ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET) \
	$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET) \
	$(SEPARATED_SOURCE_BUILD_TEST_TARGET) \
	$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET) \
	$(PRODUCTION_SOURCE_BUILD_TEST_TARGET) \
	$(REPOSITORY_QUERY_TEST_TARGET) \
	$(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_TARGET) \
	$(LOCAL_SOURCE_BUILD_TEST_TARGET) \
	$(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET) \
	$(CLI_DIAGNOSTIC_MODEL_TEST_TARGET) \
	$(UNIFIED_PLAN_OBSERVATION_TEST_TARGET) \
	$(UNIFIED_PLAN_PROJECTION_TEST_TARGET) \
	$(UNIFIED_PLAN_RENDERER_TEST_TARGET) \
	$(UPGRADE_BASELINE_METADATA_TEST_TARGET)

.PHONY: all check-libalpm clean check-upgrade-all-plan-link-firewall check-system-source-upgrade-link-firewall check-aur-update-execution-runner-link-firewall check-aur-update-operation-result-link-firewall check-filtered-aur-update-operation-link-firewall check-upgrade-all-operation-link-firewall check-upgrade-all-command-link-firewall check-commands-sync-link-firewall check-provider-installed-state-link-firewall check-dependency-constraint-link-firewall check-package-relation-link-firewall check-package-relation-observation-link-firewall check-package-constraint-metadata-link-firewall check-aur-constraint-metadata-link-firewall check-root-package-candidate-link-firewall check-root-package-search-link-firewall check-root-package-selection-link-firewall check-root-package-route-projection-link-firewall check-dependency-plan-model-link-firewall check-build-plan-artifact-target-projection-link-firewall check-artifact-selection-model-link-firewall check-artifact-identity-selection-link-firewall check-multiple-artifact-workspace-link-firewall check-multiple-artifact-identity-link-firewall check-package-base-artifact-install-plan-link-firewall check-package-base-artifact-install-executor-link-firewall check-separated-package-base-source-build-link-firewall test test-host-release test-internal-identity test-application-identity test-interactive-confirmation test-xdg-paths test-xdg-directory-safety test-xdg-state-log test-trusted-cache test-runtime-identity test-app-config test-provider-selection test-provider-installed-state test-dependency-constraint test-package-relation test-package-relation-observation test-package-constraint-metadata test-aur-constraint-metadata test-root-package-candidate test-root-package-search test-root-package-selection test-root-package-route-projection test-user-config test-package-identifier test-package-metadata test-package-metadata-integration test-repository-query test-shell-words test-source-environment test-artifact-workspace test-multiple-artifact-workspace test-artifact-identity test-multiple-artifact-identity test-artifact-install-executor test-package-base-artifact-install-plan test-package-base-artifact-install-executor test-separated-source-build test-separated-package-base-source-build test-production-source-build test-process-capture test-aur-update-plan test-upgrade-all-plan test-system-source-upgrade test-aur-update-query test-aur-update-command test-upgrade-all-command test-aur-update-execution-preflight test-aur-update-execution-preflight-integration test-aur-update-execution-preparation test-aur-update-execution-runner test-aur-update-operation-result test-filtered-aur-update-operation test-upgrade-all-operation test-dependency-plan-model test-build-plan-artifact-target-projection test-artifact-install-plan test-artifact-selection-model test-artifact-identity-selection test-command-stub-contract test-markdown-links test-aur-rpc-validation test-build-cache-symlink test-cli-parser test-dry-run-command test-commands-inspect test-commands-source-maintenance test-commands-sync test-fixture-authority test-live-contract test-run-with-pty test-conflicts-replaces test-install-layout test-package-transition test-needed-contract test-pacman-routing test-pkgbuild-export test-source-build test-source-selection release-check release-check-exclusive install uninstall
.PHONY: check-local-package-metadata-link-firewall check-local-source-root-link-firewall check-local-dependency-plan-projection-link-firewall test-local-package-metadata test-local-source-root test-local-dependency-plan-projection
.PHONY: check-local-source-workspace-link-firewall check-local-source-build-link-firewall test-local-source-workspace test-local-source-build
.PHONY: check-makepkg-assignment-precedence-link-firewall test-makepkg-assignment-precedence
.PHONY: check-unified-plan-observation-link-firewall test-unified-plan-observation test-observation-contract-gate
.PHONY: check-unified-plan-projection-link-firewall test-unified-plan-projection test-projection-fixture-gate
.PHONY: check-unified-plan-renderer-link-firewall test-unified-plan-renderer
.PHONY: check-cli-diagnostic-model-link-firewall test-cli-diagnostic-model
.PHONY: check-runtime-cli-connection-link-firewall test-runtime-cli-connection
.PHONY: check-package-relation-assessment-link-firewall test-package-relation-assessment
.PHONY: check-aur-update-execution-preflight-relation-link-firewall
.PHONY: test-artifact-identity-real-pacman
.PHONY: FORCE catalogs check-catalogs check-localization-config check-pot update-po update-pot test-localization test-catalog-metadata-gate test-cli-localization-surface check-completion-freshness test-completion-schema test-public-documentation
.PHONY: test-container test-container-live test-container-live-provider test-container-live-aur test-container-live-local
.PHONY: test-validation-status
.PHONY: $(HEAVY_LINK_FIREWALLS)

all: $(TARGET) $(MANPAGES) catalogs

check-localization-config:
	@case '$(LOCALEDIR)' in \
		/*) ;; \
		*) echo "error: LOCALEDIR must be an absolute path: $(LOCALEDIR)" >&2; exit 1 ;; \
	esac
	@test -n "$(LINGUAS)" || { \
		echo "error: $(LINGUAS_FILE) must declare at least one locale" >&2; \
		exit 1; \
	}
	@set -e; for locale in $(LINGUAS); do \
		case "$$locale" in \
			.|..|*[!A-Za-z0-9_.@-]*) \
				echo "error: invalid locale token in $(LINGUAS_FILE): $$locale" >&2; \
				exit 1 ;; \
		esac; \
		test -f "$(PO_DIR)/$$locale.po" || { \
			echo "error: missing catalog source: $(PO_DIR)/$$locale.po" >&2; \
			exit 1; \
		}; \
	done

$(LOCALIZATION_CONFIG_HEADER): FORCE | check-localization-config
	@mkdir -p $(dir $@)
	@set -eu; \
		tmp_file='$@.tmp'; \
		rm -f "$$tmp_file"; \
		trap 'rm -f "$$tmp_file"' EXIT HUP INT TERM; \
		printf '%s\n' \
			'#pragma once' \
			'' \
			'#define MOGUET_LOCALE_DIRECTORY "$(LOCALEDIR)"' \
			> "$$tmp_file"; \
		if ! cmp -s "$$tmp_file" "$@"; then \
			mv "$$tmp_file" "$@"; \
		fi

FORCE:

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

$(OBJS) $(LIBALPM_BUILD_TARGETS): | check-libalpm check-localization-config
$(BUILD_DIR)/localization.o $(LIBALPM_BUILD_TARGETS): $(LOCALIZATION_CONFIG_HEADER)
$(APP_CONFIG_MODULE_TEST_TARGET) $(PROVIDER_SELECTION_TEST_TARGET): $(LOCALIZATION_CONFIG_HEADER)

$(PRODUCTION_COMPILE_SIGNATURE): FORCE
	@mkdir -p $(@D)
	@printf '%s\n' \
		'CXX=$(CXX)' \
		'CPPFLAGS=$(CPPFLAGS)' \
		'LIBALPM_CPPFLAGS=$(LIBALPM_CPPFLAGS)' \
		'CXXFLAGS=$(CXXFLAGS)' \
		'MY_CXXFLAGS=$(MY_CXXFLAGS)' \
		> $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@

$(PRODUCTION_LINK_SIGNATURE): FORCE
	@mkdir -p $(@D)
	@printf '%s\n' \
		'CXX=$(CXX)' \
		'LDFLAGS=$(LDFLAGS)' \
		'OBJECTS=$(OBJS)' \
		'MY_LDLIBS=$(MY_LDLIBS)' \
		'LIBALPM_LDLIBS=$(LIBALPM_LDLIBS)' \
		> $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@

$(OBJS): $(PRODUCTION_COMPILE_SIGNATURE)
$(TARGET): $(PRODUCTION_LINK_SIGNATURE)

$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(VERSION_FILE)
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $< (v$(VERSION))"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -MMD -MP -c $< -o $@

define define_heavy_object_rules
$$($(1)_COMPILE_SIGNATURE): FORCE
	@mkdir -p $$(@D)
	@printf '%s\n' \
		'CXX=$$(CXX)' \
		'CPPFLAGS=$$(CPPFLAGS)' \
		'LIBALPM_CPPFLAGS=$$(LIBALPM_CPPFLAGS)' \
		'CXXFLAGS=$$(CXXFLAGS)' \
		'MY_CXXFLAGS=$$(MY_CXXFLAGS)' \
		'TARGET_CPPFLAGS=$$($(1)_CPPFLAGS)' \
		> $$@.tmp
	@cmp -s $$@.tmp $$@ && rm -f $$@.tmp || mv $$@.tmp $$@

$$($(1)_LINK_SIGNATURE): FORCE
	@mkdir -p $$(@D)
	@printf '%s\n' \
		'CXX=$$(CXX)' \
		'LDFLAGS=$$(LDFLAGS)' \
		'MY_LDLIBS=$$(MY_LDLIBS)' \
		'LIBALPM_LDLIBS=$$(LIBALPM_LDLIBS)' \
		'TARGET_LDLIBS=$$($(1)_LDLIBS)' \
		'OBJECTS=$$($(1)_LINK_OBJECTS)' \
		> $$@.tmp
	@cmp -s $$@.tmp $$@ && rm -f $$@.tmp || mv $$@.tmp $$@

$$($(1)_OBJECTS): $$($(1)_OBJECT_DIR)/%.o: %.cpp $$($(1)_COMPILE_SIGNATURE)
	@mkdir -p $$(@D)
	@echo ":: Compiling $$< for $(2)"
	$$(CCACHE) $$(CXX) $$(CPPFLAGS) $$(LIBALPM_CPPFLAGS) $$(CXXFLAGS) $$(MY_CXXFLAGS) \
		$$($(1)_CPPFLAGS) -MMD -MP -c $$< -o $$@

$$($(1)_TARGET): $$($(1)_LINK_OBJECTS) $$($(1)_LINK_SIGNATURE)
	@mkdir -p $$(@D)
	@echo ":: Linking $(2)"
	$$(CXX) $$(LDFLAGS) $$($(1)_LINK_OBJECTS) -o $$@ $$($(1)_LDLIBS)
endef

$(eval $(call define_heavy_object_rules,AUR_UPDATE_COMMAND_TEST,AUR update command integration test binary))
$(eval $(call define_heavy_object_rules,UPGRADE_ALL_COMMAND_TEST,upgrade-all command integration test binary))
$(eval $(call define_heavy_object_rules,COMMANDS_SYNC_TEST,sync command characterization test binary))
$(eval $(call define_heavy_object_rules,COMMANDS_INSPECT_TEST,command inspection characterization test binary))
$(eval $(call define_heavy_object_rules,TEST,isolated integration test binary))
$(eval $(call define_heavy_object_rules,CLI_LOCALIZATION_TEST,CLI localization integration test binary))
$(eval $(call define_heavy_object_rules,APP_CONFIG_INTEGRATION_TEST,app config integration test binary))
$(eval $(call define_heavy_object_rules,AUR_RPC_VALIDATION_TEST,AUR RPC validation fake-symbol test binary))
$(eval $(call define_heavy_object_rules,SOURCE_INSTALL_CHARACTERIZATION_TEST,shared source-install characterization test binary))
$(eval $(call define_heavy_object_rules,UPGRADE_BASELINE_METADATA_TEST,upgrade baseline metadata fake-symbol test binary))

$(ALL_HEAVY_OBJECTS): | check-libalpm check-localization-config
$(HEAVY_LOCALIZATION_OBJECTS): $(LOCALIZATION_CONFIG_HEADER)

$(AUR_UPDATE_COMMAND_TEST_TARGET): | check-aur-update-command-link-firewall
$(UPGRADE_ALL_COMMAND_TEST_TARGET): | check-upgrade-all-command-link-firewall
$(COMMANDS_SYNC_TEST_TARGET): | check-commands-sync-link-firewall
$(COMMANDS_INSPECT_TEST_TARGET): | check-commands-inspect-link-firewall
$(TEST_TARGET): | check-isolated-integration-link-firewall
$(CLI_LOCALIZATION_TEST_TARGET): | check-cli-localization-link-firewall
$(APP_CONFIG_INTEGRATION_TEST_TARGET): | check-app-config-integration-link-firewall
$(AUR_RPC_VALIDATION_TEST_TARGET): | check-aur-rpc-validation-link-firewall
$(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET): | check-source-install-characterization-link-firewall
$(UPGRADE_BASELINE_METADATA_TEST_TARGET): | check-upgrade-baseline-metadata-link-firewall

-include $(ALL_HEAVY_DEPS)

# Issue #403: direct compile/link test binaries keep their existing source,
# fake/stub, include, and library profiles. Each target owns compiler-generated
# dependency metadata and signatures without sharing objects across profiles.
DIRECT_COMPILE_ARGS = $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS)
DIRECT_LIBALPM_COMPILE_ARGS = \
	$(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS)
comma := ,
GC_SECTIONS_LINK_ARG := -Wl$(comma)--gc-sections
WRAP_GETEUID_LINK_ARG := -Wl$(comma)--wrap=geteuid

NON_HEAVY_TARGETS := \
	$(APPLICATION_IDENTITY_TEST_TARGET) \
	$(INTERACTIVE_CONFIRMATION_TEST_TARGET) \
	$(LOCALIZATION_TEST_TARGET) \
	$(LOCALIZATION_MISSING_CATALOG_TEST_TARGET) \
	$(XDG_PATHS_TEST_TARGET) \
	$(XDG_DIRECTORY_SAFETY_TEST_TARGET) \
	$(XDG_STATE_LOG_TEST_TARGET) \
	$(TRUSTED_CACHE_TEST_TARGET) \
	$(ROOT_EXECUTION_IDENTITY_TEST_TARGET) \
	$(AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET) \
	$(APP_CONFIG_MODULE_TEST_TARGET) \
	$(PROVIDER_SELECTION_TEST_TARGET) \
	$(ROOT_PACKAGE_CANDIDATE_TEST_TARGET) \
	$(ROOT_PACKAGE_SEARCH_TEST_TARGET) \
	$(ROOT_PACKAGE_SELECTION_TEST_TARGET) \
	$(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_TARGET) \
	$(LOCAL_PACKAGE_METADATA_TEST_TARGET) \
	$(LOCAL_SOURCE_ROOT_TEST_TARGET) \
	$(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_TARGET) \
	$(LOCAL_SOURCE_WORKSPACE_TEST_TARGET) \
	$(LOCAL_SOURCE_BUILD_TEST_TARGET) \
	$(USER_CONFIG_MODULE_TEST_TARGET) \
	$(PACKAGE_IDENTIFIER_TEST_TARGET) \
	$(SHELL_WORDS_TEST_TARGET) \
	$(SOURCE_ENVIRONMENT_TEST_TARGET) \
	$(ARTIFACT_WORKSPACE_TEST_TARGET) \
	$(MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET) \
	$(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_TARGET) \
	$(ARTIFACT_IDENTITY_TEST_TARGET) \
	$(MULTIPLE_ARTIFACT_IDENTITY_TEST_TARGET) \
	$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_TARGET) \
	$(ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET) \
	$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET) \
	$(SEPARATED_SOURCE_BUILD_TEST_TARGET) \
	$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET) \
	$(PRODUCTION_SOURCE_BUILD_TEST_TARGET) \
	$(PROCESS_CAPTURE_TEST_TARGET) \
	$(PROCESS_STDIN_FD_TEST_TARGET) \
	$(AUR_UPDATE_PLAN_TEST_TARGET) \
	$(UPGRADE_ALL_PLAN_TEST_TARGET) \
	$(SYSTEM_SOURCE_UPGRADE_TEST_TARGET) \
	$(AUR_UPDATE_QUERY_TEST_TARGET) \
	$(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET) \
	$(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET) \
	$(AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET) \
	$(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET) \
	$(AUR_UPDATE_EXECUTION_RUNNER_TEST_TARGET) \
	$(AUR_UPDATE_OPERATION_RESULT_TEST_TARGET) \
	$(FILTERED_AUR_UPDATE_OPERATION_TEST_TARGET) \
	$(UPGRADE_ALL_OPERATION_TEST_TARGET) \
	$(CLI_DIAGNOSTIC_MODEL_TEST_TARGET) \
	$(RUNTIME_CLI_CONNECTION_TEST_TARGET) \
	$(DEPENDENCY_PLAN_MODEL_TEST_TARGET) \
	$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_TARGET) \
	$(UNIFIED_PLAN_OBSERVATION_TEST_TARGET) \
	$(UNIFIED_PLAN_PROJECTION_TEST_TARGET) \
	$(UNIFIED_PLAN_RENDERER_TEST_TARGET) \
	$(REPOSITORY_QUERY_TEST_TARGET) \
	$(ARTIFACT_INSTALL_PLAN_TEST_TARGET) \
	$(ARTIFACT_SELECTION_MODEL_TEST_TARGET) \
	$(ARTIFACT_IDENTITY_SELECTION_TEST_TARGET) \
	$(PACKAGE_METADATA_TEST_TARGET) \
	$(PROVIDER_INSTALLED_STATE_TEST_TARGET) \
	$(DEPENDENCY_CONSTRAINT_TEST_TARGET) \
	$(PACKAGE_RELATION_TEST_TARGET) \
	$(PACKAGE_RELATION_OBSERVATION_TEST_TARGET) \
	$(PACKAGE_RELATION_ASSESSMENT_TEST_TARGET) \
	$(PACKAGE_CONSTRAINT_METADATA_TEST_TARGET) \
	$(AUR_CONSTRAINT_METADATA_TEST_TARGET) \
	$(PACKAGE_METADATA_INTEGRATION_TEST_TARGET)

NON_HEAVY_TRACKED_TARGETS :=
NON_HEAVY_DEPFILES :=

# $(1): profile prefix
# $(2): preprocessor/compiler arguments
# $(3): source files read by the dependency scanner
# $(4): link arguments placed before source/link inputs
# $(5): link arguments placed after source/link inputs and before the output
# $(6): link arguments placed after the output
# $(7): optional build inputs when they differ from $(3)
define define_non_heavy_test_profile
$(1)_DIRECT_METADATA_DIR := $(BUILD_DIR)/tests/dep/$(notdir $($(1)_TEST_TARGET))
$(1)_DIRECT_DEPFILE := $$($(1)_DIRECT_METADATA_DIR)/dependencies.d
$(1)_DIRECT_COMPILE_SIGNATURE := $$($(1)_DIRECT_METADATA_DIR)/compile.signature
$(1)_DIRECT_LINK_SIGNATURE := $$($(1)_DIRECT_METADATA_DIR)/link.signature
$(1)_DIRECT_COMPILE_ARGS := $(strip $(2))
$(1)_DIRECT_SRCS := $(strip $(3))
$(1)_DIRECT_PRE_LINK_ARGS := $(strip $(4))
$(1)_DIRECT_PRE_OUTPUT_ARGS := $(strip $(5))
$(1)_DIRECT_LINK_ARGS := $(strip $(6))
$(1)_DIRECT_BUILD_INPUTS := $(if $(strip $(7)),$(strip $(7)),$(strip $(3)))
NON_HEAVY_TRACKED_TARGETS += $$($(1)_TEST_TARGET)
NON_HEAVY_DEPFILES += $$($(1)_DIRECT_DEPFILE)

$$($(1)_DIRECT_COMPILE_SIGNATURE): FORCE
	@mkdir -p $$(@D)
	@printf '%s\n' \
		'TARGET=$$($(1)_TEST_TARGET)' \
		'CXX=$$(CXX)' \
		'COMPILE_ARGS=$$($(1)_DIRECT_COMPILE_ARGS)' \
		'SOURCES=$$($(1)_DIRECT_SRCS)' \
		> $$@.tmp
	@cmp -s $$@.tmp $$@ && rm -f $$@.tmp || mv $$@.tmp $$@

$$($(1)_DIRECT_LINK_SIGNATURE): FORCE
	@mkdir -p $$(@D)
	@printf '%s\n' \
		'TARGET=$$($(1)_TEST_TARGET)' \
		'CXX=$$(CXX)' \
		'PRE_LINK_ARGS=$$($(1)_DIRECT_PRE_LINK_ARGS)' \
		'PRE_OUTPUT_ARGS=$$($(1)_DIRECT_PRE_OUTPUT_ARGS)' \
		'LINK_ARGS=$$($(1)_DIRECT_LINK_ARGS)' \
		'BUILD_INPUTS=$$($(1)_DIRECT_BUILD_INPUTS)' \
		> $$@.tmp
	@cmp -s $$@.tmp $$@ && rm -f $$@.tmp || mv $$@.tmp $$@

$$($(1)_DIRECT_DEPFILE):
	@mkdir -p $$(@D)
	@: > $$@

$$($(1)_TEST_TARGET): \
		$$($(1)_DIRECT_DEPFILE) \
		$$($(1)_DIRECT_COMPILE_SIGNATURE) \
		$$($(1)_DIRECT_LINK_SIGNATURE)
endef

define compile_non_heavy_test
@set -eu; \
	tmp_file='$($(1)_DIRECT_DEPFILE).tmp'; \
	rm -f "$$tmp_file"; \
	trap 'rm -f "$$tmp_file"' EXIT HUP INT TERM; \
	$(CXX) $($(1)_DIRECT_COMPILE_ARGS) \
		-MM -MP -MT "$($(1)_TEST_TARGET)" \
		$($(1)_DIRECT_SRCS) > "$$tmp_file"; \
	if ! cmp -s "$$tmp_file" "$($(1)_DIRECT_DEPFILE)"; then \
		mv "$$tmp_file" "$($(1)_DIRECT_DEPFILE)"; \
	fi
$(CXX) $($(1)_DIRECT_COMPILE_ARGS) \
	$($(1)_DIRECT_PRE_LINK_ARGS) $($(1)_DIRECT_BUILD_INPUTS) \
	$($(1)_DIRECT_PRE_OUTPUT_ARGS) -o $@ $($(1)_DIRECT_LINK_ARGS)
endef

$(eval $(call define_non_heavy_test_profile,APPLICATION_IDENTITY,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(APPLICATION_IDENTITY_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,INTERACTIVE_CONFIRMATION,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(INTERACTIVE_CONFIRMATION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,LOCALIZATION,$(CPPFLAGS) $(CXXFLAGS) $(BASE_CXXFLAGS) -DMOGUET_LOCALE_DIRECTORY=\"$(MOGUET_TEST_CATALOG_DIR)\" -I$(SRC_DIR),$(LOCALIZATION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,LOCALIZATION_MISSING_CATALOG,$(CPPFLAGS) $(CXXFLAGS) $(BASE_CXXFLAGS) -DMOGUET_LOCALE_DIRECTORY=\"$(LOCALIZATION_MISSING_CATALOG_DIR)\" -I$(SRC_DIR),$(LOCALIZATION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,XDG_PATHS,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(XDG_PATHS_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,XDG_DIRECTORY_SAFETY,$(DIRECT_COMPILE_ARGS) -DMOGUET_TEST_XDG_DIRECTORY_SAFETY_HOOKS -I$(SRC_DIR),$(XDG_DIRECTORY_SAFETY_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,XDG_STATE_LOG,$(DIRECT_COMPILE_ARGS) -DMOGUET_TEST_XDG_STATE_LOG_HOOKS -I$(SRC_DIR),$(XDG_STATE_LOG_TEST_SRCS),$(LDFLAGS)))
$(eval $(call define_non_heavy_test_profile,TRUSTED_CACHE,$(DIRECT_COMPILE_ARGS) -DMOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS -I$(SRC_DIR),$(TRUSTED_CACHE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ROOT_EXECUTION_IDENTITY,$(DIRECT_COMPILE_ARGS),$(ROOT_EXECUTION_IDENTITY_DIRECT_SRCS),$(LDFLAGS),$(WRAP_GETEUID_LINK_ARG),$(MY_LDLIBS) $(LIBALPM_LDLIBS),$(OBJS) $(ROOT_EXECUTION_IDENTITY_DIRECT_SRCS)))
$(eval $(call define_non_heavy_test_profile,AUR_RPC_ENVELOPE_VALIDATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_TEST_OVERRIDES -DMOGUET_ENABLE_AUR_RPC_TEST_HOOKS -I$(SRC_DIR) -Itests/stubs/package-metadata,$(AUR_RPC_ENVELOPE_VALIDATION_TEST_SRCS),,,$(MY_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,APP_CONFIG_MODULE,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(APP_CONFIG_MODULE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PROVIDER_SELECTION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests/stubs/package-metadata,$(PROVIDER_SELECTION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ROOT_PACKAGE_CANDIDATE,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(ROOT_PACKAGE_CANDIDATE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ROOT_PACKAGE_SEARCH,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR) -Itests,$(ROOT_PACKAGE_SEARCH_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ROOT_PACKAGE_SELECTION,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(ROOT_PACKAGE_SELECTION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ROOT_PACKAGE_ROUTE_PROJECTION,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,LOCAL_PACKAGE_METADATA,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(LOCAL_PACKAGE_METADATA_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,LOCAL_SOURCE_ROOT,$(DIRECT_COMPILE_ARGS) -DMOGUET_ENABLE_LOCAL_SOURCE_ROOT_TEST_HOOKS -I$(SRC_DIR),$(LOCAL_SOURCE_ROOT_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,LOCAL_DEPENDENCY_PLAN_PROJECTION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests,$(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,LOCAL_SOURCE_WORKSPACE,$(DIRECT_COMPILE_ARGS) -DMOGUET_ENABLE_LOCAL_SOURCE_WORKSPACE_TEST_HOOKS -I$(SRC_DIR) -Itests,$(LOCAL_SOURCE_WORKSPACE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,LOCAL_SOURCE_BUILD,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS -DMOGUET_ENABLE_LOCAL_SOURCE_WORKSPACE_TEST_HOOKS -I$(SRC_DIR) -Itests,$(LOCAL_SOURCE_BUILD_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,USER_CONFIG_MODULE,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(USER_CONFIG_MODULE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_IDENTIFIER,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(PACKAGE_IDENTIFIER_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,SHELL_WORDS,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(SHELL_WORDS_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,SOURCE_ENVIRONMENT,$(DIRECT_COMPILE_ARGS) -DMOGUET_ENABLE_TEST_OVERRIDES -DMOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS -I$(SRC_DIR),$(SOURCE_ENVIRONMENT_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ARTIFACT_WORKSPACE,$(DIRECT_COMPILE_ARGS) -DMOGUET_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS -DMOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS -I$(SRC_DIR),$(ARTIFACT_WORKSPACE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,MULTIPLE_ARTIFACT_WORKSPACE,$(DIRECT_COMPILE_ARGS) -DMOGUET_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS -DMOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS -I$(SRC_DIR),$(MULTIPLE_ARTIFACT_WORKSPACE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,MAKEPKG_ASSIGNMENT_PRECEDENCE,$(DIRECT_COMPILE_ARGS) -ffunction-sections -fdata-sections -I$(SRC_DIR) -Itests,$(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_SRCS),,$(GC_SECTIONS_LINK_ARG)))
$(eval $(call define_non_heavy_test_profile,ARTIFACT_IDENTITY,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(ARTIFACT_IDENTITY_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,MULTIPLE_ARTIFACT_IDENTITY,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(MULTIPLE_ARTIFACT_IDENTITY_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_BASE_ARTIFACT_INSTALL_PLAN,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ARTIFACT_INSTALL_EXECUTOR,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests/stubs/package-metadata,$(ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_HOOKS -I$(SRC_DIR) -Itests/stubs/package-metadata,$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,SEPARATED_SOURCE_BUILD,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS -I$(SRC_DIR) -Itests/stubs/package-metadata,$(SEPARATED_SOURCE_BUILD_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,SEPARATED_PACKAGE_BASE_SOURCE_BUILD,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS -I$(SRC_DIR) -Itests/stubs/package-metadata,$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PRODUCTION_SOURCE_BUILD,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS -DMOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS -DMOGUET_ENABLE_TEST_OVERRIDES -I$(SRC_DIR) -Itests -Itests/stubs/package-metadata,$(PRODUCTION_SOURCE_BUILD_TEST_SRCS),,,$(MY_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,PROCESS_CAPTURE,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(PROCESS_CAPTURE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PROCESS_STDIN_FD,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(PROCESS_STDIN_FD_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_PLAN,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(AUR_UPDATE_PLAN_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,UPGRADE_ALL_PLAN,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(UPGRADE_ALL_PLAN_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,SYSTEM_SOURCE_UPGRADE,$(DIRECT_COMPILE_ARGS) -ffunction-sections -fdata-sections -DMOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS -I$(SRC_DIR),$(SYSTEM_SOURCE_UPGRADE_TEST_SRCS),,$(GC_SECTIONS_LINK_ARG)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_QUERY,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(AUR_UPDATE_QUERY_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_EXECUTION_PREFLIGHT,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests/stubs/package-metadata -Itests/stubs/aur-update-execution-preflight-integration,$(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_EXECUTION_PREPARATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS -I$(SRC_DIR),$(AUR_UPDATE_EXECUTION_PREPARATION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_TEST_OVERRIDES -DMOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS -DMOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS -I$(SRC_DIR),$(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_EXECUTION_RUNNER,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS -DMOGUET_ENABLE_AUR_UPDATE_EXECUTION_RUNNER_TEST_HOOKS -I$(SRC_DIR),$(AUR_UPDATE_EXECUTION_RUNNER_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,AUR_UPDATE_OPERATION_RESULT,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(AUR_UPDATE_OPERATION_RESULT_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,FILTERED_AUR_UPDATE_OPERATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -DMOGUET_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS -DMOGUET_ENABLE_AUR_UPDATE_EXECUTION_RUNNER_TEST_HOOKS -I$(SRC_DIR),$(FILTERED_AUR_UPDATE_OPERATION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,UPGRADE_ALL_OPERATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -ffunction-sections -fdata-sections -DMOGUET_ENABLE_UPGRADE_ALL_OPERATION_TEST_HOOKS -DMOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS -I$(SRC_DIR),$(UPGRADE_ALL_OPERATION_TEST_SRCS),,$(GC_SECTIONS_LINK_ARG),$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,CLI_DIAGNOSTIC_MODEL,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(CLI_DIAGNOSTIC_MODEL_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,RUNTIME_CLI_CONNECTION,$(DIRECT_COMPILE_ARGS) -ffunction-sections -fdata-sections -I$(SRC_DIR),$(RUNTIME_CLI_CONNECTION_TEST_SRCS),,$(GC_SECTIONS_LINK_ARG)))
$(eval $(call define_non_heavy_test_profile,DEPENDENCY_PLAN_MODEL,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(DEPENDENCY_PLAN_MODEL_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,BUILD_PLAN_ARTIFACT_TARGET_PROJECTION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,UNIFIED_PLAN_OBSERVATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(UNIFIED_PLAN_OBSERVATION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,UNIFIED_PLAN_PROJECTION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests,$(UNIFIED_PLAN_PROJECTION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,UNIFIED_PLAN_RENDERER,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests,$(UNIFIED_PLAN_RENDERER_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,REPOSITORY_QUERY,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests/stubs/package-metadata,$(REPOSITORY_QUERY_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ARTIFACT_INSTALL_PLAN,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(ARTIFACT_INSTALL_PLAN_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ARTIFACT_SELECTION_MODEL,$(DIRECT_COMPILE_ARGS) -I$(SRC_DIR),$(ARTIFACT_SELECTION_MODEL_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,ARTIFACT_IDENTITY_SELECTION,$(DIRECT_COMPILE_ARGS) -DMOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS -I$(SRC_DIR),$(ARTIFACT_IDENTITY_SELECTION_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_METADATA,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests/stubs/package-metadata,$(PACKAGE_METADATA_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,PROVIDER_INSTALLED_STATE,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests/stubs/package-metadata,$(PROVIDER_INSTALLED_STATE_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,DEPENDENCY_CONSTRAINT,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(DEPENDENCY_CONSTRAINT_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_RELATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(PACKAGE_RELATION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_RELATION_OBSERVATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(PACKAGE_RELATION_OBSERVATION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_RELATION_ASSESSMENT,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(PACKAGE_RELATION_ASSESSMENT_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_CONSTRAINT_METADATA,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR) -Itests/stubs/package-metadata,$(PACKAGE_CONSTRAINT_METADATA_TEST_SRCS)))
$(eval $(call define_non_heavy_test_profile,AUR_CONSTRAINT_METADATA,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(AUR_CONSTRAINT_METADATA_TEST_SRCS),,,$(LIBALPM_LDLIBS)))
$(eval $(call define_non_heavy_test_profile,PACKAGE_METADATA_INTEGRATION,$(DIRECT_LIBALPM_COMPILE_ARGS) -I$(SRC_DIR),$(PACKAGE_METADATA_INTEGRATION_TEST_SRCS),,,$(LIBALPM_LDLIBS)))

NON_HEAVY_MISSING_PROFILES := \
	$(filter-out $(NON_HEAVY_TRACKED_TARGETS),$(NON_HEAVY_TARGETS))
NON_HEAVY_UNEXPECTED_PROFILES := \
	$(filter-out $(NON_HEAVY_TARGETS),$(NON_HEAVY_TRACKED_TARGETS))
ifneq ($(strip $(NON_HEAVY_MISSING_PROFILES)),)
$(error missing non-heavy test build profiles: $(NON_HEAVY_MISSING_PROFILES))
endif
ifneq ($(strip $(NON_HEAVY_UNEXPECTED_PROFILES)),)
$(error unexpected non-heavy test build profiles: $(NON_HEAVY_UNEXPECTED_PROFILES))
endif
ifneq ($(words $(NON_HEAVY_TRACKED_TARGETS)),$(words $(sort $(NON_HEAVY_TRACKED_TARGETS))))
$(error duplicate non-heavy test build profiles)
endif

ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(wildcard $(NON_HEAVY_DEPFILES))
endif

$(MANPAGE_EN): $(MANPAGE_EN_IN) $(VERSION_FILE)
	@echo ":: Generating $@ (v$(VERSION))"
	sed 's/@VERSION@/$(VERSION)/g' $(MANPAGE_EN_IN) > $@

$(MANPAGE_JA): $(MANPAGE_JA_IN) $(VERSION_FILE)
	@echo ":: Generating $@ (v$(VERSION))"
	sed 's/@VERSION@/$(VERSION)/g' $(MANPAGE_JA_IN) > $@

catalogs: check-localization-config $(MO_FILES)

$(LOCALE_BUILD_DIR)/%/LC_MESSAGES/$(GETTEXT_DOMAIN).mo: $(PO_DIR)/%.po
	@mkdir -p $(dir $@)
	@echo ":: Compiling message catalog $<"
	@set -eu; \
		tmp_file='$@.tmp'; \
		rm -f "$$tmp_file"; \
		trap 'rm -f "$$tmp_file"' EXIT HUP INT TERM; \
		$(MSGFMT) --check --check-format --check-domain \
			--output-file="$$tmp_file" "$<"; \
		mv "$$tmp_file" "$@"

check-catalogs: check-localization-config $(POT_FILE)
	@mkdir -p $(BUILD_DIR)/po-check/catalogs
	@set -eu; \
		metadata_dir="$(BUILD_DIR)/po-check/catalogs"; \
		pot_utf8="$$metadata_dir/$(GETTEXT_DOMAIN)-utf8.pot"; \
		pot_format_messages="$$metadata_dir/$(GETTEXT_DOMAIN)-c++-format.pot"; \
		po_format_messages=; \
		trap 'rm -f "$$pot_utf8" "$$pot_format_messages" "$$po_format_messages"' EXIT HUP INT TERM; \
		sed '/^"Content-Type: /s/charset=CHARSET/charset=UTF-8/' \
			"$(POT_FILE)" > "$$pot_utf8"; \
		$(MSGGREP) --sticky-flag=c++-format --force-po --no-location --no-wrap \
			--output-file="$$pot_format_messages" "$$pot_utf8"; \
		for locale in $(LINGUAS); do \
			po_file="$(PO_DIR)/$$locale.po"; \
			if ! $(MSGCMP) --no-fuzzy-matching \
					"$$po_file" "$$pot_utf8"; then \
				echo "error: $$po_file has untranslated or fuzzy messages required by $(POT_FILE); run 'make update-po' and complete the translations" >&2; \
				exit 1; \
			fi; \
			po_format_messages="$$metadata_dir/$$locale-c++-format.po"; \
			rm -f "$$po_format_messages"; \
			$(MSGGREP) --sticky-flag=c++-format --force-po --no-location --no-wrap \
				--output-file="$$po_format_messages" "$$po_file"; \
			if ! $(MSGCMP) --no-fuzzy-matching --use-fuzzy --use-untranslated \
					"$$po_format_messages" "$$pot_format_messages"; then \
				echo "error: c++-format metadata missing from $$po_file for messages required by $(POT_FILE); run 'make update-po'" >&2; \
				exit 1; \
			fi; \
			if ! $(MSGCMP) --no-fuzzy-matching --use-fuzzy --use-untranslated \
					"$$pot_format_messages" "$$po_format_messages"; then \
				echo "error: unexpected c++-format metadata in $$po_file compared with $(POT_FILE); run 'make update-po'" >&2; \
				exit 1; \
			fi; \
			rm -f "$$po_format_messages"; \
			po_format_messages=; \
			$(MSGFMT) --check --check-format --check-domain \
				--output-file=/dev/null "$$po_file"; \
		done

update-pot: $(POTFILES_FILE) $(VERSION_FILE)
	@mkdir -p $(BUILD_DIR)/po
	@echo ":: Updating $(POT_FILE)"
	@set -eu; \
		tmp_file='$(BUILD_DIR)/po/$(GETTEXT_DOMAIN).pot.tmp'; \
		rm -f "$$tmp_file"; \
		trap 'rm -f "$$tmp_file"' EXIT HUP INT TERM; \
		$(XGETTEXT) $(XGETTEXT_OPTIONS) \
			--files-from=$(POTFILES_FILE) --output="$$tmp_file"; \
		mv "$$tmp_file" "$(POT_FILE)"

update-po: update-pot check-localization-config
	@set -e; for locale in $(LINGUAS); do \
		$(MSGMERGE) --update --backup=none --no-wrap \
			"$(PO_DIR)/$$locale.po" "$(POT_FILE)"; \
	done

check-pot: $(POT_FILE) $(POTFILES_FILE) $(VERSION_FILE)
	@mkdir -p $(BUILD_DIR)/po-check
	@echo ":: Checking $(POT_FILE) extraction drift"
	@set -eu; \
		candidate='$(BUILD_DIR)/po-check/$(GETTEXT_DOMAIN).pot'; \
		candidate_normalized="$$candidate.normalized"; \
		tracked_normalized='$(BUILD_DIR)/po-check/tracked-$(GETTEXT_DOMAIN).pot.normalized'; \
		$(XGETTEXT) $(XGETTEXT_OPTIONS) \
			--files-from=$(POTFILES_FILE) --output="$$candidate"; \
		sed '/^"POT-Creation-Date:/d' "$$candidate" > "$$candidate_normalized"; \
		sed '/^"POT-Creation-Date:/d' "$(POT_FILE)" > "$$tracked_normalized"; \
		if ! cmp -s "$$tracked_normalized" "$$candidate_normalized"; then \
			diff -u "$$tracked_normalized" "$$candidate_normalized" || true; \
			echo "error: $(POT_FILE) is stale; run 'make update-pot'" >&2; \
			exit 1; \
		fi

clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)

$(APPLICATION_IDENTITY_TEST_TARGET): $(APPLICATION_IDENTITY_TEST_SRCS) $(SRC_DIR)/application_identity.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling application identity test binary"
	$(call compile_non_heavy_test,APPLICATION_IDENTITY)

$(INTERACTIVE_CONFIRMATION_TEST_TARGET): $(INTERACTIVE_CONFIRMATION_TEST_SRCS) $(SRC_DIR)/interactive_confirmation.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling interactive confirmation test binary"
	$(call compile_non_heavy_test,INTERACTIVE_CONFIRMATION)

$(LOCALIZATION_TEST_TARGET): $(LOCALIZATION_TEST_SRCS) $(SRC_DIR)/localization.hpp $(SRC_DIR)/application_identity.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling localization test binary"
	$(call compile_non_heavy_test,LOCALIZATION)

$(LOCALIZATION_MISSING_CATALOG_TEST_TARGET): $(LOCALIZATION_TEST_SRCS) $(SRC_DIR)/localization.hpp $(SRC_DIR)/application_identity.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling missing-catalog localization test binary"
	$(call compile_non_heavy_test,LOCALIZATION_MISSING_CATALOG)

$(MOGUET_TEST_ZZ_MO): $(MOGUET_TEST_ZZ_PO)
	@mkdir -p $(dir $@)
	@echo ":: Compiling test-only message catalog $<"
	@set -eu; \
		tmp_file='$@.tmp'; \
		rm -f "$$tmp_file"; \
		trap 'rm -f "$$tmp_file"' EXIT HUP INT TERM; \
		$(MSGFMT) --check --check-format --check-domain \
			--output-file="$$tmp_file" "$<"; \
		mv "$$tmp_file" "$@"

$(MOGUET_TEST_BROKEN_MO): $(LOCALIZATION_INVALID_FORMAT_PO)
	@mkdir -p $(dir $@)
	@echo ":: Compiling intentionally invalid runtime catalog $<"
	@set -eu; \
		tmp_file='$@.tmp'; \
		rm -f "$$tmp_file"; \
		trap 'rm -f "$$tmp_file"' EXIT HUP INT TERM; \
		$(MSGFMT) --check-header --check-domain \
			--output-file="$$tmp_file" "$<"; \
		mv "$$tmp_file" "$@"

$(XDG_PATHS_TEST_TARGET): $(XDG_PATHS_TEST_SRCS) $(SRC_DIR)/xdg_paths.hpp $(SRC_DIR)/application_identity.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling XDG paths test binary"
	$(call compile_non_heavy_test,XDG_PATHS)

$(XDG_DIRECTORY_SAFETY_TEST_TARGET): $(XDG_DIRECTORY_SAFETY_TEST_SRCS) $(SRC_DIR)/xdg_directory_safety.hpp $(SRC_DIR)/xdg_paths.hpp $(SRC_DIR)/application_identity.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling XDG directory safety test binary"
	$(call compile_non_heavy_test,XDG_DIRECTORY_SAFETY)

$(XDG_STATE_LOG_TEST_TARGET): $(XDG_STATE_LOG_TEST_SRCS) $(SRC_DIR)/xdg_state_log.hpp $(SRC_DIR)/xdg_directory_safety.hpp $(SRC_DIR)/xdg_paths.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/application_identity.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling XDG state log test binary"
	$(call compile_non_heavy_test,XDG_STATE_LOG)

$(TRUSTED_CACHE_TEST_TARGET): $(TRUSTED_CACHE_TEST_SRCS) $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/xdg_directory_safety.hpp $(SRC_DIR)/xdg_paths.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling trusted cache test binary"
	$(call compile_non_heavy_test,TRUSTED_CACHE)

$(ROOT_EXECUTION_IDENTITY_TEST_TARGET): $(OBJS) $(ROOT_EXECUTION_IDENTITY_DIRECT_SRCS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Linking root execution identity test binary"
	$(call compile_non_heavy_test,ROOT_EXECUTION_IDENTITY)

$(AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET): $(AUR_RPC_ENVELOPE_VALIDATION_TEST_SRCS) $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/aur_constraint_metadata.hpp $(SRC_DIR)/package_relation.hpp $(SRC_DIR)/dependency_constraint.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp tests/stubs/package-metadata/alpm_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR RPC envelope validation test binary"
	$(call compile_non_heavy_test,AUR_RPC_ENVELOPE_VALIDATION)

$(APP_CONFIG_MODULE_TEST_TARGET): $(APP_CONFIG_MODULE_TEST_SRCS) $(SRC_DIR)/app_config.hpp $(SRC_DIR)/provider_selection.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/localization.hpp $(SRC_DIR)/user_config.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling app config module test binary"
	$(call compile_non_heavy_test,APP_CONFIG_MODULE)

$(PROVIDER_SELECTION_TEST_TARGET): $(PROVIDER_SELECTION_TEST_SRCS) $(SRC_DIR)/provider_selection.hpp $(SRC_DIR)/provider_installed_state_presentation.hpp $(SRC_DIR)/provider_installed_state.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/localization.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/package-metadata/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling provider selection test binary"
	$(call compile_non_heavy_test,PROVIDER_SELECTION)

$(ROOT_PACKAGE_CANDIDATE_TEST_TARGET): $(ROOT_PACKAGE_CANDIDATE_TEST_SRCS) $(SRC_DIR)/root_package_candidate.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling root package candidate model test binary"
	$(call compile_non_heavy_test,ROOT_PACKAGE_CANDIDATE)

$(ROOT_PACKAGE_SEARCH_TEST_TARGET): $(ROOT_PACKAGE_SEARCH_TEST_SRCS) $(SRC_DIR)/root_package_search.hpp $(SRC_DIR)/root_package_candidate.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_identifier.hpp tests/stubs/root-package-search/search_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling root package search test binary"
	$(call compile_non_heavy_test,ROOT_PACKAGE_SEARCH)

$(ROOT_PACKAGE_SELECTION_TEST_TARGET): $(ROOT_PACKAGE_SELECTION_TEST_SRCS) $(SRC_DIR)/root_package_selection.hpp $(SRC_DIR)/root_package_search.hpp $(SRC_DIR)/root_package_candidate.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling root package selection test binary"
	$(call compile_non_heavy_test,ROOT_PACKAGE_SELECTION)

$(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_TARGET): $(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_SRCS) $(SRC_DIR)/root_package_route_projection.hpp $(SRC_DIR)/root_package_selection.hpp $(SRC_DIR)/root_package_search.hpp $(SRC_DIR)/root_package_candidate.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_identifier.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling root package route projection test binary"
	$(call compile_non_heavy_test,ROOT_PACKAGE_ROUTE_PROJECTION)

$(LOCAL_PACKAGE_METADATA_TEST_TARGET): $(LOCAL_PACKAGE_METADATA_TEST_SRCS) $(LOCAL_PACKAGE_METADATA_FIXTURE_FILES) $(SRC_DIR)/local_package_metadata.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling local package metadata test binary"
	$(call compile_non_heavy_test,LOCAL_PACKAGE_METADATA)

$(LOCAL_SOURCE_ROOT_TEST_TARGET): $(LOCAL_SOURCE_ROOT_TEST_SRCS) $(SRC_DIR)/local_source_root.hpp $(SRC_DIR)/local_package_metadata.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling local source root test binary"
	$(call compile_non_heavy_test,LOCAL_SOURCE_ROOT)

$(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_TARGET): $(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_SRCS) $(SRC_DIR)/local_dependency_plan_projection.hpp $(SRC_DIR)/local_package_metadata.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_plan_projection_support.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/logging.hpp tests/stubs/local-dependency-plan/query_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling local dependency plan projection test binary"
	$(call compile_non_heavy_test,LOCAL_DEPENDENCY_PLAN_PROJECTION)

$(LOCAL_SOURCE_WORKSPACE_TEST_TARGET): \
	$(LOCAL_SOURCE_WORKSPACE_TEST_SRCS) \
	$(SRC_DIR)/local_source_workspace.hpp \
	$(SRC_DIR)/local_source_root.hpp \
	$(SRC_DIR)/local_package_metadata.hpp \
	$(SRC_DIR)/application_identity.hpp \
	$(SRC_DIR)/trusted_cache.hpp \
	$(SRC_DIR)/xdg_directory_safety.hpp \
	$(SRC_DIR)/xdg_paths.hpp \
	$(SRC_DIR)/logging.hpp \
	$(SRC_DIR)/localization.hpp \
	$(TRUSTED_CACHE_SUPPORT_HEADER) \
	$(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling local source workspace test binary"
	$(call compile_non_heavy_test,LOCAL_SOURCE_WORKSPACE)

$(LOCAL_SOURCE_BUILD_TEST_TARGET): \
	$(LOCAL_SOURCE_BUILD_TEST_SRCS) \
	$(SRC_DIR)/local_source_build.hpp \
	$(SRC_DIR)/local_source_workspace.hpp \
	$(SRC_DIR)/local_source_root.hpp \
	$(SRC_DIR)/local_package_metadata.hpp \
	$(SRC_DIR)/local_dependency_plan_projection.hpp \
	$(SRC_DIR)/dependency_plan.hpp \
	$(SRC_DIR)/dependency_plan_projection_support.hpp \
	$(SRC_DIR)/dependency_provider.hpp \
	$(SRC_DIR)/dependency_spec.hpp \
	$(SRC_DIR)/aur_rpc.hpp \
	$(SRC_DIR)/repository_query.hpp \
	$(SRC_DIR)/build_plan_artifact_target_projection.hpp \
	$(SRC_DIR)/artifact_workspace.hpp \
	$(SRC_DIR)/artifact_identity.hpp \
	$(SRC_DIR)/artifact_identity_selection.hpp \
	$(SRC_DIR)/artifact_install_plan.hpp \
	$(SRC_DIR)/application_identity.hpp \
	$(SRC_DIR)/trusted_cache.hpp \
	$(SRC_DIR)/xdg_directory_safety.hpp \
	$(SRC_DIR)/xdg_paths.hpp \
	$(SRC_DIR)/source_environment.hpp \
	$(SRC_DIR)/package_identifier.hpp \
	$(SRC_DIR)/shell_words.hpp \
	$(SRC_DIR)/process.hpp \
	$(SRC_DIR)/logging.hpp \
	$(SRC_DIR)/localization.hpp \
	$(TRUSTED_CACHE_SUPPORT_HEADER) \
	tests/stubs/local-dependency-plan/query_stub.hpp \
	tests/stubs/local-source-build/process_stub.hpp \
	$(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling local source build test binary"
	$(call compile_non_heavy_test,LOCAL_SOURCE_BUILD)

$(USER_CONFIG_MODULE_TEST_TARGET): $(USER_CONFIG_MODULE_TEST_SRCS) $(SRC_DIR)/user_config.hpp $(SRC_DIR)/cli_parser.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling user config module test binary"
	$(call compile_non_heavy_test,USER_CONFIG_MODULE)

$(PACKAGE_IDENTIFIER_TEST_TARGET): $(PACKAGE_IDENTIFIER_TEST_SRCS) $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package identifier test binary"
	$(call compile_non_heavy_test,PACKAGE_IDENTIFIER)

$(SHELL_WORDS_TEST_TARGET): $(SHELL_WORDS_TEST_SRCS) $(SRC_DIR)/shell_words.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling shell word serialization test binary"
	$(call compile_non_heavy_test,SHELL_WORDS)

$(SOURCE_ENVIRONMENT_TEST_TARGET): $(SOURCE_ENVIRONMENT_TEST_SRCS) $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/source_preference.hpp $(SRC_DIR)/xdg_directory_safety.hpp $(SRC_DIR)/xdg_paths.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling source environment test binary"
	$(call compile_non_heavy_test,SOURCE_ENVIRONMENT)

$(ARTIFACT_WORKSPACE_TEST_TARGET): $(ARTIFACT_WORKSPACE_TEST_SRCS) $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/makepkg $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact workspace test binary"
	$(call compile_non_heavy_test,ARTIFACT_WORKSPACE)

$(MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET): $(MULTIPLE_ARTIFACT_WORKSPACE_TEST_SRCS) $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/makepkg $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling multiple artifact workspace test binary"
	$(call compile_non_heavy_test,MULTIPLE_ARTIFACT_WORKSPACE)

$(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_TARGET): \
	$(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_SRCS) \
	$(SRC_DIR)/local_source_metadata_evaluation.hpp \
	$(SRC_DIR)/local_source_build.hpp \
	$(SRC_DIR)/local_source_root.hpp \
	$(SRC_DIR)/local_package_metadata.hpp \
	$(SRC_DIR)/artifact_workspace.hpp \
	$(SRC_DIR)/trusted_cache.hpp \
	$(SRC_DIR)/xdg_directory_safety.hpp \
	$(SRC_DIR)/xdg_paths.hpp \
	$(SRC_DIR)/source_environment.hpp \
	$(SRC_DIR)/package_identifier.hpp \
	$(SRC_DIR)/shell_words.hpp \
	$(SRC_DIR)/process.hpp \
	$(SRC_DIR)/logging.hpp \
	$(SRC_DIR)/localization.hpp \
	$(TRUSTED_CACHE_SUPPORT_HEADER) \
	$(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling real makepkg assignment precedence test binary"
	$(call compile_non_heavy_test,MAKEPKG_ASSIGNMENT_PRECEDENCE)

$(ARTIFACT_IDENTITY_TEST_TARGET): $(ARTIFACT_IDENTITY_TEST_SRCS) $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/artifact-identity/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact identity test binary"
	$(call compile_non_heavy_test,ARTIFACT_IDENTITY)

$(MULTIPLE_ARTIFACT_IDENTITY_TEST_TARGET): $(MULTIPLE_ARTIFACT_IDENTITY_TEST_SRCS) $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/artifact-identity/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling multiple artifact identity test binary"
	$(call compile_non_heavy_test,MULTIPLE_ARTIFACT_IDENTITY)

$(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_TARGET): $(PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_SRCS) $(SRC_DIR)/package_base_artifact_install_plan.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling PackageBase artifact install reason plan test binary"
	$(call compile_non_heavy_test,PACKAGE_BASE_ARTIFACT_INSTALL_PLAN)

$(ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET): $(ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) $(SRC_DIR)/artifact_install_executor.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact install executor fake-symbol test binary"
	$(call compile_non_heavy_test,ARTIFACT_INSTALL_EXECUTOR)

$(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_TARGET): $(PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR_TEST_SRCS) $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling PackageBase artifact install executor fake-symbol test binary"
	$(call compile_non_heavy_test,PACKAGE_BASE_ARTIFACT_INSTALL_EXECUTOR)

$(SEPARATED_SOURCE_BUILD_TEST_TARGET): $(SEPARATED_SOURCE_BUILD_TEST_SRCS) $(SRC_DIR)/separated_source_build.hpp $(SRC_DIR)/artifact_install_executor.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_workspace.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/trusted_cache.hpp $(SRC_DIR)/source_environment.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling separated source-build lifecycle fake-symbol test binary"
	$(call compile_non_heavy_test,SEPARATED_SOURCE_BUILD)

$(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_TARGET): $(SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_SRCS) $(TRUSTED_CACHE_SUPPORT_HEADER) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling separated PackageBase source-build lifecycle fake-symbol test binary"
	$(call compile_non_heavy_test,SEPARATED_PACKAGE_BASE_SOURCE_BUILD)

$(PRODUCTION_SOURCE_BUILD_TEST_TARGET): $(PRODUCTION_SOURCE_BUILD_TEST_SRCS) tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/artifact-install-executor/process_stub.hpp tests/stubs/local-dependency-plan/query_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling production source-build fake-symbol test binary"
	$(call compile_non_heavy_test,PRODUCTION_SOURCE_BUILD)

$(PROCESS_CAPTURE_TEST_TARGET): $(PROCESS_CAPTURE_TEST_SRCS) $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling process capture test binary"
	$(call compile_non_heavy_test,PROCESS_CAPTURE)

$(PROCESS_STDIN_FD_TEST_TARGET): $(PROCESS_STDIN_FD_TEST_SRCS) $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling process stdin fd test binary"
	$(call compile_non_heavy_test,PROCESS_STDIN_FD)

$(AUR_UPDATE_PLAN_TEST_TARGET): $(AUR_UPDATE_PLAN_TEST_SRCS) $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update plan model test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_PLAN)

$(UPGRADE_ALL_PLAN_TEST_TARGET): $(UPGRADE_ALL_PLAN_TEST_SRCS) $(SRC_DIR)/upgrade_all_plan.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling upgrade-all plan model test binary"
	$(call compile_non_heavy_test,UPGRADE_ALL_PLAN)

$(SYSTEM_SOURCE_UPGRADE_TEST_TARGET): $(SYSTEM_SOURCE_UPGRADE_TEST_SRCS) tests/stubs/system-source-upgrade/phase_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling system/source upgrade phase fake-symbol test binary"
	$(call compile_non_heavy_test,SYSTEM_SOURCE_UPGRADE)

$(AUR_UPDATE_QUERY_TEST_TARGET): $(AUR_UPDATE_QUERY_TEST_SRCS) $(SRC_DIR)/aur_update_query.hpp $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update query fake-symbol test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_QUERY)

$(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_SRCS) $(SRC_DIR)/aur_update_execution_preflight.hpp $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/localization.hpp tests/stubs/aur-update-execution-preflight/preflight_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preflight fake-symbol test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_EXECUTION_PREFLIGHT)

$(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_SRCS) $(SRC_DIR)/aur_update_execution_preflight.hpp $(SRC_DIR)/aur_update_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_plan_projection_support.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/aur-update-execution-preflight-integration/integration_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preflight production composition test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION)

$(AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREPARATION_TEST_SRCS) tests/stubs/aur-update-execution-preparation/preparation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preparation fake-symbol test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_EXECUTION_PREPARATION)

$(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET): $(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_SRCS) tests/stubs/aur-update-execution-preparation-integration/preparation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution preparation production-reader composition test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION)

$(AUR_UPDATE_EXECUTION_RUNNER_TEST_TARGET): $(AUR_UPDATE_EXECUTION_RUNNER_TEST_SRCS) tests/stubs/aur-update-execution-preparation/preparation_stub.hpp tests/stubs/aur-update-execution-runner/execution_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR update execution runner fake-symbol test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_EXECUTION_RUNNER)

$(AUR_UPDATE_OPERATION_RESULT_TEST_TARGET): $(AUR_UPDATE_OPERATION_RESULT_TEST_SRCS) tests/stubs/aur-update-execution-preparation/preparation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling pure AUR update operation result test binary"
	$(call compile_non_heavy_test,AUR_UPDATE_OPERATION_RESULT)

$(FILTERED_AUR_UPDATE_OPERATION_TEST_TARGET): $(FILTERED_AUR_UPDATE_OPERATION_TEST_SRCS) tests/stubs/filtered-aur-update-operation/query_stub.hpp tests/stubs/aur-update-execution-preflight/preflight_stub.hpp tests/stubs/aur-update-execution-preparation/preparation_stub.hpp tests/stubs/aur-update-execution-runner/execution_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling filtered AUR update operation production-composition test binary"
	$(call compile_non_heavy_test,FILTERED_AUR_UPDATE_OPERATION)

$(UPGRADE_ALL_OPERATION_TEST_TARGET): $(UPGRADE_ALL_OPERATION_TEST_SRCS) tests/stubs/upgrade-all-operation/operation_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling upgrade-all operation production-composition test binary"
	$(call compile_non_heavy_test,UPGRADE_ALL_OPERATION)

$(CLI_DIAGNOSTIC_MODEL_TEST_TARGET): $(CLI_DIAGNOSTIC_MODEL_TEST_SRCS) $(SRC_DIR)/cli_authority.hpp $(SRC_DIR)/diagnostic_model.hpp $(SRC_DIR)/diagnostic_projection.hpp $(SRC_DIR)/operation_state_model.hpp $(SRC_DIR)/presentation_projection.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling CLI/diagnostic pure-model test binary"
	$(call compile_non_heavy_test,CLI_DIAGNOSTIC_MODEL)

$(RUNTIME_CLI_CONNECTION_TEST_TARGET): $(RUNTIME_CLI_CONNECTION_TEST_SRCS) $(SRC_DIR)/cli_runtime_contract.hpp $(SRC_DIR)/runtime_diagnostic.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling runtime CLI connection focused test binary"
	$(call compile_non_heavy_test,RUNTIME_CLI_CONNECTION)

$(DEPENDENCY_PLAN_MODEL_TEST_TARGET): $(DEPENDENCY_PLAN_MODEL_TEST_SRCS) $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_plan_projection_support.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling dependency plan model test binary"
	$(call compile_non_heavy_test,DEPENDENCY_PLAN_MODEL)

$(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_TARGET): $(BUILD_PLAN_ARTIFACT_TARGET_PROJECTION_TEST_SRCS) $(SRC_DIR)/build_plan_artifact_target_projection.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling BuildPlan artifact target projection test binary"
	$(call compile_non_heavy_test,BUILD_PLAN_ARTIFACT_TARGET_PROJECTION)

$(UNIFIED_PLAN_OBSERVATION_TEST_TARGET): $(UNIFIED_PLAN_OBSERVATION_TEST_SRCS) $(SRC_DIR)/unified_plan_observation.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/root_package_candidate.hpp $(SRC_DIR)/local_source_root.hpp $(SRC_DIR)/local_package_metadata.hpp $(SRC_DIR)/package_constraint_metadata.hpp $(SRC_DIR)/package_base_artifact_install_plan.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling unified plan observation pure-model test binary"
	$(call compile_non_heavy_test,UNIFIED_PLAN_OBSERVATION)

$(UNIFIED_PLAN_PROJECTION_TEST_TARGET): $(UNIFIED_PLAN_PROJECTION_TEST_SRCS) $(SRC_DIR)/unified_plan_projection.hpp $(SRC_DIR)/unified_plan_observation.hpp $(SRC_DIR)/build_plan_artifact_target_projection.hpp $(SRC_DIR)/root_package_route_projection.hpp $(SRC_DIR)/root_package_search.hpp $(SRC_DIR)/local_dependency_plan_projection.hpp $(SRC_DIR)/local_source_root.hpp $(SRC_DIR)/aur_update_query.hpp $(SRC_DIR)/aur_update_execution_preflight.hpp $(SRC_DIR)/system_source_upgrade.hpp $(SRC_DIR)/upgrade_all_operation.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling unified plan projection adapter test binary"
	$(call compile_non_heavy_test,UNIFIED_PLAN_PROJECTION)

$(UNIFIED_PLAN_RENDERER_TEST_TARGET): $(UNIFIED_PLAN_RENDERER_TEST_SRCS) $(SRC_DIR)/unified_plan_renderer.hpp $(SRC_DIR)/unified_plan_observation.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/local_dependency_plan_projection.hpp $(SRC_DIR)/localization.hpp $(SRC_DIR)/system_source_upgrade.hpp $(SRC_DIR)/upgrade_all_operation.hpp tests/stubs/local-dependency-plan/query_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling unified plan renderer focused test binary"
	$(call compile_non_heavy_test,UNIFIED_PLAN_RENDERER)

$(REPOSITORY_QUERY_TEST_TARGET): $(REPOSITORY_QUERY_TEST_SRCS) $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/package_constraint_metadata.hpp $(SRC_DIR)/dependency_constraint.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/localization.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/repository-query/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling repository query fake-symbol test binary"
	$(call compile_non_heavy_test,REPOSITORY_QUERY)

$(ARTIFACT_INSTALL_PLAN_TEST_TARGET): $(ARTIFACT_INSTALL_PLAN_TEST_SRCS) $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact install plan test binary"
	$(call compile_non_heavy_test,ARTIFACT_INSTALL_PLAN)

$(ARTIFACT_SELECTION_MODEL_TEST_TARGET): $(ARTIFACT_SELECTION_MODEL_TEST_SRCS) $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact selection model test binary"
	$(call compile_non_heavy_test,ARTIFACT_SELECTION_MODEL)

$(ARTIFACT_IDENTITY_SELECTION_TEST_TARGET): $(ARTIFACT_IDENTITY_SELECTION_TEST_SRCS) $(SRC_DIR)/artifact_identity_selection.hpp $(SRC_DIR)/artifact_identity.hpp $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/dependency_provider.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact identity selection test binary"
	$(call compile_non_heavy_test,ARTIFACT_IDENTITY_SELECTION)

$(PACKAGE_METADATA_TEST_TARGET): $(PACKAGE_METADATA_TEST_SRCS) $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/localization.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/package-metadata/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package metadata fake-symbol test binary"
	$(call compile_non_heavy_test,PACKAGE_METADATA)

$(PROVIDER_INSTALLED_STATE_TEST_TARGET): $(PROVIDER_INSTALLED_STATE_TEST_SRCS) $(SRC_DIR)/provider_installed_state.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/localization.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/package-metadata/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling provider installed-state test binary"
	$(call compile_non_heavy_test,PROVIDER_INSTALLED_STATE)

$(DEPENDENCY_CONSTRAINT_TEST_TARGET): $(DEPENDENCY_CONSTRAINT_TEST_SRCS) $(SRC_DIR)/dependency_constraint.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling dependency constraint model test binary"
	$(call compile_non_heavy_test,DEPENDENCY_CONSTRAINT)

$(PACKAGE_RELATION_TEST_TARGET): $(PACKAGE_RELATION_TEST_SRCS) $(SRC_DIR)/package_relation.hpp $(SRC_DIR)/dependency_constraint.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package relation model test binary"
	$(call compile_non_heavy_test,PACKAGE_RELATION)

$(PACKAGE_RELATION_OBSERVATION_TEST_TARGET): $(PACKAGE_RELATION_OBSERVATION_TEST_SRCS) $(SRC_DIR)/package_relation_observation.hpp $(SRC_DIR)/package_relation.hpp $(SRC_DIR)/dependency_constraint.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package relation observation test binary"
	$(call compile_non_heavy_test,PACKAGE_RELATION_OBSERVATION)

$(PACKAGE_RELATION_ASSESSMENT_TEST_TARGET): $(PACKAGE_RELATION_ASSESSMENT_TEST_SRCS) $(SRC_DIR)/package_relation_assessment.hpp $(SRC_DIR)/package_relation_observation.hpp $(SRC_DIR)/package_relation.hpp $(SRC_DIR)/dependency_constraint.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package relation assessment test binary"
	$(call compile_non_heavy_test,PACKAGE_RELATION_ASSESSMENT)

$(PACKAGE_CONSTRAINT_METADATA_TEST_TARGET): $(PACKAGE_CONSTRAINT_METADATA_TEST_SRCS) $(SRC_DIR)/installed_package_relation_inventory.hpp $(SRC_DIR)/package_relation_observation_adapter.hpp $(SRC_DIR)/package_relation_observation.hpp $(SRC_DIR)/package_constraint_metadata.hpp $(SRC_DIR)/package_relation.hpp $(SRC_DIR)/dependency_constraint.hpp $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/localization.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/package-metadata/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package constraint metadata adapter test binary"
	$(call compile_non_heavy_test,PACKAGE_CONSTRAINT_METADATA)

$(AUR_CONSTRAINT_METADATA_TEST_TARGET): $(AUR_CONSTRAINT_METADATA_TEST_SRCS) $(SRC_DIR)/aur_constraint_metadata.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/package_relation.hpp $(SRC_DIR)/dependency_constraint.hpp $(SRC_DIR)/dependency_provider.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling AUR constraint metadata projection test binary"
	$(call compile_non_heavy_test,AUR_CONSTRAINT_METADATA)

$(PACKAGE_METADATA_INTEGRATION_TEST_TARGET): $(PACKAGE_METADATA_INTEGRATION_TEST_SRCS) $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/installed_package.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/shell_words.hpp $(SRC_DIR)/logging.hpp $(SRC_DIR)/localization.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package metadata integration test binary"
	$(call compile_non_heavy_test,PACKAGE_METADATA_INTEGRATION)

test-internal-identity: $(MANPAGES)
	python3 scripts/check-internal-identity.py

test-cli-localization-surface: check-pot $(POT_FILE) $(POTFILES_FILE) scripts/check-cli-localization-surface.py
	python3 scripts/check-cli-localization-surface.py

test-application-identity: $(APPLICATION_IDENTITY_TEST_TARGET)
	$(abspath $(APPLICATION_IDENTITY_TEST_TARGET)) "$(VERSION)"

test-interactive-confirmation: $(INTERACTIVE_CONFIRMATION_TEST_TARGET)
	$(abspath $(INTERACTIVE_CONFIRMATION_TEST_TARGET))

test-localization: check-catalogs $(LOCALIZATION_TEST_TARGET) $(LOCALIZATION_MISSING_CATALOG_TEST_TARGET) $(CLI_LOCALIZATION_TEST_TARGET) $(MO_FILES) $(MOGUET_TEST_ZZ_MO) $(MOGUET_TEST_BROKEN_MO) $(LOCALIZATION_INVALID_FORMAT_PO)
	sh tests/test-localization.sh \
		$(abspath $(LOCALIZATION_TEST_TARGET)) \
		$(abspath $(LOCALIZATION_MISSING_CATALOG_TEST_TARGET)) \
		$(MOGUET_TEST_CATALOG_DIR) \
		$(LOCALIZATION_MISSING_CATALOG_DIR) \
		$(abspath $(LOCALIZATION_INVALID_FORMAT_PO)) \
		"$(MSGFMT)" \
		$(abspath $(CLI_LOCALIZATION_TEST_TARGET))

test-catalog-metadata-gate: $(PO_DIR)/ja.po $(POT_FILE) $(LINGUAS_FILE) $(POTFILES_FILE)
	sh tests/test-catalog-metadata-gate.sh \
		"$(MAKE)" \
		"$(abspath .)" \
		"$(abspath $(PO_DIR))" \
		"$(XGETTEXT)" \
		"$(MSGCMP)" \
		"$(MSGFMT)" \
		"$(MSGGREP)"

test-xdg-paths: $(XDG_PATHS_TEST_TARGET)
	$(abspath $(XDG_PATHS_TEST_TARGET))

test-xdg-directory-safety: $(XDG_DIRECTORY_SAFETY_TEST_TARGET)
	$(abspath $(XDG_DIRECTORY_SAFETY_TEST_TARGET))

test-xdg-state-log: $(XDG_STATE_LOG_TEST_TARGET)
	$(abspath $(XDG_STATE_LOG_TEST_TARGET))

test-trusted-cache: $(TRUSTED_CACHE_TEST_TARGET)
	$(abspath $(TRUSTED_CACHE_TEST_TARGET))

test-runtime-identity: $(TARGET) $(ROOT_EXECUTION_IDENTITY_TEST_TARGET) $(APP_CONFIG_INTEGRATION_TEST_TARGET)
	sh tests/test-runtime-identity.sh \
		$(abspath $(TARGET)) \
		$(abspath $(ROOT_EXECUTION_IDENTITY_TEST_TARGET)) \
		$(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET))

test-app-config: $(APP_CONFIG_MODULE_TEST_TARGET) $(APP_CONFIG_INTEGRATION_TEST_TARGET)
	sh tests/test-app-config.sh $(abspath $(APP_CONFIG_MODULE_TEST_TARGET)) $(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET))

test-provider-selection: $(PROVIDER_SELECTION_TEST_TARGET)
	@test -s "$(PROVIDER_SELECTION_DIRECT_DEPFILE)" || { \
		echo "error: provider selection compiler depfile is missing" >&2; \
		exit 1; \
	}
	@grep -F 'src/dependency_constraint.hpp' \
		"$(PROVIDER_SELECTION_DIRECT_DEPFILE)" >/dev/null || { \
		echo "error: provider selection depfile lost transitive dependency_constraint.hpp" >&2; \
		exit 1; \
	}
	@test -s "$(PROVIDER_SELECTION_DIRECT_COMPILE_SIGNATURE)" || { \
		echo "error: provider selection compile signature is missing" >&2; \
		exit 1; \
	}
	@test -s "$(PROVIDER_SELECTION_DIRECT_LINK_SIGNATURE)" || { \
		echo "error: provider selection link signature is missing" >&2; \
		exit 1; \
	}
	$(abspath $(PROVIDER_SELECTION_TEST_TARGET))

check-provider-installed-state-link-firewall:
	@echo ":: Checking provider installed-state link firewall"
	@test "$(words $(PROVIDER_INSTALLED_STATE_TEST_SRCS))" -eq "$(words $(sort $(PROVIDER_INSTALLED_STATE_TEST_SRCS)))" || { \
		echo "error: provider installed-state test source list contains duplicates" >&2; \
		exit 1; \
	}
	@set -e; for source in $(PROVIDER_INSTALLED_STATE_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(PROVIDER_INSTALLED_STATE_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: provider installed-state test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(PROVIDER_INSTALLED_STATE_REQUIRED_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(PROVIDER_INSTALLED_STATE_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: provider installed-state test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(PROVIDER_INSTALLED_STATE_FORBIDDEN_TEST_SRCS),$(PROVIDER_INSTALLED_STATE_TEST_SRCS))" || { \
		echo "error: provider installed-state test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test "$(words $(filter tests/stubs/%,$(PROVIDER_INSTALLED_STATE_TEST_SRCS)))" -eq "$(words $(PROVIDER_INSTALLED_STATE_REQUIRED_TEST_SUPPORT_SRCS))" || { \
		echo "error: provider installed-state test links an unexpected test stub" >&2; \
		exit 1; \
	}

test-provider-installed-state: check-provider-installed-state-link-firewall $(PROVIDER_INSTALLED_STATE_TEST_TARGET)
	$(abspath $(PROVIDER_INSTALLED_STATE_TEST_TARGET))

check-dependency-constraint-link-firewall:
	@echo ":: Checking dependency constraint model link firewall"
	@test "$(words $(DEPENDENCY_CONSTRAINT_TEST_SRCS))" -eq "$(words $(sort $(DEPENDENCY_CONSTRAINT_TEST_SRCS)))" || { \
		echo "error: dependency constraint model test source list contains duplicates" >&2; \
		exit 1; \
	}
	@set -e; for source in $(DEPENDENCY_CONSTRAINT_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(DEPENDENCY_CONSTRAINT_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: dependency constraint model test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(DEPENDENCY_CONSTRAINT_FORBIDDEN_TEST_SRCS),$(DEPENDENCY_CONSTRAINT_TEST_SRCS))" || { \
		echo "error: dependency constraint model test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(DEPENDENCY_CONSTRAINT_TEST_SRCS))" || { \
		echo "error: dependency constraint model test links a test stub" >&2; \
		exit 1; \
	}

test-dependency-constraint: check-dependency-constraint-link-firewall $(DEPENDENCY_CONSTRAINT_TEST_TARGET)
	$(abspath $(DEPENDENCY_CONSTRAINT_TEST_TARGET))

check-package-relation-link-firewall:
	@echo ":: Checking package relation model link firewall"
	@test "$(words $(PACKAGE_RELATION_TEST_SRCS))" -eq "$(words $(sort $(PACKAGE_RELATION_TEST_SRCS)))" || { \
		echo "error: package relation model test source list contains duplicates" >&2; \
		exit 1; \
	}
	@set -e; for source in $(PACKAGE_RELATION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(PACKAGE_RELATION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: package relation model test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(PACKAGE_RELATION_FORBIDDEN_TEST_SRCS),$(PACKAGE_RELATION_TEST_SRCS))" || { \
		echo "error: package relation model test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(PACKAGE_RELATION_TEST_SRCS))" || { \
		echo "error: package relation model test links a test stub" >&2; \
		exit 1; \
	}

test-package-relation: check-package-relation-link-firewall $(PACKAGE_RELATION_TEST_TARGET)
	$(abspath $(PACKAGE_RELATION_TEST_TARGET))

check-package-relation-observation-link-firewall:
	@echo ":: Checking package relation observation link firewall"
	@test "$(words $(PACKAGE_RELATION_OBSERVATION_TEST_SRCS))" -eq "$(words $(sort $(PACKAGE_RELATION_OBSERVATION_TEST_SRCS)))" || { \
		echo "error: package relation observation test source list contains duplicates" >&2; \
		exit 1; \
	}
	@set -e; for source in $(PACKAGE_RELATION_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(PACKAGE_RELATION_OBSERVATION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: package relation observation test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(PACKAGE_RELATION_OBSERVATION_FORBIDDEN_TEST_SRCS),$(PACKAGE_RELATION_OBSERVATION_TEST_SRCS))" || { \
		echo "error: package relation observation test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(PACKAGE_RELATION_OBSERVATION_TEST_SRCS))" || { \
		echo "error: package relation observation test links a test stub" >&2; \
		exit 1; \
	}

test-package-relation-observation: check-package-relation-observation-link-firewall $(PACKAGE_RELATION_OBSERVATION_TEST_TARGET)
	$(abspath $(PACKAGE_RELATION_OBSERVATION_TEST_TARGET))

check-package-relation-assessment-link-firewall:
	@echo ":: Checking package relation assessment link firewall"
	@test "$(words $(PACKAGE_RELATION_ASSESSMENT_TEST_SRCS))" -eq "$(words $(sort $(PACKAGE_RELATION_ASSESSMENT_TEST_SRCS)))" || { \
		echo "error: package relation assessment test source list contains duplicates" >&2; \
		exit 1; \
	}
	@set -e; for source in $(PACKAGE_RELATION_ASSESSMENT_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(PACKAGE_RELATION_ASSESSMENT_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: package relation assessment test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(PACKAGE_RELATION_ASSESSMENT_FORBIDDEN_TEST_SRCS),$(PACKAGE_RELATION_ASSESSMENT_TEST_SRCS))" || { \
		echo "error: package relation assessment test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(PACKAGE_RELATION_ASSESSMENT_TEST_SRCS))" || { \
		echo "error: package relation assessment test links a test stub" >&2; \
		exit 1; \
	}

test-package-relation-assessment: check-package-relation-assessment-link-firewall $(PACKAGE_RELATION_ASSESSMENT_TEST_TARGET)
	$(abspath $(PACKAGE_RELATION_ASSESSMENT_TEST_TARGET))

check-package-constraint-metadata-link-firewall:
	@echo ":: Checking package constraint metadata adapter link firewall"
	@test "$(words $(PACKAGE_CONSTRAINT_METADATA_TEST_SRCS))" -eq "$(words $(sort $(PACKAGE_CONSTRAINT_METADATA_TEST_SRCS)))" || { \
		echo "error: package constraint metadata test source list contains duplicates" >&2; \
		exit 1; \
	}
	@set -e; for source in $(PACKAGE_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(PACKAGE_CONSTRAINT_METADATA_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: package constraint metadata test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(PACKAGE_CONSTRAINT_METADATA_FORBIDDEN_TEST_SRCS),$(PACKAGE_CONSTRAINT_METADATA_TEST_SRCS))" || { \
		echo "error: package constraint metadata test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test "$(words $(filter tests/stubs/%,$(PACKAGE_CONSTRAINT_METADATA_TEST_SRCS)))" -eq "$(words $(PACKAGE_CONSTRAINT_METADATA_REQUIRED_TEST_SUPPORT_SRCS))" || { \
		echo "error: package constraint metadata test links an unexpected test stub" >&2; \
		exit 1; \
	}

test-package-constraint-metadata: check-package-constraint-metadata-link-firewall $(PACKAGE_CONSTRAINT_METADATA_TEST_TARGET)
	$(abspath $(PACKAGE_CONSTRAINT_METADATA_TEST_TARGET))

check-aur-constraint-metadata-link-firewall:
	@echo ":: Checking AUR constraint metadata projection link firewall"
	@test "$(words $(AUR_CONSTRAINT_METADATA_TEST_SRCS))" -eq "$(words $(sort $(AUR_CONSTRAINT_METADATA_TEST_SRCS)))" || { \
		echo "error: AUR constraint metadata test source list contains duplicates" >&2; \
		exit 1; \
	}
	@set -e; for source in $(AUR_CONSTRAINT_METADATA_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(AUR_CONSTRAINT_METADATA_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: AUR constraint metadata test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(AUR_CONSTRAINT_METADATA_FORBIDDEN_TEST_SRCS),$(AUR_CONSTRAINT_METADATA_TEST_SRCS))" || { \
		echo "error: AUR constraint metadata test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(AUR_CONSTRAINT_METADATA_TEST_SRCS))" || { \
		echo "error: AUR constraint metadata test links a test stub" >&2; \
		exit 1; \
	}

test-aur-constraint-metadata: check-aur-constraint-metadata-link-firewall $(AUR_CONSTRAINT_METADATA_TEST_TARGET)
	$(abspath $(AUR_CONSTRAINT_METADATA_TEST_TARGET))

check-root-package-candidate-link-firewall:
	@echo ":: Checking root package candidate model link firewall"
	@set -e; for source in $(ROOT_PACKAGE_CANDIDATE_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(ROOT_PACKAGE_CANDIDATE_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: root package candidate test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(ROOT_PACKAGE_CANDIDATE_FORBIDDEN_TEST_SRCS),$(ROOT_PACKAGE_CANDIDATE_TEST_SRCS))" || { \
		echo "error: root package candidate test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(ROOT_PACKAGE_CANDIDATE_TEST_SRCS))" || { \
		echo "error: root package candidate test links a test stub" >&2; \
		exit 1; \
	}

test-root-package-candidate: check-root-package-candidate-link-firewall $(ROOT_PACKAGE_CANDIDATE_TEST_TARGET)
	$(abspath $(ROOT_PACKAGE_CANDIDATE_TEST_TARGET))

check-root-package-search-link-firewall:
	@echo ":: Checking root package search link firewall"
	@set -e; for source in $(ROOT_PACKAGE_SEARCH_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(ROOT_PACKAGE_SEARCH_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: root package search test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(ROOT_PACKAGE_SEARCH_REQUIRED_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(ROOT_PACKAGE_SEARCH_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: root package search test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(ROOT_PACKAGE_SEARCH_FORBIDDEN_TEST_SRCS),$(ROOT_PACKAGE_SEARCH_TEST_SRCS))" || { \
		echo "error: root package search test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test "$(words $(filter tests/stubs/%,$(ROOT_PACKAGE_SEARCH_TEST_SRCS)))" -eq "$(words $(ROOT_PACKAGE_SEARCH_REQUIRED_TEST_SUPPORT_SRCS))" || { \
		echo "error: root package search test links an unexpected test stub" >&2; \
		exit 1; \
	}

test-root-package-search: check-root-package-search-link-firewall $(ROOT_PACKAGE_SEARCH_TEST_TARGET)
	$(abspath $(ROOT_PACKAGE_SEARCH_TEST_TARGET))

check-root-package-selection-link-firewall:
	@echo ":: Checking root package selection link firewall"
	@set -e; for source in $(ROOT_PACKAGE_SELECTION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(ROOT_PACKAGE_SELECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: root package selection test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(ROOT_PACKAGE_SELECTION_FORBIDDEN_TEST_SRCS),$(ROOT_PACKAGE_SELECTION_TEST_SRCS))" || { \
		echo "error: root package selection test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(ROOT_PACKAGE_SELECTION_TEST_SRCS))" || { \
		echo "error: root package selection test links a test stub" >&2; \
		exit 1; \
	}

test-root-package-selection: check-root-package-selection-link-firewall $(ROOT_PACKAGE_SELECTION_TEST_TARGET)
	$(abspath $(ROOT_PACKAGE_SELECTION_TEST_TARGET))

check-root-package-route-projection-link-firewall:
	@echo ":: Checking root package route projection link firewall"
	@set -e; for source in $(ROOT_PACKAGE_ROUTE_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: root package route projection test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(ROOT_PACKAGE_ROUTE_PROJECTION_FORBIDDEN_TEST_SRCS),$(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_SRCS))" || { \
		echo "error: root package route projection test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_SRCS))" || { \
		echo "error: root package route projection test links a test stub" >&2; \
		exit 1; \
	}

test-root-package-route-projection: check-root-package-route-projection-link-firewall $(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_TARGET)
	$(abspath $(ROOT_PACKAGE_ROUTE_PROJECTION_TEST_TARGET))

check-local-package-metadata-link-firewall:
	@echo ":: Checking local package metadata link firewall"
	@set -e; for source in $(LOCAL_PACKAGE_METADATA_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(LOCAL_PACKAGE_METADATA_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: local package metadata test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(LOCAL_PACKAGE_METADATA_FORBIDDEN_TEST_SRCS),$(LOCAL_PACKAGE_METADATA_TEST_SRCS))" || { \
		echo "error: local package metadata test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(LOCAL_PACKAGE_METADATA_TEST_SRCS))" || { \
		echo "error: local package metadata test links a test stub" >&2; \
		exit 1; \
	}

test-local-package-metadata: check-local-package-metadata-link-firewall $(LOCAL_PACKAGE_METADATA_TEST_TARGET)
	$(abspath $(LOCAL_PACKAGE_METADATA_TEST_TARGET)) \
		$(abspath tests/fixtures/local-package-metadata)

check-local-source-root-link-firewall:
	@echo ":: Checking local source root link firewall"
	@set -e; for source in $(LOCAL_SOURCE_ROOT_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(LOCAL_SOURCE_ROOT_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: local source root test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(LOCAL_SOURCE_ROOT_FORBIDDEN_TEST_SRCS),$(LOCAL_SOURCE_ROOT_TEST_SRCS))" || { \
		echo "error: local source root test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(LOCAL_SOURCE_ROOT_TEST_SRCS))" || { \
		echo "error: local source root test links a test stub" >&2; \
		exit 1; \
	}

test-local-source-root: check-local-source-root-link-firewall $(LOCAL_SOURCE_ROOT_TEST_TARGET)
	$(abspath $(LOCAL_SOURCE_ROOT_TEST_TARGET))

check-local-dependency-plan-projection-link-firewall:
	@echo ":: Checking local dependency plan projection link firewall"
	@set -e; for source in $(LOCAL_DEPENDENCY_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: local dependency plan projection test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(LOCAL_DEPENDENCY_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: local dependency plan projection test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(LOCAL_DEPENDENCY_PLAN_PROJECTION_FORBIDDEN_TEST_SRCS),$(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_SRCS))" || { \
		echo "error: local dependency plan projection test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test "$(words $(filter tests/stubs/%,$(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_SRCS)))" -eq "$(words $(LOCAL_DEPENDENCY_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS))" || { \
		echo "error: local dependency plan projection test links an unexpected test stub" >&2; \
		exit 1; \
	}

test-local-dependency-plan-projection: check-local-dependency-plan-projection-link-firewall $(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_TARGET)
	$(abspath $(LOCAL_DEPENDENCY_PLAN_PROJECTION_TEST_TARGET))

check-local-source-workspace-link-firewall:
	@echo ":: Checking local source workspace link firewall"
	@set -e; for source in $(LOCAL_SOURCE_WORKSPACE_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(LOCAL_SOURCE_WORKSPACE_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: local source workspace test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(LOCAL_SOURCE_WORKSPACE_FORBIDDEN_TEST_SRCS),$(LOCAL_SOURCE_WORKSPACE_TEST_SRCS))" || { \
		echo "error: local source workspace test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(LOCAL_SOURCE_WORKSPACE_TEST_SRCS))" || { \
		echo "error: local source workspace test links a test stub" >&2; \
		exit 1; \
	}

test-local-source-workspace: check-local-source-workspace-link-firewall $(LOCAL_SOURCE_WORKSPACE_TEST_TARGET)
	$(abspath $(LOCAL_SOURCE_WORKSPACE_TEST_TARGET))

check-local-source-build-link-firewall:
	@echo ":: Checking local source build link firewall"
	@set -e; for source in $(LOCAL_SOURCE_BUILD_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(LOCAL_SOURCE_BUILD_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: local source build test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(LOCAL_SOURCE_BUILD_REQUIRED_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(LOCAL_SOURCE_BUILD_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: local source build test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(LOCAL_SOURCE_BUILD_FORBIDDEN_TEST_SRCS),$(LOCAL_SOURCE_BUILD_TEST_SRCS))" || { \
		echo "error: local source build test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test "$(words $(filter tests/stubs/%,$(LOCAL_SOURCE_BUILD_TEST_SRCS)))" -eq "$(words $(LOCAL_SOURCE_BUILD_REQUIRED_TEST_SUPPORT_SRCS))" || { \
		echo "error: local source build test links an unexpected test stub" >&2; \
		exit 1; \
	}

test-local-source-build: check-local-source-build-link-firewall $(LOCAL_SOURCE_BUILD_TEST_TARGET)
	$(abspath $(LOCAL_SOURCE_BUILD_TEST_TARGET))

test-user-config: $(USER_CONFIG_MODULE_TEST_TARGET)
	sh tests/test-user-config.sh $(abspath $(USER_CONFIG_MODULE_TEST_TARGET))

test-package-identifier: $(PACKAGE_IDENTIFIER_TEST_TARGET)
	$(abspath $(PACKAGE_IDENTIFIER_TEST_TARGET))

test-package-metadata: $(PACKAGE_METADATA_TEST_TARGET)
	$(abspath $(PACKAGE_METADATA_TEST_TARGET))

test-package-metadata-integration: $(PACKAGE_METADATA_INTEGRATION_TEST_TARGET)
	$(abspath $(PACKAGE_METADATA_INTEGRATION_TEST_TARGET))

test-shell-words: $(SHELL_WORDS_TEST_TARGET)
	$(abspath $(SHELL_WORDS_TEST_TARGET))

test-source-environment: $(SOURCE_ENVIRONMENT_TEST_TARGET)
	XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/tests/source-environment-fixture/config) \
		HOME=$(abspath $(BUILD_DIR)/tests/source-environment-fixture/home) \
		$(abspath $(SOURCE_ENVIRONMENT_TEST_TARGET)) \
		$(abspath $(BUILD_DIR)/tests/source-environment-fixture/config/moguet/source-build.d)

test-artifact-workspace: $(ARTIFACT_WORKSPACE_TEST_TARGET)
	MOGUET_TEST_MAKEPKG_STUB=$(abspath tests/stubs/makepkg) \
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
	MOGUET_TEST_MAKEPKG_STUB=$(abspath tests/stubs/makepkg) \
		$(abspath $(MULTIPLE_ARTIFACT_WORKSPACE_TEST_TARGET))

check-makepkg-assignment-precedence-link-firewall:
	@echo ":: Checking real makepkg assignment precedence link firewall"
	@set -e; for source in $(MAKEPKG_ASSIGNMENT_PRECEDENCE_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: makepkg assignment precedence test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(MAKEPKG_ASSIGNMENT_PRECEDENCE_FORBIDDEN_TEST_SRCS),$(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_SRCS))" || { \
		echo "error: makepkg assignment precedence test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_SRCS))" || { \
		echo "error: makepkg assignment precedence test links a test stub" >&2; \
		exit 1; \
	}

test-makepkg-assignment-precedence: check-makepkg-assignment-precedence-link-firewall $(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_TARGET)
	@test -x /usr/bin/makepkg || { \
		echo "error: /usr/bin/makepkg is required for assignment precedence validation" >&2; \
		exit 1; \
	}
	@test -x /usr/bin/bsdtar || { \
		echo "error: /usr/bin/bsdtar is required for assignment precedence validation" >&2; \
		exit 1; \
	}
	$(abspath $(MAKEPKG_ASSIGNMENT_PRECEDENCE_TEST_TARGET))

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

test-artifact-identity-real-pacman:
	sh tests/test-artifact-identity-real-pacman.sh

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
	XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/tests/production-source-build-preferences/config) \
		HOME=$(abspath $(BUILD_DIR)/tests/production-source-build-preferences/home) \
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

define configure_heavy_link_firewall
$(1): private OBJECT_BUILD_LABEL := $(4)
$(1): private OBJECT_BUILD_SRCS := $($(2)_SRCS)
$(1): private OBJECT_BUILD_OBJECTS := $($(2)_OBJECTS)
$(1): private OBJECT_BUILD_LINK_OBJECTS := $($(2)_LINK_OBJECTS)
$(1): private OBJECT_BUILD_LDLIBS := $($(2)_LDLIBS)
$(1): private OBJECT_BUILD_REQUIRED_PRODUCTION_SRCS := $($(3)_REQUIRED_PRODUCTION_TEST_SRCS)
$(1): private OBJECT_BUILD_REQUIRED_SUPPORT_SRCS := $($(3)_REQUIRED_TEST_SUPPORT_SRCS)
$(1): private OBJECT_BUILD_FORBIDDEN_SRCS := $($(3)_FORBIDDEN_TEST_SRCS)
$(1): private OBJECT_BUILD_FORBIDDEN_LDLIBS := $($(3)_FORBIDDEN_TEST_LDLIBS)
endef

$(eval $(call configure_heavy_link_firewall,check-aur-update-command-link-firewall,AUR_UPDATE_COMMAND_TEST,AUR_UPDATE_COMMAND,AUR update command test))
$(eval $(call configure_heavy_link_firewall,check-upgrade-all-command-link-firewall,UPGRADE_ALL_COMMAND_TEST,UPGRADE_ALL_COMMAND,upgrade-all command test))
$(eval $(call configure_heavy_link_firewall,check-commands-sync-link-firewall,COMMANDS_SYNC_TEST,COMMANDS_SYNC,sync command test))
$(eval $(call configure_heavy_link_firewall,check-commands-inspect-link-firewall,COMMANDS_INSPECT_TEST,COMMANDS_INSPECT,command inspection test))
$(eval $(call configure_heavy_link_firewall,check-isolated-integration-link-firewall,TEST,CORE,isolated integration test))
$(eval $(call configure_heavy_link_firewall,check-cli-localization-link-firewall,CLI_LOCALIZATION_TEST,CLI_LOCALIZATION,CLI localization test))
$(eval $(call configure_heavy_link_firewall,check-app-config-integration-link-firewall,APP_CONFIG_INTEGRATION_TEST,APP_CONFIG_INTEGRATION,app config integration test))
$(eval $(call configure_heavy_link_firewall,check-aur-rpc-validation-link-firewall,AUR_RPC_VALIDATION_TEST,AUR_RPC_VALIDATION,AUR RPC validation test))
$(eval $(call configure_heavy_link_firewall,check-source-install-characterization-link-firewall,SOURCE_INSTALL_CHARACTERIZATION_TEST,SOURCE_INSTALL_CHARACTERIZATION,source-install characterization test))
$(eval $(call configure_heavy_link_firewall,check-upgrade-baseline-metadata-link-firewall,UPGRADE_BASELINE_METADATA_TEST,UPGRADE_BASELINE_METADATA,upgrade baseline metadata test))

$(HEAVY_LINK_FIREWALLS):
	@echo ":: Checking $(OBJECT_BUILD_LABEL) link firewall"
	@test "$(words $(OBJECT_BUILD_REQUIRED_PRODUCTION_SRCS) $(OBJECT_BUILD_REQUIRED_SUPPORT_SRCS))" -eq \
		"$(words $(sort $(OBJECT_BUILD_REQUIRED_PRODUCTION_SRCS) $(OBJECT_BUILD_REQUIRED_SUPPORT_SRCS)))" || { \
		echo "error: $(OBJECT_BUILD_LABEL) required source set contains duplicates" >&2; \
		exit 1; \
	}
	@test "$(words $(OBJECT_BUILD_SRCS))" -eq "$(words $(sort $(OBJECT_BUILD_SRCS)))" || { \
		echo "error: $(OBJECT_BUILD_LABEL) source list contains duplicates" >&2; \
		exit 1; \
	}
	@test -z "$(filter-out $(OBJECT_BUILD_SRCS),$(OBJECT_BUILD_REQUIRED_PRODUCTION_SRCS) $(OBJECT_BUILD_REQUIRED_SUPPORT_SRCS))" && \
		test -z "$(filter-out $(OBJECT_BUILD_REQUIRED_PRODUCTION_SRCS) $(OBJECT_BUILD_REQUIRED_SUPPORT_SRCS),$(OBJECT_BUILD_SRCS))" || { \
		echo "error: $(OBJECT_BUILD_LABEL) must link every required source exactly once" >&2; \
		exit 1; \
	}
	@test -z "$(filter $(OBJECT_BUILD_FORBIDDEN_SRCS),$(OBJECT_BUILD_SRCS))" || { \
		echo "error: $(OBJECT_BUILD_LABEL) links a production source owned by test support" >&2; \
		exit 1; \
	}
	@test -z "$(filter $(OBJECT_BUILD_FORBIDDEN_LDLIBS),$(OBJECT_BUILD_LDLIBS))" || { \
		echo "error: $(OBJECT_BUILD_LABEL) links a library owned by test support" >&2; \
		exit 1; \
	}
	@test "$(words $(OBJECT_BUILD_SRCS))" -eq "$(words $(OBJECT_BUILD_OBJECTS))" && \
		test "$(words $(OBJECT_BUILD_OBJECTS))" -eq "$(words $(sort $(OBJECT_BUILD_OBJECTS)))" && \
		test "$(words $(OBJECT_BUILD_OBJECTS))" -eq "$(words $(OBJECT_BUILD_LINK_OBJECTS))" && \
		test "$(words $(OBJECT_BUILD_LINK_OBJECTS))" -eq "$(words $(sort $(OBJECT_BUILD_LINK_OBJECTS)))" && \
		test -z "$(filter-out $(OBJECT_BUILD_OBJECTS),$(OBJECT_BUILD_LINK_OBJECTS))" && \
		test -z "$(filter-out $(OBJECT_BUILD_LINK_OBJECTS),$(OBJECT_BUILD_OBJECTS))" || { \
		echo "error: $(OBJECT_BUILD_LABEL) source-to-object mapping is not one-to-one" >&2; \
		exit 1; \
	}

test-aur-update-command: check-aur-update-command-link-firewall $(AUR_UPDATE_COMMAND_TEST_TARGET)
	sh tests/test-aur-update-command.sh $(abspath $(AUR_UPDATE_COMMAND_TEST_TARGET))

test-upgrade-all-command: check-upgrade-all-command-link-firewall $(UPGRADE_ALL_COMMAND_TEST_TARGET) $(MO_FILES)
	sh tests/test-upgrade-all-command.sh $(abspath $(UPGRADE_ALL_COMMAND_TEST_TARGET))

check-aur-update-execution-preflight-relation-link-firewall:
	@echo ":: Checking AUR update preflight relation-authority link firewall"
	@test -z "$(filter $(AUR_UPDATE_EXECUTION_PREFLIGHT_FORBIDDEN_RELATION_AUTHORITY_TEST_SRCS),$(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_SRCS))" || { \
		echo "error: AUR update preflight test links relation assessment/query authority" >&2; \
		exit 1; \
	}

test-aur-update-execution-preflight: check-aur-update-execution-preflight-relation-link-firewall $(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET)
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_TEST_TARGET))

test-aur-update-execution-preflight-integration: $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) simple
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) repository-failure
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) aur-failure
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) relation-assessment
	$(abspath $(AUR_UPDATE_EXECUTION_PREFLIGHT_INTEGRATION_TEST_TARGET)) relation-query-failure

test-aur-update-execution-preparation: $(AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET) $(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET)
	$(abspath $(AUR_UPDATE_EXECUTION_PREPARATION_TEST_TARGET))
	XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/tests/aur-update-execution-preparation-fixture/config) \
		HOME=$(abspath $(BUILD_DIR)/tests/aur-update-execution-preparation-fixture/home) \
		$(abspath $(AUR_UPDATE_EXECUTION_PREPARATION_INTEGRATION_TEST_TARGET)) \
		$(abspath $(BUILD_DIR)/tests/aur-update-execution-preparation-fixture/config/moguet/source-build.d)

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

check-cli-diagnostic-model-link-firewall:
	@echo ":: Checking CLI/diagnostic pure-model link firewall"
	@set -e; for source in $(CLI_DIAGNOSTIC_MODEL_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(CLI_DIAGNOSTIC_MODEL_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: CLI/diagnostic model test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(CLI_DIAGNOSTIC_MODEL_FORBIDDEN_TEST_SRCS),$(CLI_DIAGNOSTIC_MODEL_TEST_SRCS))" || { \
		echo "error: CLI/diagnostic model test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(CLI_DIAGNOSTIC_MODEL_TEST_SRCS))" || { \
		echo "error: CLI/diagnostic model test links a test stub" >&2; \
		exit 1; \
	}

test-cli-diagnostic-model: check-cli-diagnostic-model-link-firewall $(CLI_DIAGNOSTIC_MODEL_TEST_TARGET)
	$(abspath $(CLI_DIAGNOSTIC_MODEL_TEST_TARGET))

check-runtime-cli-connection-link-firewall:
	@echo ":: Checking runtime CLI connection link firewall"
	@set -e; for source in $(RUNTIME_CLI_CONNECTION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(RUNTIME_CLI_CONNECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: runtime CLI connection test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(RUNTIME_CLI_CONNECTION_FORBIDDEN_TEST_SRCS),$(RUNTIME_CLI_CONNECTION_TEST_SRCS))" || { \
		echo "error: runtime CLI connection test links an unrelated production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(RUNTIME_CLI_CONNECTION_TEST_SRCS))" || { \
		echo "error: runtime CLI connection test links a test stub" >&2; \
		exit 1; \
	}

test-runtime-cli-connection: check-runtime-cli-connection-link-firewall $(RUNTIME_CLI_CONNECTION_TEST_TARGET)
	$(abspath $(RUNTIME_CLI_CONNECTION_TEST_TARGET))

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

check-unified-plan-observation-link-firewall:
	@echo ":: Checking unified plan observation link firewall"
	@set -e; for source in $(UNIFIED_PLAN_OBSERVATION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(UNIFIED_PLAN_OBSERVATION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: unified plan observation test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(UNIFIED_PLAN_OBSERVATION_FORBIDDEN_TEST_SRCS),$(UNIFIED_PLAN_OBSERVATION_TEST_SRCS))" || { \
		echo "error: unified plan observation test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test -z "$(filter tests/stubs/%,$(UNIFIED_PLAN_OBSERVATION_TEST_SRCS))" || { \
		echo "error: unified plan observation test links a test stub" >&2; \
		exit 1; \
	}

test-unified-plan-observation: check-unified-plan-observation-link-firewall $(UNIFIED_PLAN_OBSERVATION_TEST_TARGET)
	$(abspath $(UNIFIED_PLAN_OBSERVATION_TEST_TARGET))

test-observation-contract-gate: test-unified-plan-observation

UNIFIED_PLAN_PROJECTION_FORBIDDEN_SYMBOL_PATTERN := (^|[^[:alnum:]_])(resolve_|execute_|run_command|capture_command|cmd_|shell_words::|Process::|prepare_production_source_build_invocation|prepare_aur_source_build_work_items|run_explicit_process|capture_explicit_process_output_raw|exec_command|command_status|prepare_artifact_install|prepare_package_base_artifact_install|prepare_smart_source_build_work_item|prepare_resolved_source_build_work_item|argv)
UNIFIED_PLAN_PROJECTION_FIREWALL_PROBE_SYMBOLS := \
	resolve_build_plan \
	execute_source_build_typed \
	shell_words::quote \
	argv \
	run_explicit_process \
	capture_explicit_process_output_raw \
	exec_command \
	command_status \
	prepare_artifact_install \
	prepare_package_base_artifact_install \
	prepare_smart_source_build_work_item \
	prepare_resolved_source_build_work_item

check-unified-plan-projection-link-firewall: $(BUILD_DIR)/unified_plan_projection.o
	@echo ":: Checking unified plan projection link firewall"
	@NM='$(NM)' sh scripts/check-nm-symbol-firewall.sh \
		$(BUILD_DIR)/unified_plan_projection.o \
		'$(UNIFIED_PLAN_PROJECTION_FORBIDDEN_SYMBOL_PATTERN)' \
		'unified plan projection object'
	@set -e; for symbol in $(UNIFIED_PLAN_PROJECTION_FIREWALL_PROBE_SYMBOLS); do \
		printf '                 U %s\n' "$$symbol" | \
			grep -Eq '$(UNIFIED_PLAN_PROJECTION_FORBIDDEN_SYMBOL_PATTERN)' || { \
				echo "error: unified plan projection firewall misses representative symbol $$symbol" >&2; \
				exit 1; \
			}; \
	done
	@set -e; for source in $(UNIFIED_PLAN_PROJECTION_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(UNIFIED_PLAN_PROJECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: unified plan projection test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(UNIFIED_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(UNIFIED_PLAN_PROJECTION_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: unified plan projection test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(UNIFIED_PLAN_PROJECTION_FORBIDDEN_TEST_SRCS),$(UNIFIED_PLAN_PROJECTION_TEST_SRCS))" || { \
		echo "error: unified plan projection test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test "$(words $(filter tests/stubs/%,$(UNIFIED_PLAN_PROJECTION_TEST_SRCS)))" -eq "$(words $(UNIFIED_PLAN_PROJECTION_REQUIRED_TEST_SUPPORT_SRCS))" || { \
		echo "error: unified plan projection test links an unexpected test stub" >&2; \
		exit 1; \
	}

test-unified-plan-projection: check-unified-plan-projection-link-firewall $(UNIFIED_PLAN_PROJECTION_TEST_TARGET)
	$(abspath $(UNIFIED_PLAN_PROJECTION_TEST_TARGET))

test-projection-fixture-gate: test-unified-plan-projection

UNIFIED_PLAN_RENDERER_FORBIDDEN_SYMBOL_PATTERN := (^|[^[:alnum:]_])(resolve_|evaluate_consumer_dependency_requirement|select_provider|make_provider_selection_session|provider_selection_callback|execute_|run_command|capture_command|cmd_|shell_words::|Process::|prepare_production_source_build_invocation|prepare_aur_source_build_work_items|run_explicit_process|capture_explicit_process_output_raw|exec_command|command_status|prepare_artifact_install|prepare_package_base_artifact_install|prepare_smart_source_build_work_item|prepare_resolved_source_build_work_item|argv)
UNIFIED_PLAN_RENDERER_FIREWALL_PROBE_SYMBOLS := \
	resolve_build_plan \
	evaluate_consumer_dependency_requirement \
	select_provider \
	make_provider_selection_session \
	provider_selection_callback \
	execute_source_build_typed \
	shell_words::quote \
	argv \
	run_explicit_process \
	capture_explicit_process_output_raw \
	exec_command \
	command_status \
	prepare_artifact_install \
	prepare_package_base_artifact_install \
	prepare_smart_source_build_work_item \
	prepare_resolved_source_build_work_item

check-unified-plan-renderer-link-firewall: $(BUILD_DIR)/unified_plan_renderer.o
	@echo ":: Checking unified plan renderer link firewall"
	@NM='$(NM)' sh scripts/check-nm-symbol-firewall.sh \
		$(BUILD_DIR)/unified_plan_renderer.o \
		'$(UNIFIED_PLAN_RENDERER_FORBIDDEN_SYMBOL_PATTERN)' \
		'unified plan renderer object'
	@set -e; for symbol in $(UNIFIED_PLAN_RENDERER_FIREWALL_PROBE_SYMBOLS); do \
		printf '                 U %s\n' "$$symbol" | \
			grep -Eq '$(UNIFIED_PLAN_RENDERER_FORBIDDEN_SYMBOL_PATTERN)' || { \
				echo "error: unified plan renderer firewall misses representative symbol $$symbol" >&2; \
				exit 1; \
			}; \
	done
	@set -e; for source in $(UNIFIED_PLAN_RENDERER_ALLOWED_PRODUCTION_TEST_SRCS); do \
		count=$$(printf '%s\n' $(UNIFIED_PLAN_RENDERER_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: unified plan renderer test must link $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@set -e; for source in $(UNIFIED_PLAN_RENDERER_REQUIRED_TEST_SUPPORT_SRCS); do \
		count=$$(printf '%s\n' $(UNIFIED_PLAN_RENDERER_TEST_SRCS) | \
			awk -v expected="$$source" '$$0 == expected { count++ } END { print count + 0 }'); \
		test "$$count" -eq 1 || { \
			echo "error: unified plan renderer test must link support $$source exactly once" >&2; \
			exit 1; \
		}; \
	done
	@test -z "$(filter $(UNIFIED_PLAN_RENDERER_FORBIDDEN_TEST_SRCS),$(UNIFIED_PLAN_RENDERER_TEST_SRCS))" || { \
		echo "error: unified plan renderer test links a forbidden production source" >&2; \
		exit 1; \
	}
	@test "$(words $(filter tests/stubs/%,$(UNIFIED_PLAN_RENDERER_TEST_SRCS)))" -eq "$(words $(UNIFIED_PLAN_RENDERER_REQUIRED_TEST_SUPPORT_SRCS))" || { \
		echo "error: unified plan renderer test links an unexpected test stub" >&2; \
		exit 1; \
	}

test-unified-plan-renderer: check-unified-plan-renderer-link-firewall $(UNIFIED_PLAN_RENDERER_TEST_TARGET)
	LC_ALL=C LANGUAGE= $(abspath $(UNIFIED_PLAN_RENDERER_TEST_TARGET))

test-repository-query: $(REPOSITORY_QUERY_TEST_TARGET)
	@set -e; for test_case in \
		candidate-value-contract \
		configured-order \
		split-package-base \
		confirmed-not-found \
		malformed-package-base \
		returned-child-mismatch \
		present-later-failure \
		absent-later-failure \
		unrelated-malformed-exact \
		provider-capabilities \
		provider-partial-failure \
		repository-named-aur \
		configuration-failure \
		installed-exact-states; do \
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

test-validation-status:
	sh tests/test-validation-status.sh

test-markdown-links:
	sh tests/test-markdown-links.sh \
		$(abspath scripts/check-markdown-links.sh)

test-conflicts-replaces: $(TEST_TARGET)
	sh tests/test-conflicts-replaces.sh $(abspath $(TEST_TARGET))

test-aur-rpc-validation: $(AUR_RPC_VALIDATION_TEST_TARGET) $(AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET)
	sh tests/test-aur-rpc-validation.sh \
		$(abspath $(AUR_RPC_VALIDATION_TEST_TARGET)) \
		$(abspath $(AUR_RPC_ENVELOPE_VALIDATION_TEST_TARGET))

# POLICY(#352): CLI parser/routing/presentationはproduction TUを保ち、strict
# repository authorityだけを既存libalpm stubのcase-local snapshotへ接続する。
test-cli-parser: $(AUR_RPC_VALIDATION_TEST_TARGET)
	sh tests/test-cli-parser.sh $(abspath $(AUR_RPC_VALIDATION_TEST_TARGET))

test-dry-run-command: $(TEST_TARGET) $(AUR_RPC_VALIDATION_TEST_TARGET)
	sh tests/test-dry-run-command.sh \
		$(abspath $(TEST_TARGET)) \
		$(abspath $(AUR_RPC_VALIDATION_TEST_TARGET))

test-completion-schema: scripts/generate_completions.py tests/test-completion-schema-validator.py
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test-completion-schema-validator.py

check-completion-freshness:
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/generate_completions.py --check

test-public-documentation: check-completion-freshness test-completion-schema $(CLI_LOCALIZATION_TEST_TARGET) $(MANPAGES) $(COMPLETION_FILES) $(MO_FILES) tests/test-help-man-completion.sh tests/test-public-documentation-checker.py tests/test-static-completion.sh
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test-public-documentation-checker.py
	sh tests/test-help-man-completion.sh $(abspath $(CLI_LOCALIZATION_TEST_TARGET))
	bash tests/test-static-completion.sh $(abspath $(BASH_COMPLETION))

test-commands-inspect: $(COMMANDS_INSPECT_TEST_TARGET) $(MO_FILES)
	sh tests/test-commands-inspect.sh $(abspath $(COMMANDS_INSPECT_TEST_TARGET))

# POLICY(#406): strict repository discovery in the full CLI and lower
# characterization routes reads the same case-local libalpm snapshot.
test-commands-source-maintenance: $(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET) $(PROCESS_STDIN_FD_TEST_TARGET) $(UPGRADE_BASELINE_METADATA_TEST_TARGET)
	$(abspath $(PROCESS_STDIN_FD_TEST_TARGET))
	sh tests/test-commands-source-maintenance.sh \
		$(abspath $(UPGRADE_BASELINE_METADATA_TEST_TARGET)) \
		$(abspath $(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET)) \
		$(abspath $(UPGRADE_BASELINE_METADATA_TEST_TARGET))

test-commands-sync: check-commands-sync-link-firewall $(COMMANDS_SYNC_TEST_TARGET)
	sh tests/test-commands-sync.sh $(abspath $(COMMANDS_SYNC_TEST_TARGET))

test-fixture-authority:
	sh tests/test-fixture-authority.sh

test-live-contract:
	sh tests/test-live-contract.sh

test-run-with-pty:
	sh tests/test-run-with-pty.sh

test-pacman-routing: $(TEST_TARGET)
	sh tests/test-pacman-routing.sh $(abspath $(TEST_TARGET))

# POLICY(#406): cache safety characterization keeps every production TU while
# strict repository discovery reads a case-local libalpm snapshot.
test-build-cache-symlink: $(AUR_RPC_VALIDATION_TEST_TARGET)
	sh tests/test-build-cache-symlink.sh $(abspath $(AUR_RPC_VALIDATION_TEST_TARGET))

# POLICY(#406): source-build characterization keeps every production TU while
# strict repository discovery reads a case-local libalpm snapshot.
test-source-build: $(AUR_RPC_VALIDATION_TEST_TARGET) $(UPGRADE_BASELINE_METADATA_TEST_TARGET)
	sh tests/test-source-build.sh \
		$(abspath $(AUR_RPC_VALIDATION_TEST_TARGET)) \
		$(abspath $(UPGRADE_BASELINE_METADATA_TEST_TARGET)) \
		$(abspath $(UPGRADE_BASELINE_METADATA_TEST_TARGET))

# POLICY(#352): source-selection CLI characterization keeps every production TU
# while strict repository discovery reads a case-local libalpm snapshot.
test-source-selection: $(AUR_RPC_VALIDATION_TEST_TARGET)
	sh tests/test-source-selection.sh $(abspath $(AUR_RPC_VALIDATION_TEST_TARGET))

test-install-layout: $(TARGET) $(MANPAGES) $(COMPLETION_FILES) $(MO_FILES) $(PROJECT_LICENSE_FILES) $(COMPLIANCE_DOC_FILES) $(PUBLIC_DOC_FILES)
	sh tests/test-install-layout.sh

test-package-transition: $(TARGET) $(MANPAGES) $(COMPLETION_FILES) $(PROJECT_LICENSE_FILES) $(COMPLIANCE_DOC_FILES) $(PUBLIC_DOC_FILES)
	sh tests/test-package-transition.sh

# POLICY(#352): --needed CLI characterization keeps every production TU while
# strict repository discovery reads a case-local libalpm snapshot.
test-needed-contract: $(AUR_RPC_VALIDATION_TEST_TARGET)
	sh tests/test-needed-contract.sh $(abspath $(AUR_RPC_VALIDATION_TEST_TARGET))

test-pkgbuild-export: $(TEST_TARGET)
	sh tests/test-pkgbuild-export.sh $(abspath $(TEST_TARGET))

test-container:
	@set -eu; \
		if ! git -C "$(CURDIR)" rev-parse --verify \
			'refs/tags/v1.16.0^{commit}' >/dev/null 2>&1; then \
			printf '%s\n' \
				'error: local tag v1.16.0 is required for container validation' >&2; \
			exit 1; \
		fi; \
		fixture_dir=$$(mktemp -d); \
		legacy_archive=$$fixture_dir/jpacker-v1.16.0-source.tar; \
		cleanup() { \
			rm -f -- "$$legacy_archive"; \
			rmdir -- "$$fixture_dir"; \
		}; \
		trap cleanup EXIT; \
		trap 'exit 129' HUP; \
		trap 'exit 130' INT; \
		trap 'exit 143' TERM; \
		git -C "$(CURDIR)" archive --format=tar \
			--output="$$legacy_archive" v1.16.0; \
		printf '%s\n' ':: Building Arch validation image'; \
		$(DOCKER) build --pull \
			--build-context "moguet-transition-fixture=$$fixture_dir" \
			--tag "$(ARCH_VALIDATION_IMAGE)" \
			--file containers/arch-validation/Dockerfile \
			.; \
		printf '%s\n' ':: Running Arch validation container'; \
		$(DOCKER) run --rm --network=none "$(ARCH_VALIDATION_IMAGE)"

test-container-live-provider:
	@set -eu; \
		printf '%s\n' ':: Building Arch live provider-validation image'; \
		$(DOCKER) build --pull \
			--tag "$(ARCH_LIVE_VALIDATION_IMAGE)" \
			--file containers/arch-live-validation/Dockerfile \
			.; \
		printf '%s\n' ':: Running Arch live provider-validation container'; \
		$(DOCKER) run --rm "$(ARCH_LIVE_VALIDATION_IMAGE)"

test-container-live-aur:
	@set -eu; \
		printf '%s\n' ':: Building Arch live AUR-validation image'; \
		$(DOCKER) build --pull \
			--tag "$(ARCH_LIVE_AUR_VALIDATION_IMAGE)" \
			--file containers/arch-live-validation/Dockerfile.aur \
			.; \
		printf '%s\n' ':: Running Arch live AUR-validation container'; \
		$(DOCKER) run --rm "$(ARCH_LIVE_AUR_VALIDATION_IMAGE)"

test-container-live-local:
	@set -eu; \
		printf '%s\n' ':: Building Arch live local-PKGBUILD validation image'; \
		$(DOCKER) build --pull \
			--tag "$(ARCH_LIVE_LOCAL_VALIDATION_IMAGE)" \
			--file containers/arch-live-validation/Dockerfile.local \
			.; \
		printf '%s\n' ':: Running Arch live local-PKGBUILD validation container'; \
		$(DOCKER) run --rm "$(ARCH_LIVE_LOCAL_VALIDATION_IMAGE)"

test-container-live:
	+@set -eu; \
		$(MAKE) test-container-live-provider; \
		$(MAKE) test-container-live-aur; \
		$(MAKE) test-container-live-local

test: \
	test-internal-identity \
	test-application-identity \
	test-interactive-confirmation \
	test-localization \
	test-catalog-metadata-gate \
	test-cli-localization-surface \
	test-xdg-paths \
	test-xdg-directory-safety \
	test-xdg-state-log \
	test-trusted-cache \
	test-runtime-identity \
	test-app-config \
	test-provider-selection \
	test-provider-installed-state \
	test-dependency-constraint \
	test-package-relation \
	test-package-relation-observation \
	test-package-relation-assessment \
	test-package-constraint-metadata \
	test-aur-constraint-metadata \
	test-root-package-candidate \
	test-root-package-search \
	test-root-package-selection \
	test-root-package-route-projection \
	test-local-package-metadata \
	test-local-source-root \
	test-local-dependency-plan-projection \
	test-local-source-workspace \
	test-local-source-build \
	test-user-config \
	test-package-identifier \
	test-package-metadata \
	test-package-metadata-integration \
	test-repository-query \
	test-shell-words \
	test-source-environment \
	test-artifact-workspace \
	test-multiple-artifact-workspace \
	test-makepkg-assignment-precedence \
	test-artifact-identity \
	test-multiple-artifact-identity \
	test-artifact-identity-real-pacman \
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
	test-cli-diagnostic-model \
	test-runtime-cli-connection \
	test-dependency-plan-model \
	test-build-plan-artifact-target-projection \
	test-unified-plan-observation \
	test-unified-plan-projection \
	test-unified-plan-renderer \
	test-artifact-install-plan \
	test-package-base-artifact-install-plan \
	test-artifact-selection-model \
	test-artifact-identity-selection \
	test-command-stub-contract \
	test-validation-status \
	test-markdown-links \
	test-aur-rpc-validation \
	test-build-cache-symlink \
	test-cli-parser \
	test-dry-run-command \
	test-public-documentation \
	test-commands-inspect \
	test-commands-source-maintenance \
	test-commands-sync \
	test-fixture-authority \
	test-live-contract \
	test-run-with-pty \
	test-conflicts-replaces \
	test-install-layout \
	test-package-transition \
	test-needed-contract \
	test-pacman-routing \
	test-pkgbuild-export \
	test-source-build \
	test-source-selection

test-host-release: test
	+@$(MAKE) --no-print-directory release-check-exclusive

release-check: check-pot check-catalogs test-localization test-catalog-metadata-gate test-cli-localization-surface test-internal-identity test-application-identity test-xdg-paths test-xdg-directory-safety test-source-environment test-xdg-state-log test-trusted-cache test-runtime-identity test-dry-run-command test-public-documentation test-install-layout test-package-transition test-fixture-authority test-live-contract test-validation-status
	+@$(MAKE) --no-print-directory release-check-exclusive

release-check-exclusive:
	@echo ":: Checking release version consistency"
	sh scripts/check-release-version.sh
	@echo ":: Checking license compliance"
	sh scripts/check-license-compliance.sh
	@echo ":: Checking packaging metadata and payload"
	sh scripts/check-packaging-metadata.sh
	@echo ":: Checking tracked Markdown links"
	sh scripts/check-markdown-links.sh

install: check-localization-config check-completion-freshness $(TARGET) $(MANPAGES) $(COMPLETION_FILES) $(MO_FILES) $(PROJECT_LICENSE_FILES) $(COMPLIANCE_DOC_FILES) $(PUBLIC_DOC_FILES)
	@echo ":: Installing binary..."
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Installing message catalogs..."
	@set -e; for locale in $(LINGUAS); do \
		install -Dm644 \
			"$(LOCALE_BUILD_DIR)/$$locale/LC_MESSAGES/$(GETTEXT_DOMAIN).mo" \
			"$(DESTDIR)$(LOCALEDIR)/$$locale/LC_MESSAGES/$(GETTEXT_DOMAIN).mo"; \
	done

	@echo ":: Installing bash completion..."
	install -Dm644 $(BASH_COMPLETION) $(DESTDIR)$(COMPDIR)/moguet

	@echo ":: Installing zsh completion..."
	install -Dm644 $(ZSH_COMPLETION) $(DESTDIR)$(ZSHCOMPDIR)/_moguet

	@echo ":: Installing fish completion..."
	install -Dm644 $(FISH_COMPLETION) $(DESTDIR)$(FISHCOMPDIR)/moguet.fish

	@echo ":: Installing English and Japanese man pages..."
	install -Dm644 $(MANPAGE_EN) $(DESTDIR)$(MANDIR)/moguet.1
	install -Dm644 $(MANPAGE_JA) $(DESTDIR)$(JAMANDIR)/moguet.1

	@echo ":: Installing license files..."
	install -Dm644 LICENSE $(DESTDIR)$(LICENSEDIR)/LICENSE
	install -Dm644 LICENSES/jpacker-MIT-legacy.txt $(DESTDIR)$(LICENSEDIR)/jpacker-MIT-legacy.txt
	install -Dm644 LICENSES/curl.txt $(DESTDIR)$(LICENSEDIR)/curl.txt
	install -Dm644 LICENSES/nlohmann-json-MIT.txt $(DESTDIR)$(LICENSEDIR)/nlohmann-json-MIT.txt
	install -Dm644 LICENSES/tomlplusplus-MIT.txt $(DESTDIR)$(LICENSEDIR)/tomlplusplus-MIT.txt
	install -Dm644 LICENSES/bjoern-hoehrmann-utf8-MIT.txt $(DESTDIR)$(LICENSEDIR)/bjoern-hoehrmann-utf8-MIT.txt

	@echo ":: Installing documentation..."
	install -Dm644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -Dm644 README.ja.md $(DESTDIR)$(DOCDIR)/README.ja.md
	install -Dm644 THIRD_PARTY_NOTICES.md $(DESTDIR)$(DOCDIR)/THIRD_PARTY_NOTICES.md
	install -Dm644 docs/LICENSING.md $(DESTDIR)$(DOCDIR)/docs/LICENSING.md
	install -Dm644 docs/migration/v1-to-v2.md $(DESTDIR)$(DOCDIR)/docs/migration/v1-to-v2.md
	install -Dm644 docs/migration/v1-to-v2.ja.md $(DESTDIR)$(DOCDIR)/docs/migration/v1-to-v2.ja.md

uninstall: check-localization-config
	@echo ":: Removing binary..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Removing message catalogs..."
	@set -e; for locale in $(LINGUAS); do \
		rm -f "$(DESTDIR)$(LOCALEDIR)/$$locale/LC_MESSAGES/$(GETTEXT_DOMAIN).mo"; \
		rmdir "$(DESTDIR)$(LOCALEDIR)/$$locale/LC_MESSAGES" 2>/dev/null || true; \
		rmdir "$(DESTDIR)$(LOCALEDIR)/$$locale" 2>/dev/null || true; \
	done

	@echo ":: Removing bash completion..."
	rm -f $(DESTDIR)$(COMPDIR)/moguet

	@echo ":: Removing zsh completion..."
	rm -f $(DESTDIR)$(ZSHCOMPDIR)/_moguet

	@echo ":: Removing fish completion..."
	rm -f $(DESTDIR)$(FISHCOMPDIR)/moguet.fish

	@echo ":: Removing English and Japanese man pages..."
	rm -f $(DESTDIR)$(MANDIR)/moguet.1
	rm -f $(DESTDIR)$(JAMANDIR)/moguet.1

	@echo ":: Removing license files..."
	rm -f $(DESTDIR)$(LICENSEDIR)/LICENSE
	rm -f $(DESTDIR)$(LICENSEDIR)/jpacker-MIT-legacy.txt
	rm -f $(DESTDIR)$(LICENSEDIR)/curl.txt
	rm -f $(DESTDIR)$(LICENSEDIR)/nlohmann-json-MIT.txt
	rm -f $(DESTDIR)$(LICENSEDIR)/tomlplusplus-MIT.txt
	rm -f $(DESTDIR)$(LICENSEDIR)/bjoern-hoehrmann-utf8-MIT.txt
	@rmdir $(DESTDIR)$(LICENSEDIR) 2>/dev/null || true

	@echo ":: Removing documentation..."
	rm -f $(DESTDIR)$(DOCDIR)/README.md
	rm -f $(DESTDIR)$(DOCDIR)/README.ja.md
	rm -f $(DESTDIR)$(DOCDIR)/THIRD_PARTY_NOTICES.md
	rm -f $(DESTDIR)$(DOCDIR)/docs/LICENSING.md
	rm -f $(DESTDIR)$(DOCDIR)/docs/migration/v1-to-v2.md
	rm -f $(DESTDIR)$(DOCDIR)/docs/migration/v1-to-v2.ja.md
	@rmdir $(DESTDIR)$(DOCDIR)/docs/migration 2>/dev/null || true
	@rmdir $(DESTDIR)$(DOCDIR)/docs 2>/dev/null || true
	@rmdir $(DESTDIR)$(DOCDIR) 2>/dev/null || true

-include $(DEPS)
