# ❄️ winter

<p align="center">
  <img src="https://raw.githubusercontent.com/catppuccin/catppuccin latte/main/assets/logo.png" width="100" alt="Winter Logo" />
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C++23-8839ef?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++23" />
  <img src="https://img.shields.io/badge/license-MIT-939ab7?style=for-the-badge&logo=opensourceinit&logoColor=white" alt="MIT License" />
  <img src="https://img.shields.io/badge/build-meson-f5c2e7?style=for-the-badge&logo=meson&logoColor=white" alt="Meson" />
  <img src="https://img.shields.io/badge/status-active-a6e3a1?style=for-the-badge" alt="Active" />
</p>

<p align="center">
  <strong style="color: #cba6f7;">A high-performance, modal TUI editor built for the modern era.</strong><br/>
  <em style="color: #bac2de;">Inspired by vis, Neovim, and the elegance of minimal design.</em>
</p>

---

## ✨ Features

`winter` isn't just another text editor; it's a precision tool for developers who love their terminal.

### 🚀 Core Engine

- **Piece Table Buffer** $\rightarrow$ Blazing fast edits regardless of file size. Memory-efficient insertions and deletions.
- **Double-Buffered Rendering** $\rightarrow$ Minimal terminal escape sequences via cell-diffing for a flicker-free experience.
- **Synchronized Output** $\rightarrow$ Uses DEC mode 2026 to eliminate screen tearing.
- **UTF-8 Native** $\rightarrow$ Full support for wide characters and grapheme boundaries.

### 🧠 Intelligence

- **Tree-sitter Integration** $\rightarrow$ Powerful structural syntax highlighting and indentation for dozens of languages.
- **LSP Support** $\rightarrow$ Full Language Server Protocol implementation for:
  - 🔍 **Goto Definition** & **References**
  - ✏️ **Symbol Rename**
  - ⚠️ **Real-time Diagnostics**
  - 📋 **Smart Completions**
- **Semantic Tokens** $\rightarrow$ Context-aware coloring provided by your LSP.

### 🛠️ Workflow

- **Modal Editing** $\rightarrow$ Efficient navigation and manipulation (Normal, Insert, Visual modes).
- **Quickfix List** $\rightarrow$ Centralized error and search result management.
- **Virtual Text** $\rightarrow$ UI overlays and inline hints for a richer editing experience.
- **Custom Commands** $\rightarrow$ Extensible command-line interface.

---

## 🏗️ Architecture

`winter` is built on a strict three-layer architecture to ensure stability and performance:

1. **Document Model**: The `PieceTable` handles raw text manipulation without knowing about the screen.
2. **Cell Grid**: A virtual grid of characters and styles (`front` vs `back` buffers).
3. **Flush**: The diffing engine that translates cell changes into the most efficient VT100 sequences possible.

---

## 🚀 Getting Started

### Prerequisites

- A C++23 compatible compiler (GCC 10+ or Clang 12+)
- `meson` & `ninja`
- `tree-sitter` libraries

### Installation

```bash
git clone https://github.com/youruser/winter.git
cd winter
meson setup builddir
ninja -C builddir
./builddir/winter
```

---

## 📜 Roadmap

- [x] 📂 File explorer / Project tree
- [ ] 🧩 Plugin system via Lua/WASM
- [~] 🌗 Advanced Theme Engine
- [~] ⌨️ Advanced key-chord support

---

<p align="center">
  Made with ❤️  for the terminal community.
</p>
