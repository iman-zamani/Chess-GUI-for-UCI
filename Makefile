# Makefile for building a chess GUI with SFML

# Compiler settings
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

# SFML libraries
LIBS = -lsfml-graphics -lsfml-window -lsfml-system

# Source files and their corresponding object files
SRC = ./src/main.cpp ./src/piece.cpp
OBJ_DIR = ./objectFiles
OBJ = $(SRC:./src/%.cpp=$(OBJ_DIR)/%.o)

# Output executable
TARGET = chess-gui

# Ensure the object files directory exists
$(shell mkdir -p $(OBJ_DIR))

# Build target
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

# Rule to convert each .cpp file into an .o file in the objectFiles directory
$(OBJ_DIR)/%.o: ./src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up
clean:
	rm -f $(OBJ) $(TARGET)

# Phony targets
.PHONY: clean
