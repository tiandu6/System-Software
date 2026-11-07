#!/usr/bin/env python3
"""Run module-two checks in a temporary copy; preserve the submitted kernel."""

from pathlib import Path
import shutil
import subprocess
import tempfile


repo = Path(__file__).resolve().parents[2]
checks = Path(__file__).with_name("checks.c")


def run(command, cwd):
    print("$ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


with tempfile.TemporaryDirectory(prefix="lab3-module2-") as directory:
    target = Path(directory) / "code"
    shutil.copytree(repo, target, ignore=shutil.ignore_patterns("obj", "bin", ".qemu*"))
    init = target / "kern/init/init.c"
    text = init.read_text()
    anchor = "    idt_init();"
    position = text.rindex(anchor) + len(anchor)
    text = "extern void test_module2(void);\n" + text[:position] + (
        "\n    test_module2();\n" + text[position:]
    )
    init.write_text(text)
    shutil.copyfile(checks, target / "kern/init/module2_checks.c")
    run(["make", "-j2", "DEFS+=-Dsbadaddr=stval"], target)
    print("$ qemu-system-riscv64 (module-two supplemental checks)", flush=True)
    completed = subprocess.run(
        ["qemu-system-riscv64", "-machine", "virt", "-nographic", "-bios", "default",
         "-device", "loader,file=bin/ucore.img,addr=0x80200000", "-monitor", "none",
         "-no-reboot"],
        cwd=target, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=15, check=True,
    )
    print(completed.stdout, end="", flush=True)
    assert "ALL MODULE2 SUPPLEMENTAL TESTS PASSED" in completed.stdout
    assert completed.stdout.count("Unknown interrupt: cause 15") == 2
    assert "cause    0x00000014" in completed.stdout
    for marker in (
        "[M2] 16-bit c.ebreak returned: PASS",
        "[M2] 32-bit ebreak returned: PASS",
        "[M2] 16-bit illegal instruction returned: PASS",
        "[M2] 32-bit illegal instruction returned: PASS",
        "[M2] software/unknown IRQs preserve frame and ticks: PASS",
        "[M2] unknown exception preserves EPC and frame: PASS",
        "[M2] halfword-aligned 32-bit EPC advance: PASS",
    ):
        assert marker in completed.stdout, marker
    print("Supplemental QEMU exit status: 0", flush=True)

    # Verify that the shutdown threshold is configurable without source edits.
    init.write_text((repo / "kern/init/init.c").read_text())
    (target / "kern/init/module2_checks.c").unlink()
    run(["make", "clean"], target)
    run(["make", "-j2", "DEFS+=-Dsbadaddr=stval -DSHUTDOWN_AFTER=1"], target)
    completed = subprocess.run(
        ["qemu-system-riscv64", "-machine", "virt", "-nographic", "-bios", "default",
         "-device", "loader,file=bin/ucore.img,addr=0x80200000", "-monitor", "none",
         "-no-reboot"],
        cwd=target, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=10, check=True,
    )
    print(completed.stdout, end="", flush=True)
    assert completed.stdout.splitlines().count("100 ticks") == 1
    print("[M2] SHUTDOWN_AFTER=1: one output, clean shutdown: PASS", flush=True)
    print("ALL MODULE2 VALIDATIONS PASSED", flush=True)
