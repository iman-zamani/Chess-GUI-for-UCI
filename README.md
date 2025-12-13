# Chess-GUI-for-UCI (Under Development)
![app screenshot](images/chessBoard.png)
**Chess-GUI-for-UCI** is a graphical interface for **UCI-compatible chess engines**.
It supports **engine vs engine** matches and **human vs engine** play.

---

## Prebuilt Binaries available for:

* macOS (Apple Silicon / ARM)
* macOS (Intel)
* Windows
* Linux AppImage
* Linux `.deb`
* Linux `.rpm`
---

## Features

* Engine vs Engine matches (UCI)
* Human vs Engine play
* Load and switch UCI-compatible engines
* Cross-platform support (Windows, macOS, Linux)

---

### Requirements (Building From Source)

* C++17 or newer compiler
* CMake
* SFML 2.6 or newer

---

### All Platforms (Windows, macOS, Linux)

The `build/` directory already exists in the repository.

```bash
git clone https://github.com/iman-zamani/Chess-GUI-for-UCI.git
cd Chess-GUI-for-UCI/build
cmake ..
cmake --build .
```

Ensure the `Resources` folder is located **next to the executable** before running.

---
## License

This project is licensed under the GNU General Public License v2.0.
See [`LICENSE`](./LICENSE) for full terms.

