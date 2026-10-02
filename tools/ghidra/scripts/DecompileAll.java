// SPDX-License-Identifier: GPL-3.0-or-later
// Decompile every function in the loaded program into one .c file.
// Public domain (CC0).
//@category Decompiler

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;

import java.io.*;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: DecompileAll <output.c>");
            return;
        }
        File out = new File(args[0]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            w.println("// Auto-decompiled by Ghidra");
            w.println("// Source binary: " + currentProgram.getName());
            w.println("// Functions: " + currentProgram.getFunctionManager().getFunctionCount());
            w.println();

            FunctionIterator fns = currentProgram.getFunctionManager().getFunctions(true);
            int count = 0, errors = 0;
            for (Function fn : fns) {
                if (monitor.isCancelled()) break;
                count++;
                w.println("// =============================================");
                w.println("// " + fn.getName() + " @ " + fn.getEntryPoint());
                w.println("// =============================================");
                try {
                    DecompileResults res = decomp.decompileFunction(fn, 30, monitor);
                    if (res != null && res.decompileCompleted()) {
                        w.println(res.getDecompiledFunction().getC());
                    } else {
                        w.println("// DECOMPILE FAILED: " +
                                  (res != null ? res.getErrorMessage() : "null result"));
                        errors++;
                    }
                } catch (Exception e) {
                    w.println("// DECOMPILE EXCEPTION: " + e.getMessage());
                    errors++;
                }
                w.println();
                if (count % 25 == 0) {
                    println("decompiled " + count + " functions...");
                }
            }
            println("done: " + count + " functions, " + errors + " errors -> " + out.getAbsolutePath());
        }
    }
}
