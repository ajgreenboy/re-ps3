// For each caller of FUN_0033fc30 (the packed-id layout resolver: factor[id>>16]*v + grid[id&0xffff]),
// capture the argument setup: the integer `id` (GPR arg) and the float `v` (FPR arg), plus the tail of the
// basic block so non-literal cases can be read by hand.
// Args: output-file target-hex
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.address.Address;
import java.io.*;

public class CallerArgs extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address target = toAddr(Long.parseLong(args[1], 16));
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (Reference ref : getReferencesTo(target)) {
                Address from = ref.getFromAddress();
                Function f = getFunctionContaining(from);
                out.println("CALLER " + from + "  fn=" + (f == null ? "?" : f.getName()));

                // Walk back up to 24 instructions, capture the last writer of r3 / r4 and any float load to f1.
                Integer r3 = null, r4 = null;
                String r3how = null, r4how = null, f1 = null;
                Address a = from;
                for (int back = 1; back <= 24; back++) {
                    a = a.subtract(4);
                    Instruction ins = getInstructionAt(a);
                    if (ins == null) continue;
                    String mn = ins.getMnemonicString();
                    Object[] ops0 = ins.getOpObjects(0);

                    // integer literal into a GPR
                    if ((mn.equals("li") || mn.equals("addi") || mn.equals("lis") || mn.equals("addis") || mn.equals("ori")) && ops0.length > 0) {
                        String reg = ops0[0].toString();
                        if (r3 == null && reg.equals("r3")) { r3how = classify(ins); r3 = intImm(ins); }
                        if (r4 == null && reg.equals("r4")) { r4how = classify(ins); r4 = intImm(ins); }
                    }
                    // float load into f1
                    if ((mn.equals("lfs") || mn.equals("lfd")) && ops0.length > 0 && ops0[0].toString().equals("f1")) {
                        if (f1 == null) {
                            Object[] ops1 = ins.getOpObjects(1);
                            String operand = ins.toString();
                            Address refTo = null;
                            for (Reference r : ins.getReferencesFrom()) { refTo = r.getToAddress(); }
                            if (refTo != null) {
                                try {
                                    int bits = getInt(refTo);
                                    f1 = "mem[" + refTo + "]=" + Float.intBitsToFloat(bits);
                                } catch (Exception e) { f1 = "mem[" + refTo + "]"; }
                            } else { f1 = "lfs " + operand; }
                        }
                    }
                    // stop early if both r3 and r4 are known and we already saw the bl's own context
                }
                out.println("  r3(id?) = " + (r3 == null ? "?" : r3 + " (0x" + Integer.toHexString(r3) + ") via " + r3how));
                out.println("  r4      = " + (r4 == null ? "?" : r4 + " (0x" + Integer.toHexString(r4) + ") via " + r4how));
                out.println("  f1(v?)  = " + (f1 == null ? "?" : f1));

                // tail disassembly for manual inspection
                Address b = from.subtract(16);
                for (int k = 0; k < 8; k++) {
                    Instruction ins = getInstructionAt(b);
                    out.println("      " + b + ": " + (ins == null ? "??" : ins.toString()));
                    b = b.add(4);
                }
            }
        }
    }
    private String classify(Instruction ins) {
        String mn = ins.getMnemonicString();
        Object[] ops = ins.getOpObjects(1);
        if (mn.equals("li")) return "li";
        if (mn.equals("lis")) return "lis";
        if (mn.equals("addi")) return ops.length > 0 ? "addi(" + ops[0] + ")" : "addi";
        if (mn.equals("addis")) return ops.length > 0 ? "addis(" + ops[0] + ")" : "addis";
        if (mn.equals("ori")) return "ori";
        return mn;
    }
    private Integer intImm(Instruction ins) {
        try {
            Object[] ops1 = ins.getOpObjects(1);
            if (ops1.length > 0 && ops1[0] instanceof Scalar) {
                long v = ((Scalar) ops1[0]).getValue();
                return (int) v;
            }
        } catch (Exception e) {}
        return null;
    }
}
