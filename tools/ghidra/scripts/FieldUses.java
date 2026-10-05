// SPDX-License-Identifier: GPL-3.0-or-later
// Every use of [R + field] where R was loaded from [any + base] shortly
// before (within `window` instructions, R not written in between): finds
// who touches a struct's field reached through a pointer at +base (a
// unit's data at +0x14, say), which a byte search can't. Args:
//   <base-hex> <field-hex> <window> <output.txt>
//@category References

import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

import java.io.*;
import java.util.*;

public class FieldUses extends GhidraScript {
    // The register / displacement of a memory operand like [EAX + 0x14], or null.
    private Object[] memoryOperand(Instruction instruction, int operand) {
        Object[] parts = instruction.getOpObjects(operand);
        Register register = null;
        Long displacement = 0L;
        boolean memory = instruction.getDefaultOperandRepresentation(operand).contains("[");
        if (!memory) return null;
        for (Object part : parts) {
            if (part instanceof Register) {
                if (register != null) return null;   // [a + b*k]: not this shape
                register = (Register) part;
            } else if (part instanceof Scalar) {
                displacement = ((Scalar) part).getSignedValue();
            }
        }
        return register == null ? null : new Object[] { register, displacement };
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long base = Long.parseLong(args[0].replace("0x", ""), 16);
        long field = Long.parseLong(args[1].replace("0x", ""), 16);
        int window = Integer.parseInt(args[2]);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[3]))) {
            for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
                Map<String, Integer> loadedAt = new HashMap<>();   // register -> instruction index it got [x + base]
                int index = 0;
                for (Instruction instruction : currentProgram.getListing().getInstructions(function.getBody(), true)) {
                    ++index;
                    // A use of [R + field] with R loaded from [x + base] lately.
                    for (int operand = 0; operand < instruction.getNumOperands(); ++operand) {
                        Object[] memory = memoryOperand(instruction, operand);
                        if (memory == null || (Long) memory[1] != field) continue;
                        String register = ((Register) memory[0]).getBaseRegister().getName();
                        Integer at = loadedAt.get(register);
                        if (at != null && index - at <= window)
                            out.printf("%s %s  %s  (operand %d)%n", instruction.getAddress(), function.getName(), instruction, operand);
                    }
                    // MOV R, [x + base] marks R; any other write to R clears it.
                    Object[] results = instruction.getResultObjects();
                    String mnemonic = instruction.getMnemonicString();
                    Object[] source = instruction.getNumOperands() == 2 ? memoryOperand(instruction, 1) : null;
                    for (Object result : results) {
                        if (!(result instanceof Register)) continue;
                        String register = ((Register) result).getBaseRegister().getName();
                        if (mnemonic.equals("MOV") && source != null && (Long) source[1] == base) loadedAt.put(register, index);
                        else loadedAt.remove(register);
                    }
                }
            }
        }
    }
}
