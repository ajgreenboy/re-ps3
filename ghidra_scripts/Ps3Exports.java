// Lists PS3 PRX exports (libent records, size 0x1c) and labels the exported functions.
// Exported functions are named <module>_<NID> unless they already carry a non-default name.
// Args: <output csv or directory>
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class Ps3Exports extends GhidraScript {
    long u32(Address a) throws Exception { return currentProgram.getMemory().getInt(a) & 0xffffffffL; }
    Address addr(long off) { return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(off); }
    boolean mapped(long off) { return off != 0 && currentProgram.getMemory().contains(addr(off)); }

    String cstr(long off) throws Exception {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 128; i++) {
            byte b = currentProgram.getMemory().getByte(addr(off + i));
            if (b == 0) break;
            sb.append((char) b);
        }
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        if (new File(outPath).isDirectory()) outPath = new File(outPath, currentProgram.getName() + "_exports.csv").getPath();
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        // size 0x1c, unk0 0, any version
        byte[] sig = {0x1c, 0x00};
        int libs = 0, funcs = 0;
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("module,nid,opd,func,toc,name");
            Address a = currentProgram.getMinAddress();
            while ((a = mem.findBytes(a, sig, null, true, monitor)) != null) {
                Address rec = a;
                a = a.add(4);
                if ((rec.getOffset() & 3) != 0) continue;
                try {
                    int attr = mem.getShort(rec.add(4)) & 0xffff;
                    int numFunc = mem.getShort(rec.add(6)) & 0xffff;
                    long nameP = u32(rec.add(0x10)), nids = u32(rec.add(0x14)), addrs = u32(rec.add(0x18));
                    // attr 0x0001 = library export; management records (module_start etc.) have no name
                    if ((attr & 1) == 0 || numFunc == 0 || numFunc > 8192 || !mapped(nameP) || !mapped(nids) || !mapped(addrs)) continue;
                    String module = cstr(nameP);
                    if (module.isEmpty() || !module.matches("[A-Za-z0-9_]+")) continue;
                    libs++;
                    for (int i = 0; i < numFunc; i++) {
                        long nid = u32(addr(nids + 4L * i));
                        long opd = u32(addr(addrs + 4L * i));
                        if (!mapped(opd)) continue;
                        long code = u32(addr(opd)), toc = u32(addr(opd + 4));
                        String name = "";
                        if (mapped(code)) {
                            Function f = fm.getFunctionAt(addr(code));
                            if (f == null) {
                                // Export discovery runs after initial auto-analysis. Seed instructions
                                // before creating a function so its body is not a one-byte placeholder.
                                disassemble(addr(code));
                                f = createFunction(addr(code), null);
                            }
                            if (f != null) {
                                if (f.getName().startsWith("FUN_")) f.setName(String.format("%s_%08X", module, nid), SourceType.IMPORTED);
                                name = f.getName();
                            }
                        }
                        w.printf("%s,0x%08X,0x%08X,0x%08X,0x%08X,%s%n", module, nid, opd, code, toc, name);
                        funcs++;
                    }
                } catch (MemoryAccessException e) {
                    // not a record
                }
            }
        }
        println(String.format("export libraries: %d, exported functions: %d", libs, funcs));
    }
}
