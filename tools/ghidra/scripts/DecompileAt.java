// SPDX-License-Identifier: GPL-3.0-or-later
// Decompile the functions at the given entry points, creating them first
// when auto-analysis never did (code reached only through data tables:
// callbacks, menu handlers). Works on a -readOnly project: nothing is
// saved. Args: <output.c> <entry-hex>...
//@category Decompiler

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

import java.io.*;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DecompileAt <output.c> <entry-hex>...");
            return;
        }
        File out = new File(args[0]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (int i = 1; i < args.length; ++i) {
                Address ep = toAddr(Long.parseUnsignedLong(args[i].replace("0x", ""), 16));
                if (getInstructionAt(ep) == null) disassemble(ep);
                Function fn = getFunctionAt(ep);
                if (fn == null) fn = createFunction(ep, null);
                w.printf("// === %s @ %s ===%n", fn != null ? fn.getName() : "?", ep);
                if (fn == null) { w.println("// FAILED: no function"); continue; }
                DecompileResults res = decomp.decompileFunction(fn, 60, monitor);
                w.println(res != null && res.decompileCompleted() ? res.getDecompiledFunction().getC()
                                                                 : "// FAILED: " + (res != null ? res.getErrorMessage() : "null"));
            }
        }
        println("-> " + out);
    }
}
