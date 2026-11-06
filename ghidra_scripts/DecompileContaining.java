// Decompile the function containing each given address (e.g. a return address from a runtime backtrace). When the
// project has no function there, walk back to the nearest `stdu r1,-N(r1)` prologue and create one (read-only session).
// Args: output-file address [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileContaining extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i=1;i<args.length;i++) {
                var a = toAddr(Long.parseLong(args[i],16));
                Function f = getFunctionContaining(a);
                if (f == null) {
                    for (var p = a; p.getOffset() > a.getOffset()-0x4000; p = p.subtract(4)) {
                        if (getInstructionAt(p) == null) disassemble(p);
                        Instruction ins = getInstructionAt(p);
                        if (ins != null && ins.getMnemonicString().equals("stdu") && ins.toString().contains("r1,")) {
                            // the prologue's mflr usually sits one or two words before stdu
                            var s = p;
                            for (int k=1;k<=3;k++) { Instruction q = getInstructionAt(p.subtract(4*k)); if (q!=null && q.getMnemonicString().equals("mfspr")) s = p.subtract(4*k); }
                            f = createFunction(s, null);
                            break;
                        }
                    }
                }
                if (f == null) { out.println("// no function containing " + a); continue; }
                DecompileResults r = decompiler.decompileFunction(f,60,monitor);
                out.println("// " + f.getName() + " @ " + f.getEntryPoint() + " (contains " + a + ")");
                out.println(r.decompileCompleted()?r.getDecompiledFunction().getC():"// "+r.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
