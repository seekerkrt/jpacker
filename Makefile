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
MY_CXXFLAGS := -std=c++20 -Wall -Wextra -DJPKG_VERSION=\"$(VERSION)\"
MY_LDLIBS   := -lcurl
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
HEADERS   := $(wildcard $(SRC_DIR)/*.hpp)
OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)

.PHONY: all clean test-aur-rpc-validation test-build-cache-symlink test-cli-parser test-conflicts-replaces test-needed-contract test-pacman-routing test-pkgbuild-export test-source-selection release-check install uninstall

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

test-conflicts-replaces: $(TEST_TARGET)
	sh tests/test-conflicts-replaces.sh $(abspath $(TEST_TARGET))

test-aur-rpc-validation: $(TEST_TARGET)
	sh tests/test-aur-rpc-validation.sh $(abspath $(TEST_TARGET))

test-cli-parser: $(TEST_TARGET)
	sh tests/test-cli-parser.sh $(abspath $(TEST_TARGET))

test-pacman-routing: $(TEST_TARGET)
	sh tests/test-pacman-routing.sh $(abspath $(TEST_TARGET))

test-build-cache-symlink: $(TEST_TARGET)
	sh tests/test-build-cache-symlink.sh $(abspath $(TEST_TARGET))

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
