#!/bin/sh
# Lab2 function2-2 2511107: run the real Best-Fit module without page_init/QEMU.
set -eu

task_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_code_dir=$(CDPATH= cd -- "$task_script_dir/.." && pwd)
task_cc=${HOSTCC:-gcc}

if ! command -v "$task_cc" >/dev/null 2>&1; then
    printf 'Host compiler not found: %s\n' "$task_cc" >&2
    exit 1
fi
if ! command -v timeout >/dev/null 2>&1; then
    printf 'The timeout utility is required for the bounded host test.\n' >&2
    exit 1
fi

task_binary=$(mktemp /tmp/lab2-best-fit-host.XXXXXX)
trap 'rm -f -- "$task_binary"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

task_sanitizer_flags=
if [ "${BEST_FIT_SANITIZERS:-auto}" != 0 ]; then
    if printf '%s\n' 'int main(void) { return 0; }' |
        "$task_cc" -x c - -fsanitize=address,undefined -o /dev/null \
        >/dev/null 2>&1; then
        task_sanitizer_flags='-fsanitize=address,undefined -fno-omit-frame-pointer'
        printf 'Host test sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer\n'
    else
        printf 'Host test sanitizers: unavailable; running ordinary host assertions\n'
    fi
else
    printf 'Host test sanitizers: disabled by BEST_FIT_SANITIZERS=0\n'
fi

# Word splitting is intentional only for the fixed sanitizer flag string.
# Both source paths refer to real project files; allocator code is not copied.
"$task_cc" -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused \
    -Wno-sign-compare -fno-builtin -nostdinc -Ducore_test \
    $task_sanitizer_flags \
    -I"$task_code_dir/libs" -I"$task_code_dir/kern/mm" \
    -I"$task_code_dir/kern/debug" -I"$task_code_dir/kern/driver" \
    -I"$task_code_dir/kern/arch" \
    "$task_script_dir/test_best_fit_host.c" \
    "$task_code_dir/kern/mm/best_fit_pmm.c" \
    -o "$task_binary"

timeout 20s "$task_binary"
