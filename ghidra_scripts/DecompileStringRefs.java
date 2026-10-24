// Decompile every function that references one of the given strings, directly or through a PPC TOC entry
// (code loads r2+offset -> TOC slot -> string). Works on an already analyzed local firmware project.
// Args: output-file string [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class DecompileStringRefs extends GhidraScript {
    private final Set<Function> functions = new LinkedHashSet<>();
    private final Map<Function, Set<String>> why = new HashMap<>();

    private void collect(Address target, String label, int depth) {
        for (Reference ref : getReferencesTo(target)) {
            Address from = ref.getFromAddress();
            Function f = getFunctionContaining(from);
            if (f != null) {
                functions.add(f);
                why.computeIfAbsent(f, k -> new TreeSet<>()).add(label);
            } else if (depth < 2) {
                collect(from, label, depth + 1); // TOC slot or pointer table holding the string address
            }
        }
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        Memory memory = currentProgram.getMemory();
        for (int i = 1; i < args.length; i++) {
            byte[] needle = (args[i] + "\0").getBytes(StandardCharsets.US_ASCII);
            Address at = memory.getMinAddress();
            while (at != null) {
                at = memory.findBytes(at, needle, null, true, monitor);
                if (at == null) break;
                collect(at, args[i], 0);
                at = at.add(1);
            }
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (Function f : functions) {
                out.println("// " + f.getName() + " @ " + f.getEntryPoint() + " uses " + why.get(f));
                DecompileResults result = decompiler.decompileFunction(f, 120, monitor);
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println("// " + result.getErrorMessage());
            }
        } finally {
            decompiler.dispose();
        }
        println("decompiled " + functions.size() + " functions");
    }
}
