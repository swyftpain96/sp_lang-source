# SP Language VS Code Extension

Provides syntax highlighting, basic IntelliSense, and type inference for the SP language (`.sp` files).

## Features

- Syntax highlighting (keywords, numbers, strings, punctuation)
- Completion for keywords + variables declared with `set`
- Hover type information for variables
- Diagnostics for undefined variables and unsupported binary operations

## Development

Install dependencies and compile:

```bash
cd sp_language-vscode
npm install
npm run compile
```

Launch in VS Code: Use the **Run Extension** debug configuration.
