// Remove functions that auto-analysis created in non-executable memory (function pointer variables).
// Create a function at each 4-byte aligned pointer in the data block that points to code outside all functions.
// Targets that start with NOP (0x90) or zero bytes are small integers, not pointers, and are skipped.
// Usage: -postScript FindFunctions.java (after auto-analysis)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class FindFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock code = mem.getBlock("code");
        MemoryBlock data = mem.getBlock("data");
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!code.contains(f.getEntryPoint())) {
                removeFunction(f);
            }
        }
        int count = 0;
        for (Address a = data.getStart(); a.add(3).compareTo(data.getEnd()) <= 0; a = a.add(4)) {
            Address t = toAddr(mem.getInt(a) & 0xffffffffL);
            if (!code.contains(t) || getFunctionContaining(t) != null) {
                continue;
            }
            byte b = mem.getByte(t);
            if (b == (byte) 0x90 || b == 0) {
                continue;
            }
            disassemble(t);
            if (createFunction(t, null) != null) {
                count++;
            }
        }
        println("FindFunctions: " + count + " functions");
    }
}
