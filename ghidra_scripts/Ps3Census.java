// Counts instruction mnemonics inside functions; appends "<program>,<mnemonic>,<count>" rows.
// Args: <output csv>
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class Ps3Census extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String, Integer> counts = new TreeMap<>();
        Listing listing = currentProgram.getListing();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                counts.merge(ins.getMnemonicString(), 1, Integer::sum);
            }
        }
        try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0], true))) {
            for (var e : counts.entrySet()) w.printf("%s,%s,%d%n", currentProgram.getName(), e.getKey(), e.getValue());
        }
        println("distinct mnemonics: " + counts.size());
    }
}
