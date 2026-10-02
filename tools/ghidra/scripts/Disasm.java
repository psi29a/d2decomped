// SPDX-License-Identifier: GPL-3.0-or-later
// Print raw disassembly for an address range — one instruction per line
// with mnemonic + operands. Handy when the decompiler hides register-level
// info like fastcall ECX arguments. Args:
//   <start-hex> <end-hex> <output.txt>
//@category Disassembler

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

import java.io.*;

public class Disasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            println("usage: Disasm <start-hex> <end-hex> <output.txt>");
            return;
        }
        Address start = toAddr(Long.parseUnsignedLong(args[0].replace("0x", ""), 16));
        Address end   = toAddr(Long.parseUnsignedLong(args[1].replace("0x", ""), 16));
        File out = new File(args[2]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();
        Listing listing = currentProgram.getListing();
        // Code only reached via data tables (menu-record on_click etc.)
        // was never disassembled by auto-analysis — do it on demand.
        if (listing.getInstructionAt(start) == null) disassemble(start);
        int count = 0;
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            w.printf("// disasm %s..%s%n%n", start, end);
            Instruction i = listing.getInstructionAt(start);
            if (i == null) i = listing.getInstructionAfter(start);   // start fell mid-instruction
            while (i != null && i.getAddress().compareTo(end) < 0) {
                w.printf("%s  %-30s ; %s%n",
                         i.getAddress(),
                         i.toString(),
                         i.getMnemonicString());
                ++count;
                i = i.getNext();
            }
        }
        println("insns=" + count + " -> " + out);
    }
}
