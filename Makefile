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
MY_CXXFLAGS := -std=c++20 -Wall -Wextra -DJPACKER_VERSION=\"$(VERSION)\"
MY_LDLIBS   := -lcurl
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
HEADERS   := $(wildcard $(SRC_DIR)/*.hpp)
COMMANDS_INSPECT_TEST_SRCS := $(filter-out $(SRC_DIR)/aur_rpc.cpp,$(SRCS)) tests/commands_inspect_aur_stub.cpp
COMMANDS_SYNC_TEST_SRCS := $(filter-out $(SRC_DIR)/aur_rpc.cpp,$(SRCS)) tests/stubs/commands-sync/aur_rpc_stub.cpp
SOURCE_INSTALL_CHARACTERIZATION_TEST_SRCS := $(filter-out $(SRC_DIR)/jpacker.cpp,$(SRCS))
OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)

.PHONY: all clean test-app-config test-package-identifier test-shell-words test-aur-rpc-validation test-build-cache-symlink test-cli-parser test-commands-inspect test-commands-source-maintenance test-commands-sync test-conflicts-replaces test-needed-contract test-pacman-routing test-pkgbuild-export test-source-build test-source-selection release-check install uninstall

all: $(TARGET) $(MANPAGE)

$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(MY_LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(VERSION_FILE)
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $< (v$(VERSION))"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -MMD -MP -c $< -o $@

$(MANPAGE): $(MANPAGE_IN) $(VERSION_FILE)
	@echo ":: Generating $@ (v$(VERSION))"
	sed 's/@VERSION@/$(VERSION)/g' $(MANPAGE_IN) > $@

clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR) $(TARGET)

$(TEST_TARGET): $(SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling isolated integration test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES $(SRCS) -o $@ $(MY_LDLIBS)

$(COMMANDS_INSPECT_TEST_TARGET): $(COMMANDS_INSPECT_TEST_SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling command inspection characterization test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -I$(SRC_DIR) $(COMMANDS_INSPECT_TEST_SRCS) -o $@ $(MY_LDLIBS)

$(COMMANDS_SYNC_TEST_TARGET): $(COMMANDS_SYNC_TEST_SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling sync command characterization test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -DJPACKER_ENABLE_TEST_CONFIG_PATH -I$(SRC_DIR) $(COMMANDS_SYNC_TEST_SRCS) -o $@ $(MY_LDLIBS)

$(SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET): tests/source_install_characterization.cpp $(SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling shared source-install characterization test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -I$(SRC_DIR) tests/source_install_characterization.cpp $(SOURCE_INSTALL_CHARACTERIZATION_TEST_SRCS) -o $@ $(MY_LDLIBS)

$(APP_CONFIG_MODULE_TEST_TARGET): tests/app_config_test.cpp $(SRC_DIR)/app_config.cpp $(SRC_DIR)/app_config.hpp $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling app config module test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -I$(SRC_DIR) tests/app_config_test.cpp $(SRC_DIR)/app_config.cpp -o $@

$(APP_CONFIG_INTEGRATION_TEST_TARGET): $(SRCS) $(HEADERS) $(VERSION_FILE)
	@mkdir -p $(dir $@)
	@echo ":: Compiling app config integration test binary"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -DJPACKER_ENABLE_TEST_OVERRIDES -DJPACKER_ENABLE_TEST_CONFIG_PATH -DJPACKER_ENABLE_APP_CONFIG_TEST_HOOKS $(SRCS) -o $@ $(MY_LDLIBS)

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

test-app-config: $(APP_CONFIG_MODULE_TEST_TARGET) $(APP_CONFIG_INTEGRATION_TEST_TARGET)
	sh tests/test-app-config.sh $(abspath $(APP_CONFIG_MODULE_TEST_TARGET)) $(abspath $(APP_CONFIG_INTEGRATION_TEST_TARGET))

test-package-identifier: $(PACKAGE_IDENTIFIER_TEST_TARGET)
	$(abspath $(PACKAGE_IDENTIFIER_TEST_TARGET))

test-shell-words: $(SHELL_WORDS_TEST_TARGET)
	$(abspath $(SHELL_WORDS_TEST_TARGET))

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
