// Dump a first-pass overview of the current program to markdown.
// Args: [outDir]  (default: current working dir)
// Output: <outDir>/<program-name>-overview.md
//         + <outDir>/<program-name>-{functions,strings,imports,exports}.tsv
//
// ponytail: single flat dump, no filtering. Grep is the query language.
//@category D2Decomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ExportOverview extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path outDir = Paths.get(args.length > 0 ? args[0] : ".").toAbsolutePath();
        Files.createDirectories(outDir);

        // Strip every extension so "game.exe.pe" (the carved scratch name) becomes "game".
        String base = currentProgram.getName().toLowerCase().replaceAll("\\..*$", "");
        Path funcsTsv   = outDir.resolve(base + "-functions.tsv");
        Path stringsTsv = outDir.resolve(base + "-strings.tsv");
        Path importsTsv = outDir.resolve(base + "-imports.tsv");
        Path exportsTsv = outDir.resolve(base + "-exports.tsv");
        Path overviewMd = outDir.resolve(base + "-overview.md");

        long funcCount    = dumpFunctions(funcsTsv);
        long stringCount  = dumpStrings(stringsTsv);
        long importCount  = dumpImports(importsTsv);
        long exportCount  = dumpExports(exportsTsv);

        writeOverview(overviewMd, base, funcCount, stringCount, importCount, exportCount);
        println("wrote overview to " + overviewMd);
    }

    private long dumpFunctions(Path out) throws IOException {
        long n = 0;
        try (BufferedWriter w = Files.newBufferedWriter(out)) {
            w.write("address\tname\tsize\tparams\n");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                w.write(String.format("%s\t%s\t%d\t%d%n",
                        f.getEntryPoint(), f.getName(), f.getBody().getNumAddresses(),
                        f.getParameterCount()));
                n++;
            }
        }
        return n;
    }

    private long dumpStrings(Path out) throws IOException {
        long n = 0;
        try (BufferedWriter w = Files.newBufferedWriter(out)) {
            w.write("address\tlength\tvalue\n");
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
                String v = sdi.getStringValue();
                if (v == null || v.isEmpty()) continue;
                // one line per string; escape newlines/tabs to keep TSV parseable.
                v = v.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
                w.write(String.format("%s\t%d\t%s%n", d.getAddress(), v.length(), v));
                n++;
            }
        }
        return n;
    }


    private long dumpImports(Path out) throws IOException {
        long n = 0;
        try (BufferedWriter w = Files.newBufferedWriter(out)) {
            w.write("library\tsymbol\taddress\n");
            SymbolTable st = currentProgram.getSymbolTable();
            SymbolIterator it = st.getExternalSymbols();
            while (it.hasNext() && !monitor.isCancelled()) {
                Symbol s = it.next();
                if (s.getSymbolType() != SymbolType.FUNCTION && s.getSymbolType() != SymbolType.LABEL) continue;
                Namespace ns = s.getParentNamespace();
                String lib = ns != null ? ns.getName() : "";
                w.write(String.format("%s\t%s\t%s%n", lib, s.getName(), s.getAddress()));
                n++;
            }
        }
        return n;
    }

    private long dumpExports(Path out) throws IOException {
        long n = 0;
        try (BufferedWriter w = Files.newBufferedWriter(out)) {
            w.write("ordinal\tname\taddress\n");
            SymbolTable st = currentProgram.getSymbolTable();
            AddressIterator addrs = st.getExternalEntryPointIterator();
            while (addrs.hasNext() && !monitor.isCancelled()) {
                Address a = addrs.next();
                Symbol[] syms = st.getSymbols(a);
                String name = syms.length > 0 ? syms[0].getName() : "";
                w.write(String.format("\t%s\t%s%n", name, a));
                n++;
            }
        }
        return n;
    }

    private void writeOverview(Path out, String base, long f, long s, long imp, long exp) throws IOException {
        try (BufferedWriter w = Files.newBufferedWriter(out)) {
            w.write("# " + base + " — first-pass overview\n\n");
            w.write("Generated by `tools/ghidra/scripts/ExportOverview.java`.\n");
            w.write("Regenerate with `tools/ghidra/import.sh`.\n\n");
            w.write("| metric | count |\n|---|---:|\n");
            w.write("| functions | " + f + " |\n");
            w.write("| defined strings | " + s + " |\n");
            w.write("| imports | " + imp + " |\n");
            w.write("| exports | " + exp + " |\n\n");
            w.write("Companion TSVs live alongside this file:\n");
            w.write("- `" + base + "-functions.tsv`\n");
            w.write("- `" + base + "-strings.tsv`\n");
            w.write("- `" + base + "-imports.tsv`\n");
            w.write("- `" + base + "-exports.tsv`\n\n");
            w.write("Grep these before diving into Ghidra — cheap way to find anchors.\n");
        }
    }
}
