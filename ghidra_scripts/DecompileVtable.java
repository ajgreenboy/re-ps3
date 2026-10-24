// Decompile the functions named by a vtable: reads N 32-bit entries at an address; each entry is either a
// code address or a PPC OPD (function descriptor) whose first word is the code address.
// Args: output-file vtable-address entry-count
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileVtable extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address table = toAddr(Long.decode(args[1]));
        int count = Integer.parseInt(args[2]);
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 0; i < count; i++) {
                Address entry = toAddr(getInt(table.add(i * 4L)) & 0xffffffffL);
                Function f = getFunctionAt(entry);
                Address code = null;
                if (f == null && entry.getOffset() != 0 && currentProgram.getMemory().contains(entry)) {
                    code = toAddr(getInt(entry) & 0xffffffffL); // OPD: first word is the code address
                    f = getFunctionAt(code);
                    if (f == null && currentProgram.getMemory().contains(code)) {
                        disassemble(code);
                        f = createFunction(code, null);
                    }
                }
                out.println("// vtable[" + i + "] -> " + entry + (code != null ? " code " + code : "") + (f == null ? " (no function)" : " " + f.getName()));
                if (f == null) continue;
                DecompileResults result = decompiler.decompileFunction(f, 120, monitor);
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println("// " + result.getErrorMessage());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
