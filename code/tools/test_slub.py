#!/usr/bin/env python3
"""Run SLUB host tests and, optionally, build/boot an untouched source copy."""
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
        if not line.startswith(("buddy-seq ", "slub-seq ")):
            print(line, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--qemu", action="store_true")
    parser.add_argument("--normal", action="store_true", help="host build without failure-injection macro")
    parser.add_argument("--instrumented-kernel", action="store_true", help="kernel also defines SLUB_TEST")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    work = Path(tempfile.mkdtemp(prefix="ucore-slub-test-"))
    print("Test artifacts: " + str(work), flush=True)
    includes = [root / p for p in ("libs", "kern/mm", "kern/driver", "kern/debug")]
    command = [os.environ.get("HOSTCC", "gcc"), "-std=gnu99", "-O2", "-g",
               "-Wall", "-Werror", "-Wno-unused", "-fno-builtin",
               "-ffunction-sections", "-fdata-sections"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
    command += ["-I" + str(p) for p in includes]
    if not args.normal:
        command += ["-DSLUB_TEST"]
    command += [str(root / "tools/test_slub.c"),
                str(root / "kern/mm/pmm.c"), str(root / "kern/mm/buddy_pmm.c"),
                "-Wl,--gc-sections",
                "-o", str(work / "test_slub")]
    run(command)
    result = subprocess.run([str(work / "test_slub")], cwd=work, timeout=60,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (work / "host.log").write_text(result.stdout)
    show_summary(result.stdout)
    result.check_returncode()
    cases = ("freelist_cycle", "freelist_live", "freelist_cross_slab", "wrong_capacity",
             "wrong_state", "active_full", "retired_reference", "backend_pair",
             "backend_duplicate", "noop_state", "foreign_live")
    for case in cases:
        result = subprocess.run([str(work / "test_slub"), case], cwd=work,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=20)
        (work / (case + ".log")).write_text(result.stdout)
        marker = "foreign live" if case == "foreign_live" else "SLUB test scenario=" + case
        if result.returncode != -signal.SIGABRT or marker not in result.stdout:
            raise RuntimeError(case + ": expected specified assertion abort, got "
                               + str(result.returncode) + "\n" + result.stdout[-3000:])
        print("PASS: independent negative checker case " + case, flush=True)
    if args.qemu:
        tree = work / "kernel"
        shutil.copytree(root, tree, ignore=shutil.ignore_patterns("obj", "bin", "core"))
        prefix = os.environ.get("GCCPREFIX")
        if not prefix:
            bundled = root.parents[1] / "riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-"
            prefix = str(bundled) if Path(str(bundled) + "gcc").is_file() else "riscv64-unknown-elf-"
        with (work / "build.log").open("w") as log:
            build_command = ["make", "-j2", "GCCPREFIX=" + prefix]
            if args.instrumented_kernel:
                build_command += ["DEFS=-DSLUB_TEST"]
            run(build_command, cwd=tree,
                stdout=log, stderr=subprocess.STDOUT, timeout=180)
        qemu = os.environ.get("QEMU", "qemu-system-riscv64")
        command = [qemu, "-machine", "virt", "-nographic", "-bios", "default",
                   "-device", "loader,file=bin/ucore.img,addr=0x80200000"]
        print("+ " + " ".join(command), flush=True)
        process = subprocess.Popen(command, cwd=tree, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        stopped = False
        try:
            output, _ = process.communicate(timeout=45)
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
                       "buddy_check() succeeded!", "check_alloc_page() succeeded!",
                       "slub_check round=1 restored", "slub_check round=2 restored",
                       "slub_check() succeeded!"):
            if marker not in output:
                raise RuntimeError("missing boot marker: " + marker)
        if "kernel panic" in output:
            raise RuntimeError("kernel panic during boot")
        print("PASS: QEMU boot; process exit=" + str(process.returncode)
              + (" (terminated by harness after kernel idle loop)" if stopped else ""))
    print("Validation completed; artifacts: " + str(work))


if __name__ == "__main__":
    main()
