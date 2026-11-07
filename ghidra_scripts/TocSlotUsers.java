// Functions that load the given TOC slots with `lwz/ld rD, d(r2)`. TOC bases come from the .opd entries.
// Args: output-file slot-address-hex [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class TocSlotUsers extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Memory memory = currentProgram.getMemory();
        Map<Long,Integer> tocs = new HashMap<>();
        MemoryBlock opd = memory.getBlock(".opd");
        if (opd != null) {
            for (Address a = opd.getStart(); a.compareTo(opd.getEnd()) < 0; a = a.add(8)) {
                long t = memory.getInt(a.add(4)) & 0xffffffffL;
                if (t != 0) tocs.merge(t, 1, Integer::sum);
            }
        }
        ghidra.program.model.lang.Register r2 = currentProgram.getRegister("r2");
        int n = 0;
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            ghidra.program.model.lang.RegisterValue rv = currentProgram.getProgramContext().getRegisterValue(r2, fn.getEntryPoint());
            if (rv != null && rv.hasValue()) tocs.merge(rv.getUnsignedValue().longValue() & 0xffffffffL, 1, Integer::sum);
            if (++n > 20000) break;
        }
        StringBuilder blocks = new StringBuilder();
        for (MemoryBlock b : memory.getBlocks()) blocks.append(b.getName()).append(" ").append(b.getStart()).append("-").append(b.getEnd()).append("; ");
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("blocks " + blocks); out.println("TOC bases " + tocs);
            for (int i = 1; i < args.length; i++) {
                long slot = Long.parseLong(args[i], 16);
                for (long toc : tocs.keySet()) {
                    long d = slot - toc;
                    if (d < -32768 || d > 32767) continue;
                    // lwz rD,d(r2): 0x80000000 | rD<<21 | 2<<16 | d   (mask out rD)
                    for (int op : new int[]{32, 58}) {
                        int word = (op << 26) | (2 << 16) | ((int) d & 0xffff);
                        if (op == 58) word &= ~3; // ld: DS form, low bits 0
                        byte[] b = {(byte)(word>>>24),(byte)(word>>>16),(byte)(word>>>8),(byte)word};
                        byte[] m = {(byte)0xfc,(byte)0x1f,(byte)0xff,(byte)(op==58?0xff:0xff)};
                        Address q = memory.getMinAddress();
                        while (q != null) {
                            q = memory.findBytes(q, b, m, true, monitor);
                            if (q == null) break;
                            Function f = getFunctionContaining(q);
                            out.println("slot " + args[i] + " toc " + Long.toHexString(toc) + " d " + d + " at " + q + (f != null ? " in " + f.getName() + " @ " + f.getEntryPoint() : " (no function)"));
                            q = q.add(4);
                        }
                    }
                }
            }
        }
    }
}
