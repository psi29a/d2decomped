// Every address in [start, end) (step bytes apart) that something refers
// to, with the referrers — finds where code indexes a table. Args:
//   <start-hex> <end-hex> <step> <output.txt>
//@category References

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;

import java.io.*;

public class XrefsRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 4) { println("usage: XrefsRange <start-hex> <end-hex> <step> <output.txt>"); return; }
        long start = Long.parseUnsignedLong(args[0].replace("0x", ""), 16);
        long end = Long.parseUnsignedLong(args[1].replace("0x", ""), 16);
        int step = Integer.parseInt(args[2]);
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(args[3])))) {
            for (long a = start; a < end; a += step) {
                Address target = toAddr(a);
                ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(target);
                while (it.hasNext()) {
                    Reference r = it.next();
                    Function fn = getFunctionContaining(r.getFromAddress());
                    w.printf("%s <- %s  %s  %s%n", target, r.getFromAddress(), r.getReferenceType(),
                             fn != null ? fn.getName() + " @ " + fn.getEntryPoint() : "(no fn)");
                }
            }
        }
    }
}
