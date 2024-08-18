# Makefile for building a chess GUI with SFML

# Compiler settings
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

# SFML libraries
LIBS = -lsfml-graphics -lsfml-window -lsfml-system

# Source files and their corresponding object files
SRC = ./src/main.cpp ./src/piece.cpp
OBJ = $(SRC:.cpp=.o)

# Output executable
TARGET = chess-gui

# Build target
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

# Rule to convert each .cpp file into an .o file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up
clean:
	rm -f $(OBJ) $(TARGET)

# Phony targets
.PHONY: clean
