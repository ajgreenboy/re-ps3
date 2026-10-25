// Functions referencing symbols whose name contains any of the given substrings, from an analyzed local project.
// Args: output-file substring [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class SymbolRefs extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(true)) {
                boolean hit=false; for (int i=1;i<args.length;i++) if (s.getName().contains(args[i])) hit=true;
                if (!hit) continue;
                out.println(s.getName()+" @ "+s.getAddress());
                for (Reference r : getReferencesTo(s.getAddress())) { Function f=getFunctionContaining(r.getFromAddress()); out.println("   <- "+r.getFromAddress()+" "+(f==null?"?":f.getName()+" @ "+f.getEntryPoint())); }
            }
        }
    }
}
