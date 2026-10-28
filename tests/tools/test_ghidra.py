import os
import shutil
import subprocess
from pathlib import Path

import pytest

from dm2test.markers import ROOT
from dm2tools.le import load_image


def ghidra_installed():
    if not shutil.which("brew"):
        return False
    prefix = subprocess.run(["brew", "--prefix", "ghidra"], capture_output=True, text=True).stdout.strip()
    return Path(prefix, "libexec/support/analyzeHeadless").exists()


def java_installed():
    """The JDK that tools/ghidra/decompile.sh uses: JAVA_HOME, or openjdk@21 of Homebrew."""
    home = os.environ.get("JAVA_HOME")
    if not home:
        prefix = subprocess.run(["brew", "--prefix", "openjdk@21"], capture_output=True, text=True).stdout.strip()
        home = str(Path(prefix, "libexec/openjdk.jdk/Contents/Home"))
    return Path(home, "bin/java").exists()


@pytest.mark.skipif(not ghidra_installed(), reason="Ghidra not installed")
@pytest.mark.skipif(not java_installed(), reason="Java not installed")
@pytest.mark.timeout(900)
def test_decompile_skull(game_dir, tmp_path):
    image = load_image(game_dir / "SKULL.EXE")
    binary = tmp_path / "skull.bin"
    binary.write_bytes(image.data)
    symbols = tmp_path / "symbols.csv"
    symbols.write_text(f"address,name,signature,subsystem\n{image.entry:#x},test_entry,,test\n")
    out = tmp_path / "SKULL.c"
    subprocess.run(
        [ROOT / "tools/ghidra/decompile.sh", binary, hex(image.base), hex(image.objects[1].base), out, hex(image.entry)],
        env={**os.environ, "SYMBOLS": str(symbols)},
        check=True,
    )
    c = out.read_text()
    entry_c = c.split(f"/* {image.entry:08x} */")[1][:500]
    assert "__watcall test_entry(" in entry_c
    assert c.count("__watcall") > 1000
    assert "SCRIPT ERROR" not in Path(f"{out}.log").read_text()
