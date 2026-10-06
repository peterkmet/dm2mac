"""Run functions of the loaded SKULL.EXE image in a Unicorn x86 emulator.

Flat model: a GDT with one code descriptor (selector 0x08) and one data
descriptor (selector 0x10), both base 0 and limit 4 GiB. CS holds 0x08, all
other segment registers hold 0x10. Code that copies segment registers
(`push ds; pop es`) works. Code that loads a DOS/4GW selector faults.

Calling convention (Watcom register): arguments 1-4 go in EAX, EDX, EBX, ECX,
the rest on the stack, pushed right to left. The callee pops the stack
arguments (`ret n`), so ESP after the return is not the ESP before the call.

Stubs: `stub(addr, fn)` replaces a function of the image with a Python
function. `serve_file(data)` stubs the file I/O wrappers of SKULL.EXE and
serves them from one host buffer, for every file handle:
- `file_seek(fd, offset)` sets the position, returns 1.
- `file_read(fd, size, buf)` copies from the position, returns 1 (like the
  original, a short read at the end of the file is not an error).
- `file_write(fd, size, buf)` appends to the `written` buffer, returns 1.
No other DOS or IBMIOP service is emulated; `int 21h` still raises.
"""

import struct

from unicorn import (
    UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR, UC_HOOK_MEM_FETCH_PROT,
    UC_HOOK_MEM_FETCH_UNMAPPED, UC_MODE_32, UC_PROT_ALL, UC_PROT_READ, UC_PROT_WRITE, Uc, UcError,
)
from unicorn.x86_const import (
    UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_EAX, UC_X86_REG_EBP,
    UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ES,
    UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_GS, UC_X86_REG_SS,
)

PAGE = 0x1000
RET_ADDR = 0x1000  # sentinel return address, one executable page that only the stop hook sees
GDT = 0x2000
STACK_TOP = 0x00400000
STACK_SIZE = 0x00100000
SCRATCH = 0x00500000  # free buffer for inputs, outputs and code stubs
SCRATCH_SIZE = 0x00100000
ARG_REGS = (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ECX)
FILE_SEEK = 0x10F5D
FILE_READ = 0x10FAE
FILE_WRITE = 0x10FFF
RUN_FRAME = 0x8000  # stack room above ESP for the locals of a function that run() enters in the middle


class EmuError(Exception):
    pass


def _align(n):
    return (n + PAGE - 1) & ~(PAGE - 1)


