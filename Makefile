# --- プロジェクト情報 ---
TARGET    := jpacker
VERSION   := $(strip 0.5.1)# ここは適宜更新
SRC_DIR   := src
BUILD_DIR := build

# --- インストール先設定 ---
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
SYSCONFDIR  ?= /etc
# 【New!】 補完ファイルのパス (Arch Linux標準)
COMPDIR     ?= /usr/share/bash-completion/completions

# ... (コンパイラ設定などは変更なし) ...
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

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(MY_LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $< (v$(VERSION))"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	@echo ":: Installing binary..."
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Installing configs..."
	install -Dm644 jpacker.conf $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf
	install -d $(DESTDIR)$(SYSCONFDIR)/jpacker/package.build

	@echo ":: Installing bash completion..."
	# 【New!】 補完スクリプトのインストール
	install -Dm644 jpacker_completion.bash $(DESTDIR)$(COMPDIR)/jpacker

uninstall:
	@echo ":: Removing binary..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Removing configs..."
	rm -f $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf
	rm -rf $(DESTDIR)$(SYSCONFDIR)/jpacker/package.build
	-rmdir $(DESTDIR)$(SYSCONFDIR)/jpacker

	@echo ":: Removing completion..."
	rm -f $(DESTDIR)$(COMPDIR)/jpacker

-include $(DEPS)
