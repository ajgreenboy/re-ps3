// Decompile the functions containing (or starting at, creating them if needed) the given addresses (hex).
// Args: output-file address [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileAt extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("// image base " + currentProgram.getImageBase());
            for (int i=1;i<args.length;i++) {
                var a = toAddr(Long.parseLong(args[i],16));
                Function f = getFunctionAt(a);
                if (f == null) { disassemble(a); f = createFunction(a, null); }
                if (f == null) f = getFunctionContaining(a);
                if (f == null) { out.println("// no function at " + a); continue; }
                DecompileResults r = decompiler.decompileFunction(f,60,monitor);
                out.println("// " + f.getName() + " @ " + f.getEntryPoint());
                out.println(r.decompileCompleted()?r.getDecompiledFunction().getC():"// "+r.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
