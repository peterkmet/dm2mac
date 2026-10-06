// Split the flat image into a code block (read, execute) and a data block (read, write) and mark the entry points.
// Usage: -preScript Layout.java <data-start> <entry> [<entry> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class Layout extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Memory mem = currentProgram.getMemory();
        Address data = toAddr(Long.decode(args[0]));
        mem.split(mem.getBlock(data), data);
        MemoryBlock code = mem.getBlock(mem.getMinAddress());
        code.setName("code");
        code.setPermissions(true, false, true);
        MemoryBlock dataBlock = mem.getBlock(data);
        dataBlock.setName("data");
        dataBlock.setPermissions(true, true, false);
        for (int i = 1; i < args.length; i++) {
            Address entry = toAddr(Long.decode(args[i]));
            currentProgram.getSymbolTable().addExternalEntryPoint(entry);
            disassemble(entry);
            createFunction(entry, null);
        }
    }
}
