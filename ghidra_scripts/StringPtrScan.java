// For each string: its addresses, every 4-byte big-endian pointer to it anywhere in memory, and the functions
// referencing those pointer slots (or the string). Read-only on an analyzed local firmware project.
// Args: output-file string [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;

public class StringPtrScan extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Memory memory = currentProgram.getMemory();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                if (args[i].startsWith("@")) { // raw address: pointer scan only
                    Address at0 = toAddr(Long.parseLong(args[i].substring(1), 16));
                    out.println("ADDR " + at0);
                    for (Reference r : getReferencesTo(at0)) report(out, "  ref", r.getFromAddress());
                    long v0 = at0.getOffset();
                    byte[] p0 = new byte[]{(byte)(v0 >> 24), (byte)(v0 >> 16), (byte)(v0 >> 8), (byte)v0};
                    Address q0 = memory.getMinAddress();
                    while (q0 != null) {
                        q0 = memory.findBytes(q0, p0, null, true, monitor);
                        if (q0 == null) break;
                        report(out, "  ptr", q0);
                        for (Reference r : getReferencesTo(q0)) report(out, "    ref", r.getFromAddress());
                        q0 = q0.add(1);
                    }
                    continue;
                }
                byte[] needle = (args[i] + "\0").getBytes(StandardCharsets.US_ASCII);
                Address at = memory.getMinAddress();
                while (at != null) {
                    at = memory.findBytes(at, needle, null, true, monitor);
                    if (at == null) break;
                    out.println("STRING " + args[i] + " @ " + at);
                    for (Reference r : getReferencesTo(at)) report(out, "  ref", r.getFromAddress());
                    long v = at.getOffset();
                    byte[] p = new byte[]{(byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v};
                    Address q = memory.getMinAddress();
                    while (q != null) {
                        q = memory.findBytes(q, p, null, true, monitor);
                        if (q == null) break;
                        report(out, "  ptr", q);
                        for (Reference r : getReferencesTo(q)) report(out, "    ref", r.getFromAddress());
                        q = q.add(1);
                    }
                    at = at.add(1);
                }
            }
        }
    }
    private void report(PrintWriter out, String tag, Address a) {
        Function f = getFunctionContaining(a);
        out.println(tag + " " + a + (f != null ? " in " + f.getName() + " @ " + f.getEntryPoint() : ""));
    }
}
