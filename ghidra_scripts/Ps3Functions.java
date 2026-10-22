// Writes "<entry>,<start>-<end>;<start>-<end>...,<name>" per function (hex, Ghidra addresses).
// Args: <output csv or directory>
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class Ps3Functions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        if (new File(outPath).isDirectory()) outPath = new File(outPath, currentProgram.getName() + "_functions.csv").getPath();
        int n = 0;
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("entry,ranges,name");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.isThunk() && f.getBody().getNumAddresses() == 0) continue;
                StringBuilder r = new StringBuilder();
                for (AddressRange ar : f.getBody()) {
                    if (r.length() > 0) r.append(';');
                    r.append(String.format("%08X-%08X", ar.getMinAddress().getOffset(), ar.getMaxAddress().getOffset()));
                }
                w.printf("%08X,%s,%s%n", f.getEntryPoint().getOffset(), r, f.getName());
                n++;
            }
        }
        println("functions written: " + n);
    }
}
