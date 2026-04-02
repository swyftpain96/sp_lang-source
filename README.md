# SP Programming Language

A modern and expressive language designed for performance and simplicity. SP features a self-contained compiler and virtual machine, accessible through a single standalone executable.

---

## 🚀 Installation (Mac & Linux)

SP is distributed as a standalone binary for **Mac** and **Linux**.

### 1. Setup
Download the `sp` binary and move it to a directory in your `$PATH`, or run it directly from your current folder.

### 2. Add to PATH (Optional)
To use `sp` globally, add the following to your `.bashrc` or `.zshrc`:
```bash
export PATH=$PATH:/path/to/sp_directory
```

### 3. Run Your First Script
Create a file named `hello.sp`:
```sp
console.show("Hello, SP World!")
```
Run it:
```bash
./sp hello.sp
```

---

## 🎨 VS Code Extension

For the best development experience, including **syntax highlighting**, **IntelliSense**, and **type inference**, install the SP Language extension.

### Manual Installation (VSIX)
Since the extension is currently in early development, you can install it manually:

1. Download the latest `.vsix` file: **[sp-language-vscode-0.1.5.vsix](sp_language-vscode/sp-language-vscode-0.1.5.vsix)**.
2. Open VS Code.
3. Open the **Extensions** view (`Ctrl+Shift+X`).
4. Click the `...` (More Actions) menu in the top-right corner of the Extensions view.
5. Select **Install from VSIX...**.
6. Choose the downloaded `.vsix` file.

---

For more technical details, check out:
- 📘 **[Language Guide](LANGUAGE_GUIDE.md)**: Syntax, built-in modules, and core features.
- 🧩 **[Native Addons Guide](NATIVE_ADDONS_GUIDE.md)**: Building high-performance extensions in C++, Zig, Rust, and more.

## ✨ Features at a Glance

- **Modern Syntax**: Expressive and clean logic with `set` (immutable) and `var` (mutable).
- **First-Class Functions**: Powerful closures and arrow functions.
- **Rich Built-in Types**: Extensive methods for Strings (`trim`, `split`, etc.), Arrays (`map`, `filter`, `push`, etc.), Objects, and Numbers.
- **Pipeline Operator (`|>`)**: Readable, left-to-right function chaining with placeholder support.
- **Pattern Matching**: Robust `match` expressions for complex condition handling.
- **Built-in Systems**: Direct access to `console`, `fs` (File System), and `process` utilities.
- **Object-Oriented**: Support for classes with `abstract`, `private`, and `readonly` modifiers.

---

Happy Coding with **SP**!