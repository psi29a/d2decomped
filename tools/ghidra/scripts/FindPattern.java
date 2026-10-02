// SPDX-License-Identifier: GPL-3.0-or-later
// Scan the whole loaded program's memory for a byte pattern and print
// every match address. Pattern uses "??" for wildcards in a single byte.
// Args: <hex-pattern-with-spaces-or-??> <output.txt>
//   e.g. "06 00 00 00 ?? ?? ?? ?? 3c 02 00 00"
//@category Data

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.*;
import java.util.*;

public class FindPattern extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            println("usage: FindPattern <hex-pattern> <output.txt>");
            return;
        }
        // Parse pattern: whitespace-separated hex bytes, "??" = wildcard.
        String[] parts = args[0].trim().split("\\s+");
        byte[] needle = new byte[parts.length];
        boolean[] wild = new boolean[parts.length];
        for (int i = 0; i < parts.length; ++i) {
            if (parts[i].equals("??") || parts[i].equals("**")) {
                wild[i] = true;
                needle[i] = 0;
            } else {
                needle[i] = (byte)(Integer.parseInt(parts[i], 16) & 0xff);
            }
        }
        File out = new File(args[1]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();
        int hits = 0;
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            w.printf("// pattern: %s (%d bytes)%n%n", args[0], needle.length);
            Memory mem = currentProgram.getMemory();
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.isInitialized()) continue;
                long start = blk.getStart().getOffset();
                long size  = blk.getSize();
                byte[] buf = new byte[(int)Math.min(size, Integer.MAX_VALUE)];
                mem.getBytes(blk.getStart(), buf);
                for (int i = 0; i + needle.length <= buf.length; ++i) {
                    boolean ok = true;
                    for (int j = 0; j < needle.length; ++j) {
                        if (!wild[j] && buf[i + j] != needle[j]) { ok = false; break; }
                    }
                    if (ok) {
                        w.printf("%08x  in block %s%n", start + i, blk.getName());
                        ++hits;
                    }
                }
            }
        }
        println("hits=" + hits + " -> " + out);
    }
}
