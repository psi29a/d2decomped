// SPDX-License-Identifier: GPL-3.0-or-later
// List every reference TO a target address — code refs, data refs,
// callers, whatever the reference database knows. Args:
//   <target-hex> <output.txt>
//@category References

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.Function;

import java.io.*;

public class XrefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            println("usage: XrefsTo <target-hex> <output.txt>");
            return;
        }
        Address target = toAddr(Long.parseUnsignedLong(args[0].replace("0x", ""), 16));
        File out = new File(args[1]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();
        int count = 0;
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            w.printf("// xrefs to %s%n%n", target);
            ReferenceManager rm = currentProgram.getReferenceManager();
            ReferenceIterator it = rm.getReferencesTo(target);
            while (it.hasNext()) {
                Reference r = it.next();
                Address from = r.getFromAddress();
                Function fn = currentProgram.getFunctionManager().getFunctionContaining(from);
                w.printf("%s -> %s  [%s]  in %s%n",
                         from, target, r.getReferenceType().getName(),
                         fn != null ? fn.getName() + " @ " + fn.getEntryPoint() : "(no func)");
                ++count;
            }
        }
        println("xrefs=" + count + " -> " + out);
    }
}
