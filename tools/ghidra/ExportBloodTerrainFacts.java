// Exports terrain/blood decal reverse-engineering facts from the open Ghidra project.
// Run through analyzeHeadless with -readOnly -noanalysis.

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressFormatException;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

public class ExportBloodTerrainFacts extends GhidraScript {
    private static final String[] TARGET_ADDRESSES = {
        "140548c00",
        "1405497f0",
        "14069d5f0",
        "1435773c0",
        "143d83710",
        "143dfa040",
    };

    private static final String[] FIELD_NEEDLES = {
        "0x14",
        "0x40",
        "0xa0",
        "0xd0",
        "0x119",
        "0x12b",
        "0x135",
        "0x158",
        "0x15c",
        "0x160",
        "0x168",
        "0x16c",
        "0x198",
        "0x19c",
        "0x1a8",
        "0x20b",
        "0x2d8",
        "0x2dc",
    };

    @Override
    protected void run() throws Exception {
        if (currentProgram == null) {
            println("ERROR: no currentProgram");
            return;
        }

        println("PROGRAM " + currentProgram.getName());
        println("IMAGE_BASE " + currentProgram.getImageBase());
        println("LANGUAGE " + currentProgram.getLanguageID());
        println("");

        exportTargetFunctions();
        exportFieldAccessIndex();
    }

    private Address addr(String hex) throws AddressFormatException {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
    }

    private void exportTargetFunctions() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        println("=== TARGET FUNCTIONS ===");
        for (String addressText : TARGET_ADDRESSES) {
            Address address = addr(addressText);
            Function function = getFunctionContaining(address);
            if (function == null) {
                println("FUNCTION_MISSING " + address);
                continue;
            }

            println("");
            println("FUNCTION " + function.getName() + " ENTRY " + function.getEntryPoint() +
                    " BODY " + function.getBody());

            Listing listing = currentProgram.getListing();
            InstructionIterator instructions = listing.getInstructions(function.getBody(), true);
            int emitted = 0;
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                println("  ASM " + instruction.getAddress() + "  " + instruction);
                emitted++;
                if (emitted >= 260) {
                    println("  ASM_TRUNCATED after 260 instructions");
                    break;
                }
            }

            DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
            if (result != null && result.decompileCompleted()) {
                println("  DECOMPILE_BEGIN");
                println(result.getDecompiledFunction().getC());
                println("  DECOMPILE_END");
            } else {
                println("  DECOMPILE_FAILED " + (result == null ? "<null>" : result.getErrorMessage()));
            }
        }
        decompiler.dispose();
        println("");
    }

    private void exportFieldAccessIndex() {
        println("=== FIELD ACCESS INDEX ===");
        Listing listing = currentProgram.getListing();
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        Set<Function> emittedFunctions = new HashSet<>();

        while (functions.hasNext() && !monitor.isCancelled()) {
            Function function = functions.next();
            InstructionIterator instructions = listing.getInstructions(function.getBody(), true);
            boolean printedHeader = false;
            int matches = 0;
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                String text = instruction.toString().toLowerCase(Locale.ROOT);
                String operands = operandsText(instruction).toLowerCase(Locale.ROOT);
                if (!containsAnyFieldNeedle(text) && !containsAnyFieldNeedle(operands)) {
                    continue;
                }

                if (!printedHeader) {
                    println("");
                    println("FUNCTION_ACCESS " + function.getName() + " ENTRY " + function.getEntryPoint());
                    emittedFunctions.add(function);
                    printedHeader = true;
                }
                println("  " + instruction.getAddress() + "  " + instruction + "  OPERANDS " + operands);
                matches++;
                if (matches >= 80) {
                    println("  ACCESS_TRUNCATED after 80 matches");
                    break;
                }
            }
        }
        println("");
        println("FIELD_ACCESS_FUNCTIONS " + emittedFunctions.size());
    }

    private boolean containsAnyFieldNeedle(String text) {
        for (String needle : FIELD_NEEDLES) {
            if (text.contains(needle)) {
                return true;
            }
        }
        return false;
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
