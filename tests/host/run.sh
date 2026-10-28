#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

INC_COMP=components/epd_gdem042f86/include
INC_MAIN=main

run() {
    local name="$1"; shift
    echo "== $name"
    gcc -std=c11 -Wall -Wextra -Werror -O1 \
        -I "$INC_COMP" -I "$INC_MAIN" \
        -o "/tmp/$name" "$@" && "/tmp/$name"
}

run test_fb_pack tests/host/test_fb_pack.c
run test_patterns tests/host/test_patterns.c main/epd_patterns.c main/epd_text.c
