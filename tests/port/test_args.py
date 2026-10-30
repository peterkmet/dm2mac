"""args_drop_save removes every --save pair the way the option parser walks argv."""

import ctypes

import pytest


# The option table of platform/main.c.
VALUE_OPTIONS = [b"--data", b"--filter", b"--save", b"--control", b"--mode", b"--saves"]


@pytest.mark.parametrize("args, expected", [
    (["dm2"], ["dm2"]),
    (["dm2", "--data", "d", "--save", "x", "--filter", "scale2x"], ["dm2", "--data", "d", "--filter", "scale2x"]),
    (["dm2", "--save", "x", "--filter", "none"], ["dm2", "--filter", "none"]),
    (["dm2", "--data", "d", "--save", "x"], ["dm2", "--data", "d"]),
    (["dm2", "--save", "a", "--save", "b"], ["dm2"]),
    (["dm2", "--data", "--save", "--filter", "none"], ["dm2", "--data", "--save", "--filter", "none"]),
    (["dm2", "--control", "--save", "x"], ["dm2", "--control", "--save", "x"]),
    (["dm2", "--control", "p", "--save", "s"], ["dm2", "--control", "p"]),
    (["dm2", "--mode", "vanilla", "--save", "s"], ["dm2", "--mode", "vanilla"]),
    (["dm2", "--mode", "--save", "s"], ["dm2", "--mode", "--save", "s"]),
])
def test_args_drop_save(lib, args, expected):
    argv = (ctypes.c_char_p * (len(args) + 1))(*[a.encode() for a in args], None)
    options = (ctypes.c_char_p * len(VALUE_OPTIONS))(*VALUE_OPTIONS)
    lib.args_drop_save.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
    argc = lib.args_drop_save(len(args), ctypes.cast(argv, ctypes.c_void_p), ctypes.cast(options, ctypes.c_void_p),
                              len(VALUE_OPTIONS))
    assert argc == len(expected)
    assert [argv[i].decode() for i in range(argc)] == expected
    assert argv[argc] is None
