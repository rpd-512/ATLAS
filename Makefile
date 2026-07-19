# ATLAS Makefile

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
INCLUDES := -Iincludes
TARGET   := atlas
SRC      := main.cpp

.PHONY: all clean debug run

all: $(TARGET)

$(TARGET): $(SRC) includes/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) -o $(TARGET)

debug: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET) $(ARGS)

clean:
	rm -f $(TARGET) *.rpt *.log