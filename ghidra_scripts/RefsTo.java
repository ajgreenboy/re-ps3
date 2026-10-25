// Functions referencing each given address (hex), from an analyzed local firmware project.
// Args: output-file address [...]
// @category XMB
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class RefsTo extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i=1;i<args.length;i++) {
                var address = toAddr(Long.parseLong(args[i],16));
                for (Reference ref : getReferencesTo(address)) {
                    Function f = getFunctionContaining(ref.getFromAddress());
                    out.println(args[i] + " <- " + ref.getFromAddress() + " " + (f==null?"?":f.getName()));
                }
            }
        }
    }
}
