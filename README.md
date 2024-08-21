# Chess-GUI-for-UCI (Under Development)

Chess-GUI-for-UCI is a graphical user interface designed to interact with UCI-compatible chess engines. This application allows two chess engines to compete against each other or enables a human player to challenge any UCI engine. Please note that this project is currently under active development and may not have all features fully implemented.

## Features (Planned and Current)

- **Engine vs. Engine**: Set up matches between two UCI-compatible engines and watch them play.
- **Human vs. Engine**: Test your skills by playing against a variety of chess engines.
- **Customizable Settings**: Adjust basic options such as which engines to load.
- **Cross-Platform**: Intended to run on Windows, macOS, and Linux.

## Installation

To get started with Chess-GUI-for-UCI, follow these installation steps:

### Prerequisites

- Ensure you have SFML 2.6 or newer, g++, and CMake installed on your system.

### Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/iman-zamani/Chess-GUI-for-UCI.git
   ```
2. Navigate to the cloned directory:
   ```bash
   cd Chess-GUI-for-UCI
   ```
3. Create and navigate to the build directory (the resources folder is located here, so you need to be in this directory):
   ```bash
   mkdir build
   cd build
   ```
4. Generate the makefile and build the project:
   ```bash
   cmake ..
   cmake --build .
   ```

## Usage

To launch the Chess-GUI-for-UCI, run the following command from the build directory:
```bash
./chess-gui
```

## Contributing

Contributions to Chess-GUI-for-UCI are welcome! Here are a few ways you can help:
- Report bugs and request features in the Issues section.
- Submit pull requests with bug fixes or new features.

## License

Chess-GUI-for-UCI is open-source software licensed under the MIT License. See the [LICENSE](LICENSE) file for more details.

