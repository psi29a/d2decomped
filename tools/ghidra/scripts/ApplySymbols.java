// SPDX-License-Identifier: GPL-3.0-or-later
// Put d2d's names and citations on game.exe's functions, from tools/gaps.py's symbols.tsv:
// a row's name (when it has one) becomes the function's, and a plate comment says
// where d2d ports, verifies or cites it. Rerunning replaces what the last run set.
//@category D2Decomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

import java.nio.file.*;
import java.util.List;

public class ApplySymbols extends GhidraScript {
    static final String MARK = "d2d ";

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: ApplySymbols <symbols.tsv>");
            return;
        }
        List<String> rows = Files.readAllLines(Paths.get(args[0]));
        int named = 0, commented = 0;
        for (String row : rows.subList(1, rows.size())) {
            String[] cells = row.split("\t", -1);
            Function fn = getFunctionAt(toAddr(Long.parseUnsignedLong(cells[0], 16)));
            if (fn == null) continue;
            if (!cells[1].isEmpty() && !fn.getName().equals(cells[1])) {
                fn.setName(cells[1], SourceType.USER_DEFINED);
                named++;
            }
            String plate = MARK + cells[2] + ": " + cells[3].replace(" ", "\n  ");
            String old = fn.getComment();
            if (old == null || old.startsWith(MARK)) {  // never overwrite someone's own plate
                fn.setComment(plate);
                commented++;
            }
        }
        println("named " + named + ", commented " + commented + " functions");
    }
}
