#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/mouse-accumulator.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
  -I "$repo_root/tests/mouse_accumulator/stubs" -I "$repo_root/main" \
  "$repo_root/main/mouse_accumulator.c" \
  "$repo_root/tests/mouse_accumulator/test_mouse_accumulator.c" \
  -o "$test_binary"
"$test_binary"