class Emu:
    def __init__(self, image, max_insns=10_000_000):
        self.image = image
        self.max_insns = max_insns
        self.allow_io = False
        self.error = None
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_32)
        lo = image.base & ~(PAGE - 1)
        uc.mem_map(lo, _align(image.base + len(image.data)) - lo, UC_PROT_ALL)
        uc.mem_write(image.base, image.data)
        uc.mem_map(STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        uc.mem_map(SCRATCH, SCRATCH_SIZE, UC_PROT_ALL)
        uc.mem_map(RET_ADDR, PAGE, UC_PROT_ALL)
        uc.mem_map(GDT, PAGE, UC_PROT_READ | UC_PROT_WRITE)  # the CPU sets the accessed bit
        uc.mem_write(GDT, struct.pack("<3Q", 0, 0x00CF9A000000FFFF, 0x00CF92000000FFFF))
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 3 * 8 - 1, 0))
        for reg in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS, UC_X86_REG_GS):
            uc.reg_write(reg, 0x10)
        uc.reg_write(UC_X86_REG_CS, 0x08)
        uc.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.emu_stop(), begin=RET_ADDR, end=RET_ADDR)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_INSN, self._in, aux1=UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, aux1=UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED | UC_HOOK_MEM_FETCH_PROT, self._outside)

    def _pc(self):
        return self.uc.reg_read(UC_X86_REG_EIP)

    def _fail(self, message):
        # Exceptions do not cross the Unicorn C callbacks; call() raises the recorded error.
        if self.error is None:
            self.error = EmuError(f"{message}, pc={self._pc():#x}")
        self.uc.emu_stop()

    def _intr(self, uc, intno, _):
        is_int_n = self.uc.mem_read(self._pc() - 2, 2) == bytes([0xCD, intno])
        if not is_int_n:
            self._fail(f"CPU fault {intno:#x}")
        elif not self.allow_io:
            self._fail(f"interrupt {intno:#x}")

    def _in(self, uc, port, size, _):
        if not self.allow_io:
            self._fail(f"IN port {port:#x}")
        return 0

    def _out(self, uc, port, size, value, _):
        if not self.allow_io:
            self._fail(f"OUT port {port:#x} value {value:#x}")

    def _outside(self, uc, access, addr, size, value, _):
        self._fail(f"execution outside the image (DOS/4GW?) at {addr:#x}")
        return False

    def call(self, addr, *args, allow_io=False):
        """Call addr with Watcom register arguments; return EAX.

        allow_io=True turns IN into a read of 0 and OUT and `int n` into no-ops.
        CPU faults (#DE, #GP, ...) always raise.
        Execution outside the image always raises: nothing is mapped there.
        """
        self.allow_io = allow_io
        self.error = None
        uc = self.uc
        for reg in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP):
            uc.reg_write(reg, 0)
        for reg, value in zip(ARG_REGS, args):
            uc.reg_write(reg, value & 0xFFFFFFFF)
        stack = [RET_ADDR] + [v & 0xFFFFFFFF for v in args[4:]]
        sp = STACK_TOP - 4 * len(stack)
        uc.mem_write(sp, struct.pack(f"<{len(stack)}I", *stack))
        uc.reg_write(UC_X86_REG_ESP, sp)
        try:
            uc.emu_start(addr, 0, count=self.max_insns)
        except UcError as e:
            raise self.error or EmuError(f"{e}, pc={self._pc():#x}") from None
        if self.error:
            raise self.error
        if self._pc() != RET_ADDR:
            raise EmuError(f"instruction budget {self.max_insns} exceeded, pc={self._pc():#x}")
        return uc.reg_read(UC_X86_REG_EAX)

    def run(self, begin, until):
        """Run the code from begin to until, for example the middle part of a function; return EAX.

        ESP starts RUN_FRAME bytes below the stack top, so that the code finds its stack frame
        locals above ESP. The caller writes the locals that the code reads with frame().
        """
        self.error = None
        self.uc.reg_write(UC_X86_REG_ESP, self.frame())
        # Unicorn checks until only in blocks translated for this call; a hook also stops in cached blocks
        stop = self.uc.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.emu_stop(), begin=until, end=until)
        try:
            self.uc.emu_start(begin, until, count=self.max_insns)
        except UcError as e:
            raise self.error or EmuError(f"{e}, pc={self._pc():#x}") from None
        finally:
            self.uc.hook_del(stop)
        if self.error:
            raise self.error
        if self._pc() != until:
            raise EmuError(f"instruction budget {self.max_insns} exceeded, pc={self._pc():#x}")
        return self.uc.reg_read(UC_X86_REG_EAX)

    @staticmethod
    def frame():
        """The ESP value that run() starts with."""
        return STACK_TOP - RUN_FRAME

    def stub(self, addr, fn, stack_args=0):
        """Run fn(eax, edx, ebx, ecx, *stack_args) instead of the function at addr; its result (None = 0) goes to EAX.

        The stub returns like the callee: `ret 4 * stack_args`. Returns the hook handle (uc.hook_del removes it).
        """
        def hook(uc, *_):
            sp = uc.reg_read(UC_X86_REG_ESP)
            try:
                stack = [self.read_u32(sp + 4 + 4 * i) for i in range(stack_args)]
                result = fn(*(uc.reg_read(r) for r in ARG_REGS), *stack) or 0
            except Exception as e:
                self._fail(f"stub {addr:#x}: {e!r}")
                return
            uc.reg_write(UC_X86_REG_EAX, result & 0xFFFFFFFF)
            uc.reg_write(UC_X86_REG_EIP, self.read_u32(sp))
            uc.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * stack_args)

        return self.uc.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)

    def serve_file(self, data):
        """Serve file_seek and file_read from data and collect file_write in written; see the module docstring."""
        pos = 0

        def seek(fd, offset, *_):
            nonlocal pos
            pos = offset
            return 1

        def read(fd, size, buf, *_):
            nonlocal pos
            size = max(0, min(size if size < 0x80000000 else 0, len(data) - pos))  # size is a signed int32
            self.write(buf, data[pos:pos + size])
            pos += size
            return 1

        def write(fd, size, buf, *_):
            self.written.extend(self.read(buf, size))
            return 1

        self.written = bytearray()
        self.stub(FILE_SEEK, seek)
        self.stub(FILE_READ, read)
        self.stub(FILE_WRITE, write)

    def read(self, addr, size):
        return bytes(self.uc.mem_read(addr, size))

    def write(self, addr, data):
        self.uc.mem_write(addr, bytes(data))

    def read_u32(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    def write_u32(self, addr, value):
        self.write(addr, struct.pack("<I", value & 0xFFFFFFFF))
