"""Native build commands select the host ABI and diagnose missing SDL before compilation."""

import subprocess
import shutil
import sys

import pytest

from dm2test.markers import ROOT


@pytest.mark.parametrize("host, library, link_flag", [
    ("Linux", "libdm2.so", "-shared"),
    ("Darwin", "libdm2.dylib", "-dynamiclib"),
])
def test_library_build_needs_no_sdl(tmp_path, host, library, link_flag):
    result = subprocess.run(
        ["make", "-nB", "lib", f"HOST_OS={host}", f"B={tmp_path}/build", "PKG_CONFIG=false", "SDL_PREFIX=/missing"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    )
    assert f"/{library}" in result.stdout
    assert link_flag in result.stdout
    assert "SDL3" not in result.stdout
    assert not result.stderr
    if host == "Linux":
        assert "-fPIC" in result.stdout
        assert "-D_DEFAULT_SOURCE" in result.stdout
        assert "-dynamiclib" not in result.stdout


def test_missing_sdl_reports_dependency_error():
    result = subprocess.run(["make", "check-sdl", "HOST_OS=Linux", "PKG_CONFIG=false", "SDL_PREFIX="],
                            cwd=ROOT, capture_output=True, text=True)
    assert result.returncode != 0
    assert "SDL3 >= 3.4.0" in result.stderr


def test_windows_cross_build_keeps_host_flags_out(tmp_path):
    result = subprocess.run(["make", "-nB", "windows", "HOST_OS=Linux", f"B={tmp_path}/build"],
                            cwd=ROOT, capture_output=True, text=True, check=True)
    assert "x86_64-w64-mingw32-gcc" in result.stdout
    assert "-D_DEFAULT_SOURCE" not in result.stdout
    assert "-fPIC" not in result.stdout


@pytest.mark.skipif(shutil.which("pkg-config") is None, reason="pkg-config not installed")
@pytest.mark.parametrize("version, available", [("3.2.0", False), ("3.4.0", True)])
def test_custom_sdl_prefix_checks_version(tmp_path, version, available):
    pc = tmp_path / "lib" / "pkgconfig"
    pc.mkdir(parents=True)
    (pc / "sdl3.pc").write_text(
        f"prefix={tmp_path}\nName: sdl3\nDescription: SDL version fixture\nVersion: {version}\n"
        "Libs: -L${prefix}/lib -lSDL3\nCflags: -I${prefix}/include\n"
    )
    result = subprocess.run(["make", "check-sdl", "HOST_OS=Linux", f"SDL_PREFIX={tmp_path}"],
                            cwd=ROOT, capture_output=True, text=True)
    assert (result.returncode == 0) == available


@pytest.mark.skipif(sys.platform != "linux", reason="ELF runtime paths")
def test_changing_sdl_prefix_relinks_runtime_path(tmp_path):
    shutil.copy2(ROOT / "Makefile", tmp_path / "Makefile")
    source = tmp_path / "src" / "platform"
    source.mkdir(parents=True)
    (source / "main.c").write_text("int main(void) { return 0; }\n")
    for name in ("first", "second"):
        prefix = tmp_path / name
        pc = prefix / "lib" / "pkgconfig"
        pc.mkdir(parents=True)
        (pc / "sdl3.pc").write_text("Name: sdl3\nDescription: SDL fixture\nVersion: 3.4.0\n")
        subprocess.run(["make", "build/dm2", f"SDL_PREFIX={prefix}"], cwd=tmp_path,
                       capture_output=True, text=True, check=True)
        dynamic = subprocess.run(["readelf", "-d", "build/dm2"], cwd=tmp_path,
                                 capture_output=True, text=True, check=True).stdout
        assert f"{prefix}/lib" in dynamic
    unchanged = subprocess.run(["make", "build/dm2", f"SDL_PREFIX={prefix}"], cwd=tmp_path,
                               capture_output=True, text=True, check=True)
    assert "-c -o" not in unchanged.stdout
    assert "-o build/dm2" not in unchanged.stdout
