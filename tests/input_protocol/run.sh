#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$repo/tests/input_protocol/stubs" -I"$repo/main" \
  "$repo/tests/input_protocol/test_input_protocol.c" \
  "$repo/main/hid_report_parser.c" "$repo/main/usb_keyboard_report.c" \
  "$repo/main/keyboard_handler.c" -o "$work/test_input_protocol"
"$work/test_input_protocol"
