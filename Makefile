# --- プロジェクト情報 ---
TARGET    := jpacker
VERSION_FILE := VERSION
VERSION   := $(strip $(shell cat $(VERSION_FILE) 2>/dev/null))
ifeq ($(VERSION),)
VERSION   := unknown
endif
SRC_DIR   := src
BUILD_DIR := build

# --- インストール先設定 ---
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
SYSCONFDIR  ?= /etc
COMPDIR     ?= /usr/share/bash-completion/completions
ZSHCOMPDIR  ?= /usr/share/zsh/site-functions
MANDIR      ?= $(PREFIX)/share/man/man8

# --- コンパイラ設定 ---
CXX       ?= g++
CXXFLAGS  ?= -O2 -pipe
LDFLAGS   ?=
CPPFLAGS  ?=
MY_CXXFLAGS := -std=c++20 -Wall -Wextra -DJPKG_VERSION=\"$(VERSION)\"
MY_LDLIBS   := -lcurl
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)

.PHONY: all clean install uninstall

all: $(TARGET) jpacker.8

$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(MY_LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(VERSION_FILE)
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $< (v$(VERSION))"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -MMD -MP -c $< -o $@

jpacker.8: jpacker.8.in $(VERSION_FILE)
	@echo ":: Generating $@ (v$(VERSION))"
	sed 's/@VERSION@/$(VERSION)/g' jpacker.8.in > $@

clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET) jpacker.8
	@echo ":: Installing binary..."
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Installing configs..."
	install -Dm644 jpacker.conf $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf
	install -d $(DESTDIR)$(SYSCONFDIR)/jpacker/package.build

	@echo ":: Installing bash completion..."
	install -Dm644 jpacker_completion.bash $(DESTDIR)$(COMPDIR)/jpacker

	@echo ":: Installing zsh completion..."
	install -Dm644 _jpacker $(DESTDIR)$(ZSHCOMPDIR)/_jpacker

	@echo ":: Installing man page..."
	install -Dm644 jpacker.8 $(DESTDIR)$(MANDIR)/jpacker.8

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

	@echo ":: Removing man page..."
	rm -f $(DESTDIR)$(MANDIR)/jpacker.8

-include $(DEPS)
