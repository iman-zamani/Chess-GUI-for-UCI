# Chess-GUI-for-UCI (Under Development)

Chess-GUI-for-UCI is a graphical user interface designed to interact with UCI-compatible chess engines. This application allows two chess engines to compete against each other or enables a human player to challenge any UCI engine. Please note that this project is currently under active development and may not have all features fully implemented.

![app screenshot](images/chessBoard.png)

---

## Features (Planned and Current)

* **Engine vs. Engine**: Set up matches between two UCI-compatible engines and watch them play.
* **Human vs. Engine**: Test your skills by playing against a variety of chess engines.
* **Customizable Settings**: Adjust basic options such as which engines to load.
* **Cross-Platform**: Intended to run on Windows, macOS, and Linux.

---

## Installation

### Prerequisites

Make sure you have the following installed:

* A **C++17 or newer** compatible compiler

  * **Linux/macOS**: `g++`
  * **Windows**: Microsoft Visual C++ (MSVC)
* [CMake](https://cmake.org/download/)
* [SFML 2.6+](https://www.sfml-dev.org/) built for your compiler

---

### Linux / macOS

1. Clone the repository:

   ```bash
   git clone https://github.com/iman-zamani/Chess-GUI-for-UCI.git
   cd Chess-GUI-for-UCI
   cd build
   ```

2. Generate and build:

   ```bash
   cmake ..
   cmake --build .
   ```

3. Ensure the `Resources` folder is inside the `build` directory before running the app.

---

### Windows (Using MSVC)

1. Install **CMake**, **SFML for MSVC**, and **Visual Studio** or **Build Tools** with MSVC support.

2. Open the **x64 Native Tools Command Prompt for VS**.

3. Clone the repository and navigate to the build directory:

   ```bat
   git clone https://github.com/iman-zamani/Chess-GUI-for-UCI.git
   cd Chess-GUI-for-UCI\build
   ```

4. Generate and build the project:

   ```bat
   cmake ..
   cmake --build .
   ```

5. **Important – Resources Folder Placement**:
   On Windows, the binary will be located in either the `build\Debug\` or `build\Release\` folder depending on your build configuration.
   You **must move or copy the `Resources` folder into the same folder as the `.exe`**:

   ```
   Chess-GUI-for-UCI\
   ├── build\
   │   ├── Debug\
   │   │   ├── chess-gui.exe
   │   │   ├── Resources\
   ```

---

## License

This project is licensed under a custom MIT-style license with an attribution requirement.
See [CUSTOM\_LICENSE](./LICENSE) for full terms.

