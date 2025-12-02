# --- プロジェクト設定 ---
TARGET   := jpacker
VERSION  := 0.2.0
SRC_DIR  := src
BUILD_DIR:= build

# --- コンパイラ設定 ---
CXX      := g++
# -DJPKG_VERSION=\"$(VERSION)\" を追加してソースコードに渡す
CXXFLAGS := -std=c++20 -O2 -pipe -Wall -Wextra -DJPKG_VERSION=\"$(VERSION)\"
# ライブラリリンク
LDLIBS   := -lcurl
LDFLAGS  :=

# --- インストール先設定 ---
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

# --- ソースとオブジェクトの自動検出 ---
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
OBJS     := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS     := $(OBJS:.o=.d)

# --- ターゲット定義 ---
.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo ":: Linking $@"
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@echo ":: Compiling $< (v$(VERSION))"
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

debug: CXXFLAGS += -g -O0 -DDEBUG
debug: all

clean:
	@echo ":: Cleaning up"
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	@echo ":: Installing to $(DESTDIR)$(BINDIR)/$(TARGET)"
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	@echo ":: Removing from $(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

-include $(DEPS)
