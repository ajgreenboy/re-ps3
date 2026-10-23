// Decompile selected functions from an already analyzed local firmware project.
// Args: output-file name-substring [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileNamed extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
                boolean selected = false;
                for (int i=1;i<args.length;i++) if (function.getName().contains(args[i])) selected=true;
                if (!selected) continue;
                DecompileResults result = decompiler.decompileFunction(function,60,monitor);
                out.println("// " + function.getName() + " @ " + function.getEntryPoint());
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println("// " + result.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
