# --- プロジェクト設定 ---
TARGET   := jpacker
VERSION  := 0.2.0
SRC_DIR  := src
BUILD_DIR:= build

# --- コンパイラ設定 ---
CXX      := g++
# C++17必須, 最適化, 警告, パイプ処理, バージョン定義
CXXFLAGS := -std=c++17 -O2 -pipe -Wall -Wextra -DJPKG_VERSION=\"$(VERSION)\"
# ライブラリリンク (-lcurl は必須)
LDLIBS   := -lcurl
LDFLAGS  :=

# --- インストール先設定 (Arch Linuxのパッケージング標準準拠) ---
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

# --- ソースとオブジェクトの自動検出 ---
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
OBJS     := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS     := $(OBJS:.o=.d)

# --- ターゲット定義 ---
.PHONY: all clean install uninstall debug

all: $(TARGET)

# リンク処理: オジェクトファイルの後に LDLIBS (-lcurl) を置くのが重要
$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# コンパイル処理: .cpp -> .o
# -MMD -MP でヘッダーファイルの依存関係(.d)を自動生成
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $<"
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# デバッグビルド用ターゲット (make debug)
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: all

# クリーンアップ
clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR) $(TARGET)

# インストール (DESTDIR対応)
install: $(TARGET)
	@echo ":: Installing to $(DESTDIR)$(BINDIR)/$(TARGET)"
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

# アンインストール
uninstall:
	@echo ":: Removing from $(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

# 依存関係ファイルの読み込み
-include $(DEPS)
