// Narrow terrain/blood decal fact export. Keeps output focused on candidate
// terrain lifetime fields and confirmed particle decal flags.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressFormatException;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

public class ExportTerrainNarrowFacts extends GhidraScript {
    private static final String[] TARGET_FUNCTIONS = {
        "1405497f0",
        "14069d5f0",
        "143d83710",
        "143dfa040",
    };

    private static final String[] FIELD_NEEDLES = {
        "+ 0x119]",
        "+ 0x12b]",
        "+ 0x135]",
        "+ 0x154]",
        "+ 0x158]",
        "+ 0x15c]",
        "+ 0x160]",
        "+ 0x164]",
        "+ 0x168]",
        "+ 0x16c]",
        "+ 0x198]",
        "+ 0x19c]",
        "+ 0x1a8]",
        "+ 0x20b]",
        "+ 0x2d8]",
        "+ 0x2dc]",
    };

    @Override
    protected void run() throws Exception {
        println("PROGRAM " + currentProgram.getName());
        println("IMAGE_BASE " + currentProgram.getImageBase());
        println("");

        exportTargetCallers();
        exportFieldAccesses();
    }

    private Address addr(String hex) throws AddressFormatException {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
    }

    private void exportTargetCallers() throws Exception {
        println("=== TARGET CALLERS ===");
        for (String target : TARGET_FUNCTIONS) {
            Address targetAddress = addr(target);
            Function targetFunction = getFunctionAt(targetAddress);
            println("TARGET " + targetAddress + " " +
                    (targetFunction == null ? "<no-function>" : targetFunction.getName()));

            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(targetAddress);
            int count = 0;
            while (refs.hasNext() && !monitor.isCancelled()) {
                Reference ref = refs.next();
                Function caller = getFunctionContaining(ref.getFromAddress());
                println("  REF " + ref.getReferenceType() + " FROM " + ref.getFromAddress() +
                        " CALLER " + (caller == null ? "<none>" : caller.getName() + "@" + caller.getEntryPoint()));
                count++;
                if (count >= 80) {
                    println("  REF_TRUNCATED after 80 refs");
                    break;
                }
            }
        }
        println("");
    }

    private void exportFieldAccesses() {
        println("=== NARROW FIELD ACCESSES ===");
        Listing listing = currentProgram.getListing();
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        Map<String, Integer> counts = new HashMap<>();
        int total = 0;

        while (functions.hasNext() && !monitor.isCancelled()) {
            Function function = functions.next();
            InstructionIterator instructions = listing.getInstructions(function.getBody(), true);
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                String operands = operandsText(instruction).toLowerCase(Locale.ROOT);
                String matched = matchedNeedle(operands);
                if (matched == null) {
                    continue;
                }

                int count = counts.getOrDefault(matched, 0);
                if (count < 240) {
                    println("FIELD " + matched + " FUNC " + function.getName() + "@" +
                            function.getEntryPoint() + " ADDR " + instruction.getAddress() +
                            " INST " + instruction + " OPERANDS " + operands);
                }
                counts.put(matched, count + 1);
                total++;
            }
        }

        println("");
        println("FIELD_TOTAL " + total);
        for (String needle : FIELD_NEEDLES) {
            println("FIELD_COUNT " + needle + " " + counts.getOrDefault(needle, 0));
        }
    }

    private String matchedNeedle(String operands) {
        for (String needle : FIELD_NEEDLES) {
            if (operands.contains(needle)) {
                return needle;
            }
        }
        return null;
    }

    private String operandsText(Instruction instruction) {
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < instruction.getNumOperands(); i++) {
            if (i > 0) {
                builder.append(" | ");
            }
            builder.append(instruction.getDefaultOperandRepresentation(i));
        }
        return builder.toString();
    }
}
