APP      := blackholesim
CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra \
            -I./vendor/include \
            -I./vendor/include/imgui \
            -I./vendor/include/nlohmann
SRC_DIR  := src
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)

# ImGui sources (explicit list to avoid picking up imgui_demo)
IMGUI_SRC_DIR := vendor/src/imgui
IMGUI_SRCS := \
  $(IMGUI_SRC_DIR)/imgui.cpp \
  $(IMGUI_SRC_DIR)/imgui_draw.cpp \
  $(IMGUI_SRC_DIR)/imgui_tables.cpp \
  $(IMGUI_SRC_DIR)/imgui_widgets.cpp \
  $(IMGUI_SRC_DIR)/imgui_impl_glfw.cpp \
  $(IMGUI_SRC_DIR)/imgui_impl_opengl3.cpp

ALL_SRCS := $(SRCS) $(IMGUI_SRCS)
OBJS     := $(ALL_SRCS:.cpp=.o)
BIN_DIR  := bin
OUT      := $(BIN_DIR)/$(APP)

VENDOR_LIB_DIR := vendor/lib
# Static libs first, then system libs GLFW typically needs on X11
LDLIBS := $(VENDOR_LIB_DIR)/libglfw3.a $(VENDOR_LIB_DIR)/libglad.a \
          -ldl -lpthread -lX11 -lXrandr 

.PHONY: all build debug run clean

all: build

# Normal (optimized) build
build: CXXFLAGS += -O2
build: $(OUT)

# Debug build (symbols + no optimizations)
debug: CXXFLAGS += -g -O0
debug: $(OUT)

$(OUT): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: build
	./$(OUT)

clean:
	rm -rf $(BIN_DIR) $(OBJS) $(IMGUI_SRC_DIR)/*.o
