// SPDX-License-Identifier: GPL-3.0-or-later
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.Function;
import java.io.*;
public class XrefsToSym extends GhidraScript {
    @Override public void run() throws Exception {
        String needle = getScriptArgs()[0];
        String outf   = getScriptArgs()[1];
        SymbolTable st = currentProgram.getSymbolTable();
        SymbolIterator it = st.getAllSymbols(true);
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(outf)))) {
            while (it.hasNext()) {
                Symbol s = it.next();
                if (!s.getName().equals(needle)) continue;
                w.printf("// symbol %s @ %s%n", needle, s.getAddress());
                for (Reference r : s.getReferences()) {
                    Address from = r.getFromAddress();
                    Function fn = currentProgram.getFunctionManager().getFunctionContaining(from);
                    w.printf("%s -> %s  in %s%n", from, s.getAddress(),
                             fn != null ? fn.getName()+" @ "+fn.getEntryPoint() : "(no func)");
                }
            }
        }
    }
}
