#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$repo/tests/usb_recovery/stubs" -I"$repo/main" \
  "$repo/tests/usb_recovery/test_usb_recovery.c" "$repo/main/usb_hid_recovery.c" \
  -o "$work/test_usb_recovery"
"$work/test_usb_recovery"
