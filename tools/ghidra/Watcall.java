// Add the Watcom register calling convention __watcall and set it on every function.
// Arguments go in EAX, EDX, EBX, ECX, the rest on the stack; the callee removes stack arguments.
// EBX, ECX, EDX are not in the unaffected list: the decompiler does not show a preserved register as a parameter.
// Usage: -postScript Watcall.java (after auto-analysis and ApplySymbols.java, before ExportC.java)
import ghidra.app.script.GhidraScript;
import ghidra.program.database.SpecExtension;
import ghidra.program.model.listing.Function;

public class Watcall extends GhidraScript {
    private static final String PROTOTYPE = """
            <prototype name="__watcall" extrapop="unknown" stackshift="4">
              <input>
                <pentry minsize="1" maxsize="4"><register name="EAX"/></pentry>
                <pentry minsize="1" maxsize="4"><register name="EDX"/></pentry>
                <pentry minsize="1" maxsize="4"><register name="EBX"/></pentry>
                <pentry minsize="1" maxsize="4"><register name="ECX"/></pentry>
                <pentry minsize="1" maxsize="500" align="4"><addr offset="4" space="stack"/></pentry>
              </input>
              <output killedbycall="true">
                <pentry minsize="4" maxsize="10" metatype="float" extension="float"><register name="ST0"/></pentry>
                <pentry minsize="1" maxsize="4"><register name="EAX"/></pentry>
                <pentry minsize="5" maxsize="8"><addr space="join" piece1="EDX" piece2="EAX"/></pentry>
              </output>
              <unaffected>
                <register name="ESP"/>
                <register name="EBP"/>
                <register name="ESI"/>
                <register name="EDI"/>
              </unaffected>
              <killedbycall>
                <register name="ST0"/>
                <register name="ST1"/>
              </killedbycall>
            </prototype>
            """;

    @Override
    public void run() throws Exception {
        new SpecExtension(currentProgram).addReplaceCompilerSpecExtension(PROTOTYPE, monitor);
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            f.setCallingConvention("__watcall");
            count++;
        }
        println("Watcall: " + count + " functions");
    }
}
