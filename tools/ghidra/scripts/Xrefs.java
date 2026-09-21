// Print every function that references a given address (data or code).
// Usage: Xrefs <addr-hex> [addr-hex ...]
//@category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashSet;
import java.util.Set;

public class Xrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) { println("usage: Xrefs <addr-hex> [addr-hex ...]"); return; }
        for (String a : args) {
            Address target = toAddr(Long.parseUnsignedLong(a.replace("0x", ""), 16));
            println("=== refs to " + target + " ===");
            ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(target);
            Set<String> seen = new LinkedHashSet<>();
            while (it.hasNext()) {
                if (monitor.isCancelled()) break;
                Reference r = it.next();
                Address from = r.getFromAddress();
                Function fn = getFunctionContaining(from);
                String label = fn != null ? fn.getName() + " @ " + fn.getEntryPoint() : "(no fn)";
                seen.add(from + "  " + r.getReferenceType() + "  " + label);
            }
            for (String s : seen) println("  " + s);
            println("");
        }
    }
}
