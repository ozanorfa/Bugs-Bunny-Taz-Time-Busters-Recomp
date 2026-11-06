#!/usr/bin/env python3
"""Headless Ghidra driver for this project (PyGhidra, cross-platform).

Requirements: Ghidra 12.1.4 with the ghidra_psx_ldr extension installed,
GHIDRA_INSTALL_DIR pointing at it, JAVA_HOME at a JDK 21, and a Python
environment with Ghidra's bundled `pyghidra` package installed.

The Ghidra project lives in ghidra/projects/ (gitignored: it embeds a copy
of the game's executable). Nothing this script writes may be committed.

Commands (run from the repository root):
  import                     create the project, load disc/<boot EXE> with
                             the PSX loader, auto-analyse (incl. PsyQ
                             signatures), apply analysis/psxrecomp_import.py
  funcs [substring]          list functions (optionally filtered by name)
  decomp <addr>              decompile the function containing <addr>
  disasm <addr> [count]      disassemble <count> instructions from <addr>
  xrefs <addr>               list references to <addr>
  grep <addr> <text>         instructions in the function at <addr> containing <text>
  rename <addr> <name>       name the function / label at <addr>
"""
from __future__ import annotations

import os
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECT_DIR = ROOT / "ghidra" / "projects"
PROJECT_NAME = "BBT"


def boot_exe() -> Path:
    cfg = tomllib.loads((ROOT / "game.toml").read_text(encoding="utf-8"))
    return (ROOT / cfg["game"]["exe"]).resolve()


def start():
    import pyghidra
    install = os.environ.get("GHIDRA_INSTALL_DIR")
    if not install:
        raise SystemExit("set GHIDRA_INSTALL_DIR to the Ghidra 12.1.4 install")
    pyghidra.start(install_dir=Path(install))
    return pyghidra


def program_path() -> str:
    return "/" + boot_exe().name


def cmd_import(pg):
    from ghidra.app.util.importer import ProgramLoader
    from java.io import File
    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    exe = boot_exe()
    with pg.open_project(PROJECT_DIR, PROJECT_NAME, create=True) as project:
        monitor = pg.task_monitor()
        if project.getProjectData().getFile(program_path()) is None:
            results = (ProgramLoader.builder()
                       .source(File(str(exe)))
                       .project(project)
                       .loaders("PsxLoader")
                       .monitor(monitor)
                       .load())
            try:
                results.save(monitor)
            finally:
                results.close()
            print(f"imported {exe.name}")
        with pg.program_context(project, program_path()) as program:
            print("analysing ...")
            log = pg.analyze(program, monitor)
            for line in str(log).splitlines():
                if "PsyQ" in line or "rror" in line:
                    print("  " + line)
            script = ROOT / "analysis" / "psxrecomp_import.py"
            if script.is_file():
                pg.ghidra_script(script, project, program)
            program.save("analysis + psxrecomp import", monitor)
            fm = program.getFunctionManager()
            print(f"functions: {fm.getFunctionCount()}")


def with_program(pg, fn):
    with pg.open_project(PROJECT_DIR, PROJECT_NAME) as project:
        with pg.program_context(project, program_path()) as program:
            return fn(pg, project, program)


def to_addr(program, text):
    return program.getAddressFactory().getDefaultAddressSpace().getAddress(int(text, 16))


def cmd_funcs(pg, project, program, pattern=""):
    for f in program.getFunctionManager().getFunctions(True):
        name = str(f.getName())
        if pattern.lower() in name.lower():
            print(f"{f.getEntryPoint()}  {f.getBody().getNumAddresses():6d}  {name}")


def cmd_decomp(pg, project, program, addr):
    from ghidra.app.decompiler import DecompInterface
    f = program.getFunctionManager().getFunctionContaining(to_addr(program, addr))
    if f is None:
        raise SystemExit(f"no function contains {addr}")
    di = DecompInterface()
    di.openProgram(program)
    res = di.decompileFunction(f, 120, pg.task_monitor())
    if not res.decompileCompleted():
        raise SystemExit(f"decompile failed: {res.getErrorMessage()}")
    print(f"// {f.getName()} @ {f.getEntryPoint()}")
    print(res.getDecompiledFunction().getC())


def cmd_disasm(pg, project, program, addr, count="24"):
    listing = program.getListing()
    ins = listing.getInstructionAt(to_addr(program, addr))
    for _ in range(int(count)):
        if ins is None:
            break
        print(f"{ins.getAddress()}  {ins}")
        ins = ins.getNext()


def cmd_grep(pg, project, program, addr, needle):
    """Print every instruction of the function containing <addr> whose text
    contains <needle> (e.g. "0x178(sp)")."""
    f = program.getFunctionManager().getFunctionContaining(to_addr(program, addr))
    if f is None:
        raise SystemExit(f"no function contains {addr}")
    print(f"// {f.getName()} @ {f.getEntryPoint()}")
    for ins in program.getListing().getInstructions(f.getBody(), True):
        text = str(ins)
        if needle.lower() in text.lower():
            print(f"{ins.getAddress()}  {text}")


def cmd_xrefs(pg, project, program, addr):
    rm = program.getReferenceManager()
    fm = program.getFunctionManager()
    for ref in rm.getReferencesTo(to_addr(program, addr)):
        src = ref.getFromAddress()
        f = fm.getFunctionContaining(src)
        print(f"{src}  {ref.getReferenceType()}  in {f.getName() if f else '?'}")


def cmd_rename(pg, project, program, addr, name):
    from ghidra.program.model.symbol import SourceType
    a = to_addr(program, addr)
    with pg.transaction(program, f"rename {addr}"):
        f = program.getFunctionManager().getFunctionAt(a)
        if f is not None:
            f.setName(name, SourceType.USER_DEFINED)
        else:
            program.getSymbolTable().createLabel(a, name, SourceType.USER_DEFINED)
    program.save(f"rename {addr} -> {name}", pg.task_monitor())
    print(f"{addr} -> {name}")


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    pg = start()
    cmd, args = argv[1], argv[2:]
    if cmd == "import":
        cmd_import(pg)
        return
    table = {"funcs": cmd_funcs, "decomp": cmd_decomp, "disasm": cmd_disasm,
             "xrefs": cmd_xrefs, "rename": cmd_rename, "grep": cmd_grep}
    if cmd not in table:
        raise SystemExit(__doc__)
    with_program(pg, lambda pg_, project, program: table[cmd](pg_, project, program, *args))


if __name__ == "__main__":
    main(sys.argv)
