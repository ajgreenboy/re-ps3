// Dumps function/segment stats and the function list for the current program.
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;

public class DumpStats extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "stats.txt";
        if (new File(out).isDirectory()) out = new File(out, currentProgram.getName() + "_stats.txt").getPath();
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            w.println("program " + currentProgram.getName() + " lang " + currentProgram.getLanguageID());
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                w.println("block " + b.getName() + " " + b.getStart() + "-" + b.getEnd() + " x=" + b.isExecute() + " w=" + b.isWrite());
            }
            FunctionManager fm = currentProgram.getFunctionManager();
            w.println("functions " + fm.getFunctionCount());
            long named = 0;
            for (Function f : fm.getFunctions(true)) {
                if (!f.getName().startsWith("FUN_")) named++;
                w.println("fn " + f.getEntryPoint() + " " + f.getBody().getNumAddresses() + " " + f.getName());
            }
            w.println("named " + named);
        }
    }
}
