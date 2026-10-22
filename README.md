# ❄️ winter

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-orange.svg)](https://www.kernel.org/)

**winter** is a lightweight, modal TUI text editor written in C++20. It's designed with a focus on efficiency, utilizing a piece table for text manipulation and a double-buffered renderer for flicker-free terminal output.

## ✨ Features

- ⌨️ **Modal Editing**: Inspired by `nvim` and `vis`.
- 🧩 **Piece Table**: High-performance buffer management for large files.
- ⚡ **Double Buffering**: Minimal terminal escape sequences and smooth redraws.
- 🌙 **Lua Integration**: Extensible API for configuration and custom behavior.
- 🔍 **Tree-sitter Support**: Robust syntax highlighting via `tree-sitter`.
- 🛠️ **Vim Motions**: Familiar navigation and editing commands.
- 💾 **Undo/Redo**: Robust history management.

## 🚀 Getting Started

### Prerequisites

- A C++20 compatible compiler (GCC 10+ or Clang 10+)
- `meson` and `ninja` build system
- `lua` development headers

### Installation

```bash
# Clone the repository
git clone https://github.com/yourusername/winter.git
cd winter

# Setup and build
meson setup builddir
ninja -C builddir

# Run winter
./builddir/winter <file_to_edit>
```

## ⌨️ Quick Start

| Command | Action |
| :--- | :--- |
| `CTRL-S` or `:w` | Save file |
| `CTRL-Q` or `:q` | Quit editor |
| `/` | Search |

## 🛠️ Architecture

`winter` is built on three primary layers:
1. **Document Model**: A piece table that handles the raw text without knowing about the screen.
2. **Cell Grid**: A back and front grid of cells used for rendering.
3. **Flush**: A diffing engine that compares the grids and emits minimal ANSI escape sequences.

## 📄 License

This project is licensed under the MIT License.

---
*Made with ❄️ and C++*
