// SPDX-License-Identifier: GPL-3.0-or-later
// For every CALL to one of the given targets inside [start, end), print the
// call site with the last few register loads before it (ECX/EDX setup that
// the decompiler drops for fastcall helpers). Args:
//   <start-hex> <end-hex> <output.txt> <target-hex>...
//@category Disassembler

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

import java.io.*;
import java.util.*;

public class CallArgs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 4) { println("usage: CallArgs <start> <end> <out> <target>..."); return; }
        Address start = toAddr(Long.parseUnsignedLong(args[0], 16));
        Address end = toAddr(Long.parseUnsignedLong(args[1], 16));
        Set<Long> targets = new HashSet<>();
        for (int i = 3; i < args.length; ++i) targets.add(Long.parseUnsignedLong(args[i], 16));
        Listing listing = currentProgram.getListing();
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(args[2])))) {
            Deque<String> recent = new ArrayDeque<>();
            Instruction ins = listing.getInstructionAt(start);
            if (ins == null) ins = listing.getInstructionAfter(start);
            while (ins != null && ins.getAddress().compareTo(end) < 0) {
                String text = ins.toString();
                if (ins.getMnemonicString().equals("CALL") && ins.getFlows().length > 0
                        && targets.contains(ins.getFlows()[0].getOffset())) {
                    String fn = getFunctionContaining(ins.getAddress()) != null
                            ? getFunctionContaining(ins.getAddress()).getName() : "?";
                    w.printf("%s %s  %s   <- %s%n", ins.getAddress(), fn, text, String.join(" | ", recent));
                }
                if (text.contains("ECX") || text.contains("EDX") || text.startsWith("PUSH")) {
                    recent.addLast(text);
                    if (recent.size() > 6) recent.removeFirst();
                }
                if (ins.getMnemonicString().equals("CALL") || ins.getMnemonicString().startsWith("RET")) recent.clear();
                ins = ins.getNext();
            }
        }
    }
}
