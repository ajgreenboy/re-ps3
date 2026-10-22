// Resolves PS3 PRX imports (FNID stubs) for an ELF/PRX loaded as PowerPC:BE:64:*-32addr.
// NID names come from an RPCS3 source tree: REG_FUNC/REG_FNID/REG_VAR in Emu/Cell/Modules
// plus the switch tables in Emu/Cell/PPUFunction.cpp. Unknown NIDs get <module>_<nid>.
// Args: <rpcs3 source root> <output csv>
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.*;

public class Ps3ResolveImports extends GhidraScript {
    static final byte[] NID_SUFFIX = {0x67, 0x59, 0x65, (byte) 0x99, 0x04, 0x25, 0x04, (byte) 0x90,
        0x56, 0x64, 0x27, 0x49, (byte) 0x94, (byte) 0x89, 0x74, 0x1A};

    // module -> nid -> name; "" module holds names valid for any module
    final Map<String, Map<Long, String>> db = new HashMap<>();

    static long nid(String name) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        md.update(name.getBytes("UTF-8"));
        md.update(NID_SUFFIX);
        byte[] h = md.digest();
        return (h[0] & 0xffL) | (h[1] & 0xffL) << 8 | (h[2] & 0xffL) << 16 | (h[3] & 0xffL) << 24;
    }

    void put(String module, long id, String name) {
        db.computeIfAbsent(module, k -> new HashMap<>()).putIfAbsent(id, name);
    }

    void loadRpcs3(Path root) throws Exception {
        Pattern declare = Pattern.compile("DECLARE\\(ppu_module_manager::(\\w+)\\)\\(\"([^\"]+)\"");
        Pattern regFunc = Pattern.compile("REG_(?:FUNC|VAR)\\((\\w+),\\s*(\\w+)\\)");
        Pattern regFnid = Pattern.compile("REG_(?:FNID|VNID)\\((\\w+),\\s*(0x[0-9A-Fa-f]+|\"[^\"]+\"),\\s*(\\w+)\\)");
        Map<String, String> varToModule = new HashMap<>();
        List<Path> files = new ArrayList<>();
        try (var s = Files.walk(root.resolve("rpcs3/Emu/Cell/Modules"))) {
            s.filter(p -> p.toString().endsWith(".cpp")).forEach(files::add);
        }
        for (Path f : files) {
            String text = Files.readString(f);
            Matcher m = declare.matcher(text);
            while (m.find()) varToModule.put(m.group(1), m.group(2));
        }
        for (Path f : files) {
            String text = Files.readString(f);
            Matcher m = regFunc.matcher(text);
            while (m.find()) {
                String mod = varToModule.getOrDefault(m.group(1), m.group(1));
                put(mod, nid(m.group(2)), m.group(2));
            }
            m = regFnid.matcher(text);
            while (m.find()) {
                String mod = varToModule.getOrDefault(m.group(1), m.group(1));
                String idText = m.group(2);
                long id = idText.startsWith("\"") ? nid(idText.substring(1, idText.length() - 1)) : Long.parseLong(idText.substring(2), 16);
                put(mod, id, m.group(3));
            }
        }
        // PPUFunction.cpp: if (_module == "a" || _module == "b") switch (...) { case 0x...: return "name"; }
        Pattern ifMod = Pattern.compile("_module\\s*==\\s*\"([^\"]+)\"");
        Pattern caseRet = Pattern.compile("case (0x[0-9a-fA-F]+): return \"([^\"]+)\"");
        List<String> current = List.of("");
        for (String line : Files.readAllLines(root.resolve("rpcs3/Emu/Cell/PPUFunction.cpp"))) {
            if (line.contains("_module ==") || line.contains("_module.empty()")) {
                List<String> mods = new ArrayList<>();
                Matcher m = ifMod.matcher(line);
                while (m.find()) mods.add(m.group(1));
                current = mods.isEmpty() ? List.of("") : mods;
            }
            Matcher c = caseRet.matcher(line);
            if (c.find()) for (String mod : current) put(mod, Long.parseLong(c.group(1).substring(2), 16), c.group(2));
        }
    }

    String lookup(String module, long id) {
        Map<Long, String> m = db.get(module);
        if (m != null && m.containsKey(id)) return m.get(id);
        m = db.get("");
        if (m != null && m.containsKey(id)) return m.get(id);
        return null;
    }

    long u32(Address a) throws Exception { return currentProgram.getMemory().getInt(a) & 0xffffffffL; }
    Address addr(long off) { return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(off); }

    String cstr(long off) throws Exception {
        StringBuilder sb = new StringBuilder();
        Memory mem = currentProgram.getMemory();
        for (int i = 0; i < 128; i++) {
            byte b = mem.getByte(addr(off + i));
            if (b == 0) break;
            sb.append((char) b);
        }
        return sb.toString();
    }

    boolean mapped(long off) { return off != 0 && currentProgram.getMemory().contains(addr(off)); }

    // Import records (ppu_prx_module_info, size 0x2c): the executable's prx param header is
    // empty for vsh, so scan for records whose name/nids/addrs pointers all validate.
    List<Address> findImportTables(Memory mem) throws Exception {
        List<Address> out = new ArrayList<>();
        // size 0x2c, unk0 0, version 0x0000 or 0x0001 (masked low bit)
        byte[] sig = {0x2c, 0x00, 0x00, 0x00};
        byte[] mask = {(byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xfe};
        Address a = currentProgram.getMinAddress();
        while ((a = mem.findBytes(a, sig, mask, true, monitor)) != null) {
            try {
                int numFunc = mem.getShort(a.add(6)) & 0xffff;
                long name = u32(a.add(0x10)), nids = u32(a.add(0x14)), addrs = u32(a.add(0x18));
                if (numFunc > 0 && numFunc < 4096 && mapped(name) && mapped(nids) && mapped(addrs)) {
                    String s = cstr(name);
                    if (!s.isEmpty() && s.matches("[A-Za-z0-9_]+")) out.add(a);
                }
            } catch (MemoryAccessException e) {
                // unmapped pointer target; not a record
            }
            a = a.add(4);
        }
        return out;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        loadRpcs3(Paths.get(args[0]));
        int known = db.values().stream().mapToInt(Map::size).sum();
        println("NID database entries: " + known);

        Memory mem = currentProgram.getMemory();
        List<Address> libs = findImportTables(mem);
        println("import module records: " + libs.size());

        SymbolTable st = currentProgram.getSymbolTable();
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        int total = 0, named = 0, stubs = 0;
        String outPath = args[1];
        if (new File(outPath).isDirectory()) outPath = new File(outPath, currentProgram.getName() + "_imports.csv").getPath();
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("module,nid,name,resolved,slot,stub");
            for (Address lib : libs) {
                int numFunc = mem.getShort(lib.add(6)) & 0xffff;
                String module = cstr(u32(lib.add(0x10)));
                long nids = u32(lib.add(0x14)), addrs = u32(lib.add(0x18));
                for (int i = 0; i < numFunc; i++) {
                    long id = u32(addr(nids + 4L * i));
                    Address slot = addr(addrs + 4L * i);
                    String name = lookup(module, id);
                    boolean resolved = name != null;
                    if (!resolved) name = String.format("%s_%08X", module, id);
                    String sym = module + "::" + name;
                    total++;
                    if (resolved) named++;
                    st.createLabel(slot, "__imp_" + module + "_" + name, SourceType.IMPORTED);
                    String stubAt = "";
                    for (Reference r : rm.getReferencesTo(slot)) {
                        Function f = fm.getFunctionContaining(r.getFromAddress());
                        if (f != null && f.getBody().getNumAddresses() <= 64) {
                            f.setName(module + "_" + name, SourceType.IMPORTED);
                            stubAt = f.getEntryPoint().toString();
                            stubs++;
                            break;
                        }
                    }
                    w.printf("%s,0x%08X,%s,%s,%s,%s%n", module, id, name, resolved, slot, stubAt);
                }
            }
        }
        println(String.format("imports: %d, named from RPCS3: %d, stubs renamed: %d", total, named, stubs));
    }
}
