# ATLAS Makefile

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
TARGET   := atlas
SRC      := main.cpp

# Eigen: system install by default. Override with:
#   make EIGEN_DIR=third_party/eigen
# (Using := instead of ?= so a stray EIGEN_DIR env var can't silently
#  override this with an empty value -- command-line overrides still work.)
EIGEN_DIR := /usr/include/eigen3

INCLUDES := -Iincludes -I$(EIGEN_DIR)

.PHONY: all debug clean

all: $(TARGET)

$(TARGET): $(SRC) $(wildcard includes/*.h)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) -o $(TARGET)

debug: CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra
debug: $(TARGET)

clean:
	rm -f $(TARGET)