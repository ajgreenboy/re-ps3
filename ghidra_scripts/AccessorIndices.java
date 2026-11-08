// List every literal (li) index passed to the layout-table accessors at each call site.
// Args: output-file address ...   (e.g. 33fbb8 33fbf4 33fc30)
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.address.Address;
import java.io.*;

public class AccessorIndices extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i=1;i<args.length;i++) {
                var target = toAddr(Long.parseLong(args[i],16));
                String kind = args[i].toLowerCase();
                for (Reference ref : getReferencesTo(target)) {
                    var from = ref.getFromAddress();
                    Function f = getFunctionContaining(from);
                    Integer idx = findLiR3(from);
                    out.println(kind + "\t" + (idx==null?"?":idx) + "\t" + from + "\t" + (f==null?"?":f.getName()));
                }
            }
        }
    }
    // Walk back up to 6 instructions from `from` for `li r3, imm`.
    private Integer findLiR3(Address from) throws Exception {
        for (int back=1; back<=6; back++) {
            Address a = from.subtract(back*4);
            Instruction ins = getInstructionAt(a);
            if (ins==null) continue;
            if (!(ins.getMnemonicString().equals("li") || ins.getMnemonicString().equals("addi"))) continue;
            Object[] ops0 = ins.getOpObjects(0);
            if (ops0.length==0 || !ops0[0].toString().equals("r3")) continue;
            Object[] ops1 = ins.getOpObjects(1);
            if (ops1.length>0 && ops1[0] instanceof Scalar) {
                long v = ((Scalar)ops1[0]).getValue();
                return (int)v;
            }
        }
        return null;
    }
}
