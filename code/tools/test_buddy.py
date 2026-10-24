#!/usr/bin/env python3
"""Run Buddy host tests and, optionally, build/boot an untouched source copy."""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile


def run(command, **kwargs):
    print("+ " + " ".join(map(str, command)), flush=True)
    return subprocess.run(command, check=True, **kwargs)


def show_summary(output):
    # Every sequence operation remains in the artifact log for reproduction.
    for line in output.splitlines():
        if not line.startswith("buddy-seq "):
            print(line, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--qemu", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    work = Path(tempfile.mkdtemp(prefix="ucore-buddy-test-"))
    print("Test artifacts: " + str(work), flush=True)
    includes = [root / p for p in ("libs", "kern/mm", "kern/driver", "kern/debug")]
    command = [os.environ.get("HOSTCC", "gcc"), "-std=gnu99", "-O2", "-g",
               "-Wall", "-Werror", "-Wno-unused", "-fno-builtin",
               "-ffunction-sections", "-fdata-sections"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
    command += ["-I" + str(p) for p in includes]
    command += [str(root / "tools/test_buddy.c"), str(root / "kern/mm/pmm.c"),
                "-Wl,--gc-sections", "-o", str(work / "test_buddy")]
    run(command)
    result = subprocess.run([str(work / "test_buddy")], cwd=work, timeout=60,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (work / "host.log").write_text(result.stdout)
    show_summary(result.stdout)
    result.check_returncode()
    for case in ("zero_alloc", "zero_free", "reserved_free", "double_free",
                 "duplicate_registration", "out_of_bounds", "allocated_head_flag",
                 "wrong_order_size", "free_nonzero_ref", "stale_checked_bit",
                 "invalid_link", "unmerged_buddies", "insufficient_test_arena"):
        result = subprocess.run([str(work / "test_buddy"), case],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, cwd=work, timeout=10)
        (work / (case + ".log")).write_text(result.stdout)
        if result.returncode != -signal.SIGABRT:
            raise RuntimeError(case + ": expected assertion abort, got " + str(result.returncode))
        print("PASS: assertion for " + case, flush=True)
    if args.qemu:
        tree = work / "kernel"
        shutil.copytree(root, tree, ignore=shutil.ignore_patterns("obj", "bin", "core"))
        prefix = os.environ.get("GCCPREFIX")
        if not prefix:
            bundled = root.parents[1] / "riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-"
            prefix = str(bundled) if Path(str(bundled) + "gcc").is_file() else "riscv64-unknown-elf-"
        with (work / "build.log").open("w") as log:
            run(["make", "-j2", "GCCPREFIX=" + prefix], cwd=tree,
                stdout=log, stderr=subprocess.STDOUT, timeout=60)
        qemu = os.environ.get("QEMU", "qemu-system-riscv64")
        command = [qemu, "-machine", "virt", "-nographic", "-bios", "default",
                   "-device", "loader,file=bin/ucore.img,addr=0x80200000"]
        print("+ " + " ".join(command), flush=True)
        process = subprocess.Popen(command, cwd=tree, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        stopped = False
        try:
            output, _ = process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            stopped = True
            process.terminate()
            try:
                output, _ = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                output, _ = process.communicate(timeout=5)
        (work / "qemu.log").write_text(output)
        show_summary(output)
        for marker in ("memory management: buddy_pmm_manager",
                       "buddy_check round=1 restored", "buddy_check round=2 restored",
                       "buddy_check() succeeded!", "check_alloc_page() succeeded!"):
            if marker not in output:
                raise RuntimeError("missing boot marker: " + marker)
        if "kernel panic" in output:
            raise RuntimeError("kernel panic during boot")
        print("PASS: QEMU boot; process exit=" + str(process.returncode)
              + (" (terminated by harness after kernel idle loop)" if stopped else ""))
    print("Validation completed; artifacts: " + str(work))


if __name__ == "__main__":
    main()
