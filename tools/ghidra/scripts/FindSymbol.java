// SPDX-License-Identifier: GPL-3.0-or-later
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
public class FindSymbol extends GhidraScript {
    @Override public void run() throws Exception {
        String needle = getScriptArgs()[0];
        SymbolTable st = currentProgram.getSymbolTable();
        SymbolIterator it = st.getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.getName().equals(needle)) println(s.getAddress() + " " + s.getName() + " (" + s.getSymbolType() + ")");
        }
    }
}
