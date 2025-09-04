APP      := blackholesim
CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -I./vendor/include
SRC_DIR  := src
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
OBJS     := $(SRCS:.cpp=.o)
BIN_DIR  := bin
OUT      := $(BIN_DIR)/$(APP)

VENDOR_LIB_DIR := vendor/lib
# Static libs first, then system libs GLFW typically needs on X11
LDLIBS := $(VENDOR_LIB_DIR)/libglfw3.a $(VENDOR_LIB_DIR)/libglad.a \
          -ldl -lpthread -lX11 -lXrandr 

.PHONY: all build run clean

all: build

build: $(OUT)

$(OUT): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: build
	./$(OUT)

clean:
	rm -rf $(BIN_DIR) $(OBJS)
