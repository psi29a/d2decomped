// SPDX-License-Identifier: GPL-3.0-or-later
// Decompile every function whose entry point falls in [start, end) to one .c
// file. Args: <start-hex> <end-hex> <output.c>
//@category Decompiler

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

import java.io.*;

public class DecompileRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            println("usage: DecompileRange <start-hex> <end-hex> <output.c>");
            return;
        }
        Address start = toAddr(Long.parseUnsignedLong(args[0].replace("0x", ""), 16));
        Address end   = toAddr(Long.parseUnsignedLong(args[1].replace("0x", ""), 16));
        File out = new File(args[2]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        int count = 0, errors = 0;
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            w.printf("// Ghidra decompilation, range %s..%s (source: %s)%n%n",
                     start, end, currentProgram.getName());
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext()) {
                if (monitor.isCancelled()) break;
                Function fn = it.next();
                Address ep = fn.getEntryPoint();
                if (ep.compareTo(start) < 0 || ep.compareTo(end) >= 0) continue;

                w.printf("// === %s @ %s ===%n", fn.getName(), ep);
                DecompileResults res = decomp.decompileFunction(fn, 30, monitor);
                if (res != null && res.decompileCompleted()) {
                    w.println(res.getDecompiledFunction().getC());
                } else {
                    w.println("// FAILED: " + (res != null ? res.getErrorMessage() : "null"));
                    errors++;
                }
                w.println();
                count++;
            }
        }
        println("decompiled " + count + " functions, " + errors + " errors -> " + out);
    }
}
