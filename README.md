# AZ Editor

A minimal, fast terminal text editor for Windows with intuitive Vim-like motions.

![Version](https://img.shields.io/badge/version-1.1-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)

## ✨ Features

- **🎨 Dark Theme** - Easy on the eyes with syntax highlighting for line numbers
- **📁 Directory Sidebar** - Browse and open files without leaving the editor (Tab)
- **🖱️ Mouse Support** - Click to position cursor, drag to select text, scroll with wheel
- **⌨️ Vim-like Motions** - Familiar keybindings for efficient editing
- **📝 Multiple Modes** - Normal, Insert, Command, Search, and Browse modes
- **↩️ Multi-level Undo** - Up to 100 undo states
- **🚀 No Dependencies** - Pure Windows Console API, no external libraries

## 📥 Installation

### Pre-built Binary

Download `az.exe` from the [Releases](../../releases) page.

### Build from Source

```cmd
gcc az.c -o az.exe -O2
```

Requirements: MinGW-w64 or any GCC compiler for Windows.

## 🚀 Usage

```cmd
az [filename]           # Open file or start new
az --help              # Show help
az --version           # Show version
```

## ⌨️ Keybindings

### Modes

| Key   | Action                       |
| ----- | ---------------------------- |
| `i`   | Enter Insert mode            |
| `Esc` | Return to Normal mode        |
| `:`   | Enter Command mode           |
| `/`   | Enter Search mode            |
| `Tab` | Toggle sidebar (Browse mode) |

### Navigation (Normal Mode)

| Key        | Action           |
| ---------- | ---------------- |
| `h` `←`    | Move left        |
| `j` `↓`    | Move down        |
| `k` `↑`    | Move up          |
| `l` `→`    | Move right       |
| `w`        | Next word        |
| `b`        | Previous word    |
| `0` `Home` | Line start       |
| `$` `End`  | Line end         |
| `gg`       | Go to first line |
| `G`        | Go to last line  |
| `PgUp`     | Page up          |
| `PgDn`     | Page down        |

### Editing

| Key  | Action               |
| ---- | -------------------- |
| `i`  | Insert before cursor |
| `a`  | Insert after cursor  |
| `A`  | Insert at line end   |
| `I`  | Insert at line start |
| `o`  | New line below       |
| `O`  | New line above       |
| `x`  | Delete character     |
| `dd` | Delete (cut) line    |
| `yy` | Yank (copy) line     |
| `p`  | Paste below          |
| `u`  | Undo                 |

### Commands

| Command       | Action                  |
| ------------- | ----------------------- |
| `:w`          | Save file               |
| `:w filename` | Save as filename        |
| `:q`          | Quit (fails if unsaved) |
| `:q!`         | Force quit              |
| `:wq` or `:x` | Save and quit           |
| `:e filename` | Open file               |
| `:123`        | Go to line 123          |
| `:help`       | Show help               |

### Search

| Key        | Action               |
| ---------- | -------------------- |
| `/pattern` | Search for pattern   |
| `n`        | Find next occurrence |

### Sidebar (Browse Mode)

| Key          | Action           |
| ------------ | ---------------- |
| `Tab`        | Toggle sidebar   |
| `j` `↓`      | Move down        |
| `k` `↑`      | Move up          |
| `Enter` `l`  | Open file/folder |
| `h`          | Go to parent     |
| `q`          | Close sidebar    |
| Double-click | Open file/folder |

### Mouse

| Action                 | Effect          |
| ---------------------- | --------------- |
| Left click             | Position cursor |
| Left drag              | Select text     |
| Scroll wheel           | Scroll view     |
| Double-click (sidebar) | Open file       |

### Shortcuts

| Key      | Action    |
| -------- | --------- |
| `Ctrl+S` | Save file |
| `Ctrl+Q` | Quit      |

## 🖼️ Screenshots

```
  BROWSE  [No Name] | Ln 1, Col 1 | 1 lines
───────────────────────────────────────────────
 [..]              │    1
 [src]             │    2 ~
  README.md        │    3 ~
  az.c             │    4 ~
  az.exe           │    5 ~
                   │
                   │
 AZ Editor v1.1 | :help | Tab: sidebar
```

## 📄 License

MIT License - Feel free to use, modify, and distribute.

## 🤝 Contributing

Contributions are welcome! Feel free to:

- Report bugs
- Suggest features
- Submit pull requests

## 🗺️ Roadmap

- [ ] Syntax highlighting for common languages
- [ ] Split views
- [ ] Find and replace
- [ ] Configuration file support
- [ ] Line wrapping options
- [ ] Bracket matching

---

Made with ❤️ for minimalist developers
