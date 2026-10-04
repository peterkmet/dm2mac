import pytest

from dm2tools.emu import SCRATCH, Emu, EmuError
from dm2tools.le import load_image

# Watcom clib strcat(dst=EAX, src=EDX) -> dst: push ds; pop es; repne scasb; copy loop.
STRCAT = 0x6418E


@pytest.fixture
def emu(game_dir):
    return Emu(load_image(game_dir / "SKULL.EXE"))


@pytest.mark.parametrize("a, b", [(b"", b""), (b"", b"x"), (b"abc", b""), (b"Skull", b"keep"), (b"a" * 300, b"b" * 77)], ids=range(5))
def test_strcat(emu, a, b):
    dst, src = SCRATCH, SCRATCH + 0x1000
    emu.write(dst, a + b"\0" + b"\xff" * 0x400)
    emu.write(src, b + b"\0")
    assert emu.call(STRCAT, dst, src) == dst
    assert emu.read(dst, len(a + b) + 2) == a + b + b"\0\xff"


def test_stack_args(emu):
    # mov eax,[esp+4]; add eax,[esp+8]; add eax,ecx; ret 8 -- args 4..6 on the stack, callee pops.
    stub = SCRATCH + 0x2000
    emu.write(stub, bytes.fromhex("8b442404 03442408 01c8 c20800"))
    assert emu.call(stub, 1, 2, 3, 40, 500, 6000) == 6540


def test_int_raises(emu):
    stub = SCRATCH + 0x3000
    emu.write(stub, bytes.fromhex("cd21 c3"))  # int 21h; ret
    with pytest.raises(EmuError, match="interrupt 0x21"):
        emu.call(stub)
    emu.call(stub, allow_io=True)


def test_in_raises(emu):
    stub = SCRATCH + 0x3100
    emu.write(stub, bytes.fromhex("ec c3"))  # in al, dx; ret
    with pytest.raises(EmuError, match="IN port"):
        emu.call(stub)


def test_outside_image_raises(emu):
    with pytest.raises(EmuError, match="outside the image"):
        emu.call(0x00F00000)
