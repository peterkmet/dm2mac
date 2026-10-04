import ctypes
import os
import subprocess

import pytest

from conftest import ROOT


def run(lib, n, rows):
    h, w = len(rows), len(rows[0])
    src = (ctypes.c_uint8 * (w * h))(*[int(c) for r in rows for c in r])
    dst = (ctypes.c_uint8 * (w * h * n * n))()
    getattr(lib, f"scale{n}x")(src, w, h, dst)
    return ["".join(str(v) for v in dst[i * w * n:(i + 1) * w * n]) for i in range(h * n)]


@pytest.mark.parametrize(
    "n, src, out",
    [
        (2, ["55", "55"], ["5555"] * 4),  # flat area
        (3, ["55", "55"], ["555555"] * 6),
        (2, ["000", "010", "000"], ["000000", "000000", "001100", "001100", "000000", "000000"]),  # single pixel
        (3, ["000", "010", "000"], ["000000000"] * 3 + ["000111000"] * 3 + ["000000000"] * 3),
        (2, ["10", "01"], ["1100", "1010", "0101", "0011"]),  # diagonal edge
        (3, ["10", "01"], ["111000", "110100", "100110", "011001", "001011", "000111"]),
        (2, ["10", "00"], ["1100", "1000", "0000", "0000"]),  # corner rounding
    ],
)
def test_scale(lib, n, src, out):
    assert run(lib, n, src) == out


@pytest.mark.parametrize("args, env", [(["--filter", "bad"], None), ([], "bad"), (["--filter", "bad"], "none")])
def test_unknown_filter_exits_2(lib, args, env):
    environ = {k: v for k, v in os.environ.items() if k != "DM2_FILTER"} | ({"DM2_FILTER": env} if env else {})
    result = subprocess.run([ROOT / "build" / "dm2", *args], env=environ, capture_output=True, timeout=10)
    assert result.returncode == 2
    assert b"unknown filter bad" in result.stderr
