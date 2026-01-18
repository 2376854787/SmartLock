#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$repo_root"

git config core.hooksPath githooks
echo "Configured git hooks path: githooks"

