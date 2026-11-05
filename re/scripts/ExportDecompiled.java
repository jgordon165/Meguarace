// Ghidra headless post-script: writes the decompiled C of every function to
// <outdir>/all_functions.c and a function index to <outdir>/functions.tsv
// (address, size, name, callers) for planning which subsystems to port.
//@category MegaRacePort

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.File;
import java.io.PrintWriter;

public class ExportDecompiled extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : ".");
        out.mkdirs();

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);

        try (PrintWriter c = new PrintWriter(new File(out, "all_functions.c"));
             PrintWriter idx = new PrintWriter(new File(out, "functions.tsv"))) {
            idx.println("address\tsize\tname\tcallers");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                idx.printf("%s\t%d\t%s\t%d%n", f.getEntryPoint(), f.getBody().getNumAddresses(),
                           f.getName(), f.getCallingFunctions(monitor).size());
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                c.printf("// ---- %s @ %s%n", f.getName(), f.getEntryPoint());
                if (r != null && r.decompileCompleted()) {
                    c.println(r.getDecompiledFunction().getC());
                } else {
                    c.println("// decompilation failed: " + (r == null ? "null" : r.getErrorMessage()));
                }
                n++;
            }
            println("exported " + n + " functions to " + out);
        } finally {
            dec.dispose();
        }
    }
}
