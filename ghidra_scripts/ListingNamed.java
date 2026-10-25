// Disassembly listing of selected functions (by exact name) from an analyzed local firmware project, with the
// float/double value of every data reference, for reading vector code the decompiler renders poorly.
// Args: output-file function-name [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import java.io.*;

public class ListingNamed extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Memory memory = currentProgram.getMemory();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
                boolean selected = false;
                for (int i=1;i<args.length;i++) if (function.getName().equals(args[i])) selected=true;
                if (!selected) continue;
                out.println("// " + function.getName() + " @ " + function.getEntryPoint());
                for (Instruction ins : currentProgram.getListing().getInstructions(function.getBody(), true)) {
                    StringBuilder line = new StringBuilder(ins.getAddress() + ": " + ins);
                    for (Reference ref : ins.getReferencesFrom()) {
                        if (!ref.getReferenceType().isData()) continue;
                        Address a = ref.getToAddress();
                        try {
                            int bits = memory.getInt(a);
                            line.append("   ; [" + a + "] = 0x" + Integer.toHexString(bits) + " (" + Float.intBitsToFloat(bits) + ")");
                        } catch (Exception e) { line.append("   ; [" + a + "]"); }
                    }
                    out.println(line);
                }
                out.println();
            }
        }
    }
}
