// Disassembly listing of an address range (disassembling undefined bytes first, in a read-only session), with the
// float value and string at each data reference. Args: output-file start-hex end-hex
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ListRange extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        var start = toAddr(Long.parseLong(args[1],16)); var end = toAddr(Long.parseLong(args[2],16));
        disassemble(start);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (var a = start; a.compareTo(end) < 0; ) {
                Instruction ins = getInstructionAt(a);
                if (ins == null) { disassemble(a); ins = getInstructionAt(a); }
                if (ins == null) { out.println(a + ": ?? " + Integer.toHexString(getInt(a))); a = a.add(4); continue; }
                StringBuilder line = new StringBuilder(a + ": " + ins);
                for (Reference ref : ins.getReferencesFrom()) {
                    var to = ref.getToAddress();
                    try { int bits = getInt(to); line.append("   ; [" + to + "] = 0x" + Integer.toHexString(bits) + " (" + Float.intBitsToFloat(bits) + ")"); } catch (Exception e) { line.append("   ; [" + to + "]"); }
                    Data d = getDataAt(to); if (d != null && d.hasStringValue()) line.append(" \"" + d.getValue() + "\"");
                    Function f = getFunctionAt(to); if (f != null) line.append(" <" + f.getName() + ">");
                }
                out.println(line); a = a.add(4);
            }
        }
    }
}
