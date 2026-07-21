# --- プロジェクト情報 ---
TARGET    := jpacker
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
COMMANDS_INSPECT_TEST_TARGET := build/tests/jpacker-commands-inspect-test
COMMANDS_SYNC_TEST_TARGET := build/tests/jpacker-commands-sync-test
SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET := build/tests/jpacker-source-install-characterization-test
APP_CONFIG_MODULE_TEST_TARGET := build/tests/app-config-test
APP_CONFIG_INTEGRATION_TEST_TARGET := build/tests/jpacker-app-config-test
PACKAGE_IDENTIFIER_TEST_TARGET := build/tests/package-identifier-test
SHELL_WORDS_TEST_TARGET := build/tests/shell-words-test
PROCESS_STDIN_FD_TEST_TARGET := build/tests/process-stdin-fd-test
DEPENDENCY_PLAN_MODEL_TEST_TARGET := $(BUILD_DIR)/tests/dependency-plan-model-test
ARTIFACT_INSTALL_PLAN_TEST_TARGET := $(BUILD_DIR)/tests/artifact-install-plan-test
PACKAGE_METADATA_TEST_TARGET := $(BUILD_DIR)/tests/package-metadata-test
PACKAGE_METADATA_INTEGRATION_TEST_TARGET := $(BUILD_DIR)/tests/package-metadata-integration-test

# --- インストール先設定 ---
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
SYSCONFDIR  ?= /etc
COMPDIR     ?= /usr/share/bash-completion/completions
ZSHCOMPDIR  ?= /usr/share/zsh/site-functions
FISHCOMPDIR ?= /usr/share/fish/vendor_completions.d
MANDIR      ?= $(PREFIX)/share/man/man8

