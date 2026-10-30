// SPDX-License-Identifier: GPL-3.0-or-later
// Dump the call graph and each function's assert source files, for tools/gaps.py.
// Args: [outDir]  (default: current working dir)
// Output: <outDir>/<program-name>-calls.tsv     caller, callee, kind (call|jump|data)
//         <outDir>/<program-name>-srcfiles.tsv  function, source file named by an assert string
//
// A "data" row is the callee's address taken: by a function (a callback) or,
// when the caller is not a function, by the data holding it (a dispatch table).
//@category D2Decomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.Pattern;

public class ExportCalls extends GhidraScript {

    private static final Pattern SOURCE_FILE = Pattern.compile("(?i).*\\.(cpp|c|h)$");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path outDir = Paths.get(args.length > 0 ? args[0] : ".").toAbsolutePath();
        Files.createDirectories(outDir);
        String base = currentProgram.getName().toLowerCase().replaceAll("\\..*$", "");
        long calls = dumpCalls(outDir.resolve(base + "-calls.tsv"));
        long files = dumpSourceFiles(outDir.resolve(base + "-srcfiles.tsv"));
        println("wrote " + calls + " call rows, " + files + " source-file rows");
    }

    private long dumpCalls(Path out) throws Exception {
        FunctionManager functions = currentProgram.getFunctionManager();
        ReferenceManager refs = currentProgram.getReferenceManager();
        // TreeSet: sorted and deduplicated, so a regeneration diffs cleanly.
        TreeSet<String> rows = new TreeSet<>();
        for (Function callee : functions.getFunctions(true)) {
            monitor.checkCancelled();  // throws: never write a partial table
            for (Reference ref : refs.getReferencesTo(callee.getEntryPoint())) {
                RefType type = ref.getReferenceType();
                Function caller = functions.getFunctionContaining(ref.getFromAddress());
                String kind = type.isCall() ? "call" : type.isJump() ? "jump" : "data";
                // A jump inside the callee is its own loop, not an edge.
                if (caller == callee && !type.isCall()) continue;
                String from = (caller != null ? caller.getEntryPoint() : ref.getFromAddress()).toString();
                rows.add(from + "\t" + callee.getEntryPoint() + "\t" + kind);
            }
        }
        return write(out, "caller\tcallee\tkind", rows);
    }

    private long dumpSourceFiles(Path out) throws Exception {
        FunctionManager functions = currentProgram.getFunctionManager();
        ReferenceManager refs = currentProgram.getReferenceManager();
        TreeSet<String> rows = new TreeSet<>();
        for (Data data : currentProgram.getListing().getDefinedData(true)) {
            monitor.checkCancelled();  // throws: never write a partial table
            if (!data.hasStringValue()) continue;
            String value = StringDataInstance.getStringDataInstance(data).getStringValue();
            if (value == null || !SOURCE_FILE.matcher(value).matches()) continue;
            for (Reference ref : refs.getReferencesTo(data.getAddress())) {
                Function user = functions.getFunctionContaining(ref.getFromAddress());
                if (user != null) rows.add(user.getEntryPoint() + "\t" + lastTwo(value));
            }
        }
        return write(out, "function\tfile", rows);
    }

    // ".\DRLG\DrlgAnim.cpp" and "d:\diablo2\...\D2Game/Quests/a5q4.cpp" become
    // "DRLG/DrlgAnim.cpp" and "Quests/a5q4.cpp": the module and the file.
    private static String lastTwo(String path) {
        String[] parts = path.replace('\\', '/').split("/");
        int n = parts.length;
        return n > 1 ? parts[n - 2].replaceAll("^\\W+", "") + "/" + parts[n - 1] : parts[0];
    }

    private long write(Path out, String header, Collection<String> rows) throws IOException {
        try (BufferedWriter w = Files.newBufferedWriter(out)) {
            w.write(header + "\n");
            for (String row : rows) w.write(row + "\n");
        }
        return rows.size();
    }
}
