# Runtime layer and timing

The DOS runtime of `SKULL.EXE`: the SVGA option of the installer, the PIT divisor and the tick rates of the game, the music, and the digital driver, and the resident layer `IBMIOP.EXE` that the game calls for video, input, timer, and the movie script. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## SVGA option

- The SVGA option changes only the movies. It does not change the game. The game always runs in VGA mode `0x13` (320 × 200, 256 colors).
- `INSTALL.EXE` asks "Press S for SVGA animations, V for standard VGA". For SVGA it copies `INTRO`, `FTL`, `END`, and `CREDITS`. For VGA it copies `INTRO.VGA`, `FTL.VGA`, and `END.VGA` under the names `INTRO`, `FTL`, `END`, and `CREDITS.PCX` instead of the credits movie. It writes `DM2.BAT` with `IBMIOP SKULL.EXE` and, probably for SVGA only, the switch ` +VS`.
- The movie files, their video modes, and their video sizes: [movies.md](movies.md#movie-player).
- `IBMIOP.EXE` holds two launch scripts, one for SVGA and one for VGA ([movies.md](movies.md#launcher-script-ibmiopexe)).

The files show this:

- `INSTALL.EXE` and `IBMIOP.EXE` are LZEXE 0.91 (`LZ91`) files. Decompressed strings: the copy lists, the SVGA prompt, `IBMIOP SKULL.EXE`, ` +VS`, `DM2.BAT`.
- The MVE opcode `0x0a` (set video mode) of each movie gives its video mode ([movies.md](movies.md#mve-stream)).
- `IBMIOP.EXE` (decompressed `0x19fe`): `int 10h` with `AX=0x0013`. `SKULL.EXE` has no `int 10h` and no VESA call.
- The script tables, the script records, and the arguments of the movie player: [movies.md](movies.md).

The effect is certain. The exact use of ` +VS` is probable, not verified.

## PIT divisor and tick rates

- `SKULL.EXE` programs PIT channel 0 with divisor `4971` (`0x136b`): 1193180 / 4971 = 240.02 Hz.
- `IBMIOP.EXE` programs the same divisor `0x136b`.
- Music: the HMP song timer event runs at 120 Hz. One HMP delta tick = 1/120 s at tempo 100 %.
- Digital driver service event: 120 Hz.
- Game time: `IBMIOP.EXE` calls the game timer callback (`INT 2Dh`, handler `FUN_00010cc2`) on every 4th of its timer interrupts: nominal 60 Hz. Each call adds 1 to `DAT_0007f256`. A game step is due when the sum reaches `DAT_0007f268` = 8. Nominal game step rate: 7.5 Hz (133 ms). `DAT_0007f268 = 1` in one special mode (`FUN_00021d6c`, `command_execute`).

PIT programming in the HMI timer library, `FUN_00062efd` and `FUN_00062f43`:

```text
00062f1e: 66ba4300   mov dx, 0x43
00062f22: b036       mov al, 0x36        ; channel 0, lo/hi, mode 3
00062f24: ee         out dx, al
00062f25: 66ba4000   mov dx, 0x40
00062f29: 8b4508     mov eax, [ebp+8]    ; divisor
00062f2c: ee         out dx, al
00062f2d: 8ac4       mov al, ah
00062f2f: ee         out dx, al
```

- `FUN_00010010` calls `FUN_0005ae04(0xf0, 0, 0)` (`sosTIMERInitSystem(240)`): divisor = `0x1234dc / 240` = 4971. It also registers a chain event to the old `INT 8` handler at the BIOS rate (`0xff00`).
- `FUN_0005af1a` (`sosTIMERRegisterEvent`) lowers the divisor only for a rate above the current rate. Registered rates: `0x78` (120 Hz, digital driver, `FUN_00010010`) and `song[+0x38]` (`sosMIDIStartSong`, `FUN_0005fed2`). `FUN_0005f52b` registers 1500 Hz, but nothing calls it.
- HMP header of every game song and of `TEST.HMP`: `+0x30` u32 tracks, `+0x34` u32 `0xc0`, `+0x38` u32 `0x78` (120), `+0x3c` u32 `0x7e` (126 s). The longest `TEST.HMP` track has 15211 delta ticks = 126.8 s at 120 Hz.
- `IBMIOP.EXE` (decompressed `0x2c58`): `mov word [bp-2], 0x136b` → `cs:[0x125e]`. Its `INT 8` handler at `0x2c73` writes `0x36`, then the divisor to ports `0x43` and `0x40`. Every 4th interrupt (`and al, 3`) it calls the registered far callback `[0xa9d0]`. That callback (`0x3007`) executes `int 2Dh`.
- `SKULL.EXE` `FUN_0005a8b1`: sets vector `0x2d` to `FUN_00010cc2`, then `int 0FCh` with `EAX=0x0f`. `IBMIOP.EXE` function `0x0f` stores the callback and returns 1. `FUN_00010d0f` first calls `int 0FCh` function `0x2a`, which reinstalls the `IBMIOP.EXE` timer handler.
- `FUN_00013887` (frame wait) polls VGA port `0x3da` bit 3. It does not use the PIT.

The divisor 4971 and the 120 Hz music tick are certain. The 60 Hz and 7.5 Hz game rates are probable ([Unknown values](#unknown-values)).

More facts of the `IBMIOP.EXE` timer:

- `IBMIOP.EXE` function `0x0f` ends with `mov ax,1; retf`. Thus `g_timer_step` (`0x7efac`) is 1: each callback adds 1 tick.
- The tick from the keyboard poll (function `0x02`) runs only with the `IBMIOP.EXE` option `+D`. `DM2.BAT` passes `+VS`, so the tick comes from the `INT 8` handler on every 4th interrupt. This agrees with the reading above.
- Function `0x30` (shutdown) clears the flag that function `0x0f` sets. After it, no tick arrives.
- Game code reads no clock: all `int 21h` calls are in the Watcom library, and there is no `int 1Ah`. `g_game_time` (`0x7f22c`) counts game steps. Timeline event times are game steps (24 bits).

## Unknown values

Both programs hook `INT 8`: the HMI handler in protected mode, the `IBMIOP.EXE` handler in real mode. The HMI handler calls the old handler only at about 18.2 Hz. Thus the real rate of the `INT 2Dh` callback depends on how DOS/4GW routes `IRQ 0`. Static analysis cannot fix it. A measurement in DOSBox can: compare the game clock in two saves with a known real time between them. reDM2 did not measure the real rate either. Manual check C10 compares the game rate of the port with DOSBox.

## IBMIOP.EXE runtime layer

`DM2.BAT` starts `IBMIOP SKULL.EXE`. `IBMIOP.EXE` is a real-mode resident layer that `SKULL.EXE` calls through `int 0FCh` with a shared buffer (`DAT_0008048c`). It sets video mode `0x13`, writes the palette (port `0x3c8`), reads `skull.cfg`, handles the keyboard and timer, and runs the movie script. `SKULL.EXE` uses 34 call sites with function numbers `0x00` to `0x30` and `0x103` to `0x10a` (for example `0x0f` timer callback, `0x2a` timer reinstall, `0x2e`, `0x2f`, `0x30` at start and exit).

[skull-map.md](../reference/skull-map.md#ibmiop-int-0xfc-service-table) lists these `int 0FCh` functions. The platform layer of the port replaces them (video, palette, input, timer).

## Deviations from the original

- The port sets `PIT_DIVISOR` in `src/platform/platform.c` to `4971` (240.02 Hz) and drives the HMP player at 120 Hz. The timer tick and the game step of the port: [movement.md](../rules/movement.md#timing-and-the-game-step).
- The port plays the SVGA movie script only; the VGA script (`*.VGA`, `CREDITS.PCX`) is not used ([movies.md](movies.md#deviations-from-the-original)).
