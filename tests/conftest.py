import subprocess
from pathlib import Path

import pytest

pytest.register_assert_rewrite("dm2test")

pytest_plugins = [
    "dm2test.control",
    "dm2test.creature",
    "dm2test.dialog",
    "dm2test.gamefiles",
    "dm2test.gdat",
    "dm2test.input",
    "dm2test.interface",
    "dm2test.modes",
    "dm2test.scenario",
]


def pytest_configure(config):
    from dm2test.markers import ROOT

    # builds once in the main process, before the xdist workers start
    if not hasattr(config, "workerinput"):
        sdl = subprocess.run(["brew", "--prefix", "sdl3"], capture_output=True, text=True)
        have_sdl = sdl.returncode == 0 and (Path(sdl.stdout.strip()) / "include/SDL3/SDL.h").exists()
        result = subprocess.run(["make", "all" if have_sdl else "lib"], cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise pytest.UsageError("make failed:\n" + result.stderr[-4000:])


def pytest_sessionfinish(session, exitstatus):
    from dm2test.markers import ROOT

    # records the git tree of a green full run of a clean checkout; tools/test-suite skips a rerun of that tree
    if hasattr(session.config, "workerinput") or exitstatus != 0 or session.config.args != ["tests"]:
        return
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    if git("status", "--porcelain", "--untracked-files=no"):
        return
    with open(Path(git("rev-parse", "--path-format=absolute", "--git-common-dir")) / "green-trees", "a") as f:
        f.write(git("rev-parse", "HEAD^{tree}") + "\n")
