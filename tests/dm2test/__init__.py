"""The helpers of the tests. The modules import in levels: a module imports only from lower levels or from earlier
modules of its own level, in this order. No function-level import.

0. constants, structs, markers, lib, gamefiles, iso, addresses
1. save, gdat, view, oracle, audio
2. scenario
3. interface, input
4. dialog, modes, control, creature
"""
