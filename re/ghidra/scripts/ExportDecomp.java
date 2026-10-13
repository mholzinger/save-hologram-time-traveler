// Decompile every function and write one .c file per function plus an index.
// Output dir: first script arg, default build/ghidra/decomp
// @category TimeTraveler

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "build/ghidra/decomp");
        out.mkdirs();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        int n = 0, fail = 0;
        try (PrintWriter idx = new PrintWriter(new File(out, "INDEX.tsv"))) {
            idx.println("entry\tname\tfile\tstatus");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                String entry = f.getEntryPoint().toString().replace(':', '_');
                String file = entry + "_" + f.getName() + ".c";
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted()) {
                    try (PrintWriter w = new PrintWriter(new File(out, file))) {
                        w.println("// " + f.getEntryPoint() + " " + f.getName());
                        w.print(r.getDecompiledFunction().getC());
                    }
                    idx.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + file + "\tok");
                    n++;
                } else {
                    idx.println(f.getEntryPoint() + "\t" + f.getName() + "\t-\tFAILED");
                    fail++;
                }
            }
        }
        di.dispose();
        println("ExportDecomp: " + n + " ok, " + fail + " failed -> " + out);
    }
}
