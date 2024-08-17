# Makefile for building a chess GUI with SFML

# Compiler settings
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

# SFML libraries
LIBS = -lsfml-graphics -lsfml-window -lsfml-system

# Source files
SRC = ./src/main.cpp

# Output executable
TARGET = chess-gui

# Build target
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

# Clean up
clean:
	rm -f $(TARGET)

# Phony targets
.PHONY: clean
