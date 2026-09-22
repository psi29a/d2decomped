// Dump raw bytes at a virtual-address range to a hex-per-line text file.
// One record per line (with the given record size) so tables of fixed-size
// structs are easy to slice. Args: <start-hex> <end-hex> <rec-size-hex> <output.txt>
//@category Data

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.*;

public class DumpBytes extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 4) {
            println("usage: DumpBytes <start-hex> <end-hex> <record-size-hex> <output.txt>");
            return;
        }
        long startL = Long.parseUnsignedLong(args[0].replace("0x", ""), 16);
        long endL   = Long.parseUnsignedLong(args[1].replace("0x", ""), 16);
        int  rec    = (int) Long.parseUnsignedLong(args[2].replace("0x", ""), 16);
        Address start = toAddr(startL);
        Memory mem = currentProgram.getMemory();

        File out = new File(args[3]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();

        int total = (int)(endL - startL);
        byte[] buf = new byte[total];
        mem.getBytes(start, buf);

        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            w.printf("// bytes %s..%s (%d bytes, %d-byte records)%n%n",
                     args[0], args[1], total, rec);
            for (int off = 0; off < total; off += rec) {
                w.printf("%08x:", startL + off);
                int end = Math.min(off + rec, total);
                for (int i = off; i < end; ++i) w.printf(" %02x", buf[i] & 0xff);
                // Also print as 32-bit LE dwords for tables of struct fields.
                if (rec >= 4) {
                    w.print("   |");
                    for (int i = off; i + 4 <= end; i += 4) {
                        int dw = (buf[i] & 0xff)
                               | ((buf[i+1] & 0xff) << 8)
                               | ((buf[i+2] & 0xff) << 16)
                               | ((buf[i+3] & 0xff) << 24);
                        w.printf(" %08x", dw);
                    }
                }
                w.println();
            }
        }
        println("dumped " + total + " bytes -> " + out);
    }
}
