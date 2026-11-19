// SPDX-License-Identifier: GPL-3.0-or-later
// The functions whose bodies Ghidra splits into more than one address range
// (a tail moved away, a shared epilogue): one row each, the ranges inclusive.
// Args: [outDir]  (default: current working dir)
// Output: <outDir>/<program-name>-ranges.tsv  (address, ranges "start-end,...")
//@category D2Decomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

public class ExportRanges extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path outDir = Paths.get(args.length > 0 ? args[0] : ".").toAbsolutePath();
        Files.createDirectories(outDir);
        String base = currentProgram.getName().toLowerCase().replaceAll("\\..*$", "");
        Path out = outDir.resolve(base + "-ranges.tsv");
        long rows = 0;
        try (BufferedWriter w = Files.newBufferedWriter(out)) {
            w.write("address\tranges\n");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                if (f.getBody().getNumAddressRanges() < 2) continue;
                List<String> parts = new ArrayList<>();
                for (AddressRange range : f.getBody()) parts.add(range.getMinAddress() + "-" + range.getMaxAddress());
                w.write(f.getEntryPoint() + "\t" + String.join(",", parts) + "\n");
                rows++;
            }
        }
        println("wrote " + rows + " split functions to " + out);
    }
}
