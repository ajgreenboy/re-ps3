// Decompile every function whose entry lies in [start, end) (hex), from an analyzed local firmware project.
// Args: output-file start-hex end-hex
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileRange extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        var start = toAddr(Long.parseLong(args[1],16)); var end = toAddr(Long.parseLong(args[2],16));
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(start, true)) {
                if (f.getEntryPoint().compareTo(end) >= 0) break;
                DecompileResults r = decompiler.decompileFunction(f,30,monitor);
                out.println("// " + f.getName() + " @ " + f.getEntryPoint());
                out.println(r.decompileCompleted()?r.getDecompiledFunction().getC():"// "+r.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
