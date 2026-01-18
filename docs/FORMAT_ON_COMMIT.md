# 提交时自动 clang-format（不依赖 IDE）

本仓库根目录提供 `.clang-format`（基于 Google 风格）。为了保证每次提交的代码风格一致，并且不依赖任何 IDE 的格式化设置，建议启用仓库内置的 git hooks。

## 1) 安装 hooks（推荐）

Windows / PowerShell：

- 运行：`powershell -ExecutionPolicy Bypass -File scripts/install-githooks.ps1`

这会配置：`git config core.hooksPath githooks`，使 git 在提交时执行 `githooks/pre-commit`。

Linux / macOS / Git Bash：

- 运行：`sh scripts/install-githooks.sh`

## 2) 需要的软件

- 安装 LLVM 的 `clang-format`
- 确保 `clang-format` 在 `PATH` 中（命令行可直接执行）

说明：本仓库的 hook 是 `sh/bash` 脚本；在 Windows 上使用 Git for Windows（自带 Git Bash）即可正常运行。

## 3) 行为说明

每次 `git commit` 时：

- `githooks/pre-commit` 会找到“已暂存”的 C/C++ 文件（例如 `*.c/*.h/*.cpp`）
- 对这些文件执行：`clang-format --style=file`
- 再次 `git add`，确保格式化后的内容被提交

如果本机找不到 `clang-format`，hook 会阻止提交并给出提示。
