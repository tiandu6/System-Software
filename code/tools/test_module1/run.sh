#!/usr/bin/env bash
# Lab3 module-one standalone fixture. Does not compile module-two handlers.
set -euo pipefail

fixture_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$fixture_dir/../.." && pwd)
compiler="${CC:-${GCCPREFIX:-riscv64-unknown-elf-}gcc}"
objcopy="${OBJCOPY:-${GCCPREFIX:-riscv64-unknown-elf-}objcopy}"
qemu="${QEMU:-qemu-system-riscv64}"

command -v "$compiler" >/dev/null
command -v "$objcopy" >/dev/null
command -v "$qemu" >/dev/null
if grep -q 'function1 YOUR CODE' "$repo/kern/trap/trapentry.S"; then
    echo 'Module1 source is still incomplete; refusing to run.'
    exit 2
fi

build_dir=$(mktemp -d /tmp/ucore-lab3-module1.XXXXXXXX)
trap 'rm -rf -- "$build_dir"' EXIT
"$compiler" --version | head -n 1
"$qemu" --version | head -n 1
sha256sum "$repo/kern/trap/trapentry.S"

flags=(-march=rv64imac -mabi=lp64 -mcmodel=medany -mno-relax
       -msmall-data-limit=0 -ffreestanding -fno-builtin -fno-pie
       -fno-stack-protector -nostdlib -nostdinc -O2 -g
       -Wall -Wextra -Werror -I"$repo/libs" -I"$repo/kern/trap")
"$compiler" "${flags[@]}" -c "$repo/kern/trap/trapentry.S" -o "$build_dir/trapentry.o"
"$compiler" "${flags[@]}" -c "$fixture_dir/start.S" -o "$build_dir/start.o"
"$compiler" "${flags[@]}" -c "$fixture_dir/harness.c" -o "$build_dir/harness.o"
"$compiler" "${flags[@]}" -Wl,--no-relax -Wl,-T,"$fixture_dir/linker.ld" \
    -o "$build_dir/module1.elf" "$build_dir/start.o" "$build_dir/harness.o" "$build_dir/trapentry.o"
"$objcopy" -O binary "$build_dir/module1.elf" "$build_dir/module1.bin"
echo 'Build: PASS (actual repository trapentry.S linked without modification)'

set +e
timeout 12s "$qemu" -machine virt -m 128M -smp 1 -nographic -bios default \
    -device loader,file="$build_dir/module1.bin",addr=0x80200000 \
    -monitor none -no-reboot > "$build_dir/terminal-output.txt" 2>&1
status=$?
set -e
cat "$build_dir/terminal-output.txt"
if ! grep -q 'ALL MODULE1 HARNESS TESTS PASSED' "$build_dir/terminal-output.txt" ||
   grep -q '\[MODULE1 FAIL\]' "$build_dir/terminal-output.txt"; then
    echo "Standalone validation: FAIL (qemu/timeout exit $status)"
    exit 1
fi
echo 'Standalone validation: PASS (3/3 synchronous trap/return round trips)'
echo 'Scope: module one only; NOT full Lab3 grading or module-two validation.'
