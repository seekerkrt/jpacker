# --- プロジェクト情報 ---
TARGET    := jpacker
VERSION   := 0.2.0
SRC_DIR   := src
BUILD_DIR := build

# --- インストール先設定 (GNU標準準拠) ---
# makepkg等は PREFIX=/usr をセットして呼び出します
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
SYSCONFDIR  ?= /etc
DATADIR     ?= $(PREFIX)/share

# --- コンパイラ設定 ---
CXX       ?= g++

# 1. ユーザー/システム指定のフラグ (makepkg.confの内容) を尊重
#    未指定の場合のみデフォルト値 (-O2 -pipe) を使用
CXXFLAGS  ?= -O2 -pipe
LDFLAGS   ?=
CPPFLAGS  ?=

# 2. プロジェクトに必須のフラグを追加
#    警告オプション、C++バージョン、バージョン定義
#    システムのCFLAGSを上書きせず、後ろに追加する形にします
MY_CXXFLAGS := -std=c++20 -Wall -Wextra -DJPKG_VERSION=\"$(VERSION)\"
MY_LDLIBS   := -lcurl

# --- ソースとオブジェクトの自動検出 ---
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)

# --- ターゲット定義 ---
.PHONY: all clean install uninstall

all: $(TARGET)

# リンク: LDFLAGS (システム) を先に、LDLIBS (ライブラリ) を後に置くのが作法
$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(MY_LDLIBS)

# コンパイル: CPPFLAGS (プリプロセッサ) と CXXFLAGS (システム) を含める
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $< (v$(VERSION))"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MY_CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	@echo ":: Installing binary to $(DESTDIR)$(BINDIR)/$(TARGET)"
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Installing config to $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf"
	install -Dm644 jpacker.conf $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf

uninstall:
	@echo ":: Removing binary"
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

	@echo ":: Removing config"
	rm -f $(DESTDIR)$(SYSCONFDIR)/jpacker/jpacker.conf
	-rmdir $(DESTDIR)$(SYSCONFDIR)/jpacker

# 依存関係ファイルの読み込み
-include $(DEPS)