# --- コンパイラ設定 ---
CXX       ?= g++
CXXFLAGS  ?= -O2 -pipe
LDFLAGS   ?=
CPPFLAGS  ?=
PKG_CONFIG ?= pkg-config
LIBALPM_CPPFLAGS = $(shell $(PKG_CONFIG) --cflags libalpm)
LIBALPM_LDLIBS   = $(shell $(PKG_CONFIG) --libs libalpm)
MY_CXXFLAGS := -std=c++20 -Wall -Wextra -DJPACKER_VERSION=\"$(VERSION)\"
MY_LDLIBS   := -lcurl
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
HEADERS   := $(wildcard $(SRC_DIR)/*.hpp)
COMMANDS_INSPECT_TEST_SRCS := $(filter-out $(SRC_DIR)/aur_rpc.cpp,$(SRCS)) tests/commands_inspect_aur_stub.cpp
COMMANDS_SYNC_TEST_SRCS := $(filter-out $(SRC_DIR)/aur_rpc.cpp,$(SRCS)) tests/stubs/commands-sync/aur_rpc_stub.cpp
SOURCE_INSTALL_CHARACTERIZATION_TEST_SRCS := $(filter-out $(SRC_DIR)/jpacker.cpp,$(SRCS))
DEPENDENCY_PLAN_MODEL_TEST_SRCS := \
	tests/dependency_plan_model_test.cpp \
	$(SRC_DIR)/dependency_plan.cpp \
	$(SRC_DIR)/dependency_spec.cpp \
	$(SRC_DIR)/package_identifier.cpp \
	$(SRC_DIR)/logging.cpp \
	tests/stubs/dependency-plan/aur_rpc_stub.cpp \
	tests/stubs/dependency-plan/repository_query_stub.cpp
ARTIFACT_INSTALL_PLAN_TEST_SRCS := \
	tests/artifact_install_plan_test.cpp \
	$(SRC_DIR)/artifact_install_plan.cpp
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
OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)
LIBALPM_BUILD_TARGETS := \
	$(TARGET) \
	$(TEST_TARGET) \
	$(COMMANDS_INSPECT_TEST_TARGET) \
	$(COMMANDS_SYNC_TEST_TARGET) \
	$(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET) \
	$(APP_CONFIG_INTEGRATION_TEST_TARGET) \
	$(PACKAGE_METADATA_TEST_TARGET) \
	$(PACKAGE_METADATA_INTEGRATION_TEST_TARGET)

.PHONY: all check-libalpm clean test-app-config test-package-identifier test-package-metadata test-package-metadata-integration test-shell-words test-dependency-plan-model test-artifact-install-plan test-aur-rpc-validation test-build-cache-symlink test-cli-parser test-commands-inspect test-commands-source-maintenance test-commands-sync test-conflicts-replaces test-needed-contract test-pacman-routing test-pkgbuild-export test-source-build test-source-selection release-check install uninstall

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
	rm -rf $(BUILD_DIR) $(TARGET)

$(TEST_TARGET): $(SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling isolated integration test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES $(SRCS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

$(COMMANDS_INSPECT_TEST_TARGET): $(COMMANDS_INSPECT_TEST_SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling command inspection characterization test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -I$(SRC_DIR) $(COMMANDS_INSPECT_TEST_SRCS) -o $@ $(MY_LDLIBS) $(LIBALPM_LDLIBS)

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

$(PROCESS_STDIN_FD_TEST_TARGET): tests/process_stdin_fd_test.cpp $(SRC_DIR)/process.cpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.cpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling process stdin fd test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) tests/process_stdin_fd_test.cpp $(SRC_DIR)/process.cpp $(SRC_DIR)/logging.cpp -o $@

$(DEPENDENCY_PLAN_MODEL_TEST_TARGET): $(DEPENDENCY_PLAN_MODEL_TEST_SRCS) $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(SRC_DIR)/dependency_spec.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling dependency plan model test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(DEPENDENCY_PLAN_MODEL_TEST_SRCS) -o $@

$(ARTIFACT_INSTALL_PLAN_TEST_TARGET): $(ARTIFACT_INSTALL_PLAN_TEST_SRCS) $(SRC_DIR)/artifact_install_plan.hpp $(SRC_DIR)/dependency_plan.hpp $(SRC_DIR)/aur_rpc.hpp $(SRC_DIR)/repository_query.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling artifact install plan test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) $(ARTIFACT_INSTALL_PLAN_TEST_SRCS) -o $@

$(PACKAGE_METADATA_TEST_TARGET): $(PACKAGE_METADATA_TEST_SRCS) $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp tests/stubs/package-metadata/alpm_stub.hpp tests/stubs/package-metadata/process_stub.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package metadata fake-symbol test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) -Itests/stubs/package-metadata \
		$(PACKAGE_METADATA_TEST_SRCS) \
		-o $@

$(PACKAGE_METADATA_INTEGRATION_TEST_TARGET): $(PACKAGE_METADATA_INTEGRATION_TEST_SRCS) $(SRC_DIR)/package_metadata.hpp $(SRC_DIR)/package_identifier.hpp $(SRC_DIR)/process.hpp $(SRC_DIR)/logging.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling package metadata integration test binary"
	$(CXX) $(CPPFLAGS) $(LIBALPM_CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) \
		-I$(SRC_DIR) \
		$(PACKAGE_METADATA_INTEGRATION_TEST_SRCS) \
		-o $@ $(LIBALPM_LDLIBS)

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

test-dependency-plan-model: $(DEPENDENCY_PLAN_MODEL_TEST_TARGET)
	$(abspath $(DEPENDENCY_PLAN_MODEL_TEST_TARGET))

test-artifact-install-plan: $(ARTIFACT_INSTALL_PLAN_TEST_TARGET)
	$(abspath $(ARTIFACT_INSTALL_PLAN_TEST_TARGET))

test-conflicts-replaces: $(TEST_TARGET)
	sh tests/test-conflicts-replaces.sh $(abspath $(TEST_TARGET))

test-aur-rpc-validation: $(TEST_TARGET)
	sh tests/test-aur-rpc-validation.sh $(abspath $(TEST_TARGET))

test-cli-parser: $(TEST_TARGET)
	sh tests/test-cli-parser.sh $(abspath $(TEST_TARGET))

test-commands-inspect: $(COMMANDS_INSPECT_TEST_TARGET)
	sh tests/test-commands-inspect.sh $(abspath $(COMMANDS_INSPECT_TEST_TARGET))

test-commands-source-maintenance: $(APP_CONFIG_INTEGRATION_TEST_TARGET) $(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET) $(PROCESS_STDIN_FD_TEST_TARGET)
	$(abspath $(PROCESS_STDIN_FD_TEST_TARGET))
	sh tests/test-commands-source-maintenance.sh $(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET)) $(abspath $(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET))

test-commands-sync: $(COMMANDS_SYNC_TEST_TARGET)
	sh tests/test-commands-sync.sh $(abspath $(COMMANDS_SYNC_TEST_TARGET))

test-pacman-routing: $(TEST_TARGET)
	sh tests/test-pacman-routing.sh $(abspath $(TEST_TARGET))

test-build-cache-symlink: $(TEST_TARGET)
	sh tests/test-build-cache-symlink.sh $(abspath $(TEST_TARGET))

test-source-build: $(TEST_TARGET) $(APP_CONFIG_INTEGRATION_TEST_TARGET)
	sh tests/test-source-build.sh \
		$(abspath $(TEST_TARGET)) \
		$(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET))

test-source-selection: $(TEST_TARGET)
	sh tests/test-source-selection.sh $(abspath $(TEST_TARGET))

test-needed-contract: $(TEST_TARGET)
	sh tests/test-needed-contract.sh $(abspath $(TEST_TARGET))

test-pkgbuild-export: $(TEST_TARGET)
	sh tests/test-pkgbuild-export.sh $(abspath $(TEST_TARGET))

release-check:
	@echo ":: Checking release version consistency"
	sh scripts/check-release-version.sh

install: $(TARGET) $(MANPAGE)
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

uninstall:
	@echo ":: Removing binary..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Removing configs..."
	rm -f $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf
	rm -rf $(DESTDIR)$(SYSCONFDIR)/jpacker/package.build
	-rmdir $(DESTDIR)$(SYSCONFDIR)/jpacker

	@echo ":: Removing bash completion..."
	rm -f $(DESTDIR)$(COMPDIR)/jpacker

	@echo ":: Removing zsh completion..."
	rm -f $(DESTDIR)$(ZSHCOMPDIR)/_jpacker

	@echo ":: Removing fish completion..."
	rm -f $(DESTDIR)$(FISHCOMPDIR)/jpacker.fish

	@echo ":: Removing man page..."
	rm -f $(DESTDIR)$(MANDIR)/jpacker.8

-include $(DEPS)
