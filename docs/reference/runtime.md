# Runtime layer and timing

The DOS runtime of `SKULL.EXE` has three parts: the installer's Super VGA (SVGA) option, the programmable interval timer (PIT) with its tick rates, and the resident layer `IBMIOP.EXE`. Addresses and notation follow the conventions of [skull-map.md](skull-map.md).

## SVGA option

- The SVGA option changes only the movies. It does not change the game. The game always runs in VGA mode `0x13` (320 × 200, 256 colors).
- `INSTALL.EXE` asks "Press S for SVGA animations, V for standard VGA". For SVGA it copies `INTRO`, `FTL`, `END`, and `CREDITS`. For VGA it copies `INTRO.VGA`, `FTL.VGA`, and `END.VGA` as `INTRO`, `FTL`, and `END`, and copies `CREDITS.PCX` instead of the credits movie.
- `INSTALL.EXE` writes `DM2.BAT` with `IBMIOP SKULL.EXE` and, for SVGA only, the switch ` +VS`. How `IBMIOP.EXE` uses ` +VS` is unverified, because no static trace of the switch handler exists.
- `INSTALL.EXE` and `IBMIOP.EXE` are LZEXE 0.91 (`LZ91`) files. The offsets on this page are in the decompressed files.
- `IBMIOP.EXE` sets the video mode with `int 0x10`, `AX=0x0013` (decompressed `0x19fe`). `SKULL.EXE` has no `int 0x10` and no VESA call.
- [movies.md](../formats/movies.md#movie-player) gives the movie files and their video sizes, [movies.md](../formats/movies.md#mve-stream) their video modes (MVE opcode `0x0a`), and [movies.md](../formats/movies.md#launcher-script-ibmiopexe) the two launch scripts of `IBMIOP.EXE`, one for SVGA and one for VGA.

## PIT divisor and tick rates

- `SKULL.EXE` programs PIT channel 0 with divisor `4971` (`0x136b`): 1193180 / 4971 = 240.02 Hz.
- `IBMIOP.EXE` programs the same divisor `0x136b`.
- Music: the HMP song timer event runs at 120 Hz. One HMP delta tick is 1/120 s at tempo 100 %.
- The digital driver's service event runs at 120 Hz.
- Game time: `IBMIOP.EXE` calls the game timer callback (`INT 0x2d`, handler `FUN_00010cc2`) on every 4th of its timer interrupts: nominal 60 Hz. Each call adds 1 to `DAT_0007f256`. A game step is due when the sum reaches `DAT_0007f268` = 8. Nominal game step rate: 7.5 Hz (133 ms). `DAT_0007f268 = 1` while the party sleeps (`FUN_00021d6c`, `command_execute`).
- The port sets `PIT_DIVISOR` (`src/platform/platform.c:15`) to the same divisor `4971` and drives the HMP player at 120 Hz; [movement.md](../rules/movement.md#timing-and-the-game-step) describes the port's timer tick and game step.

The divisor 4971 and the 120 Hz music tick are certain. The 60 Hz and 7.5 Hz game rates are unverified, because the real rate of the `INT 0x2d` callback is unknown ([Unknown values](#unknown-values)).

The code behind these rates:

- `FUN_00062efd` and `FUN_00062f43` in the HMI timer library write the byte `0x36` (channel 0, low/high byte, mode 3) to port `0x43`, then the divisor's low and high bytes to port `0x40`.
- `FUN_00010010` calls `FUN_0005ae04(0xf0, 0, 0)` (`sosTIMERInitSystem(240)`): divisor = `0x1234dc / 240` = 4971. It also registers a chain event to the old `INT 8` handler at the BIOS rate (`0xff00`).
- `FUN_0005af1a` (`sosTIMERRegisterEvent`) lowers the divisor only for a rate above the current rate. Registered rates: `0x78` (120 Hz, digital driver, `FUN_00010010`) and `song[+0x38]` (`sosMIDIStartSong`, `FUN_0005fed2`). `FUN_0005f52b` registers 1500 Hz, but nothing calls it.
- Every game song and `TEST.HMP` has `+0x38` = `0x78` (120 Hz) in its HMP header ([music.md](../formats/music.md#hmp-player)). The longest `TEST.HMP` track has 15211 delta ticks = 126.8 s at 120 Hz.
- `IBMIOP.EXE` (decompressed `0x2c58`): `mov word [bp-2], 0x136b` → `cs:[0x125e]`. Its `INT 8` handler at `0x2c73` writes `0x36`, then the divisor to ports `0x43` and `0x40`. Every 4th interrupt (`and al, 3`) it calls the registered far callback `[0xa9d0]`. That callback (`0x3007`) executes `int 0x2d`.
- In `SKULL.EXE`, `FUN_0005a8b1` sets vector `0x2d` to `FUN_00010cc2`, then calls `int 0xfc` with `EAX=0x0f`. `IBMIOP.EXE` function `0x0f` stores the callback and returns 1 (`mov ax,1; retf`), so `g_timer_step` (`0x7efac`) is 1: each callback adds 1 tick. `FUN_00010d0f` first calls `int 0xfc` function `0x2a`, which reinstalls the `IBMIOP.EXE` timer handler.
- The tick from the keyboard poll (function `0x02`) runs only with the `IBMIOP.EXE` option `+D`. The `DM2.BAT` of the SVGA install passes `+VS` ([SVGA option](#svga-option)), so the tick comes from the `INT 8` handler on every 4th interrupt.
- Function `0x30` (shutdown) clears the flag that function `0x0f` sets. After it, no more ticks arrive.
- `FUN_00013887` (frame wait) polls VGA port `0x3da` bit 3. It does not use the PIT.
- Game code does not read a clock: all `int 0x21` calls are in the Watcom library, and there is no `int 0x1a`. `g_game_time` (`0x7f22c`) counts game steps. Timeline event times are game steps (24 bits).

## Unknown values

Both programs hook `INT 8`: the HMI handler in protected mode, the `IBMIOP.EXE` handler in real mode. The HMI handler calls the old handler only at about 18.2 Hz. Thus the real rate of the `INT 0x2d` callback depends on how DOS/4GW routes `IRQ 0`. Static analysis cannot determine it.

## IBMIOP.EXE runtime layer

`DM2.BAT` starts `IBMIOP SKULL.EXE`. `IBMIOP.EXE` is a real-mode resident layer that `SKULL.EXE` calls through `int 0xfc` with a shared buffer (`DAT_0008048c`). It sets video mode `0x13`, writes the palette (port `0x3c8`), reads `skull.cfg`, handles the keyboard and timer, and runs the movie script. `SKULL.EXE` has 34 call sites with function numbers `0x00` to `0x30` and `0x103` to `0x10a` (for example `0x0f` timer callback, `0x2a` timer reinstall, `0x2e`, `0x2f`, `0x30` on startup and exit).

[skull-map.md](skull-map.md#ibmiop-int-0xfc-service-table) lists these `int 0xfc` functions. The platform layer of the port replaces them (video, palette, input, timer).
