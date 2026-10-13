// Tag every function still named FUN_* with its source module, from the segment -> module map
// in docs/ROM_FINDINGS.md ("Source modules"). Anchored modules get "<module>_SEG_OFF";
// order-inferred ones get "maybe_<module>_SEG_OFF" (CLAUDE.md rule 5). User labels are kept.
// @category TimeTraveler

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class TimeTravelerModules extends GhidraScript {

    // { segment, module, anchored? }  (linear base = segment * 16; a segment runs to the next one)
    private static final String[][] SEGS = {
        {"C000", "crt", "y"},
        {"C2A7", "maybe_main", "n"},
        {"C2BA", "attract", "n"},
        {"C301", "config", "y"},
        {"C359", "odds", "n"},
        {"C3CE", "chorder", "n"},
        {"C40B", "episode", "n"},
        {"C4B6", "asset", "n"},
        {"CE55", "input", "n"},
        {"CEDF", "specobj", "y"},
        {"CFD9", "window", "n"},
        {"D02B", "qinput", "n"},
        {"D047", "action", "n"},
        {"D0F0", "choice", "n"},
        {"D17E", "device", "y"},
        {"D245", "clock", "n"},
        {"D2DC", "sound", "n"},
        {"D30C", "serial", "y"},
        {"D36C", "storage", "n"},
        {"D37E", "pioneer", "y"},
        {"D47D", "display", "y"},
        {"D604", "unsolved", "n"},
        {"D63D", "hint", "n"},
        {"D659", "learn", "n"},
        {"D668", "init", "n"},
        {"D70D", "chapter", "n"},
        {"D7C0", "branch", "n"},
        {"D843", "map", "n"},
        {"D945", "account", "y"},
        {"DA9D", "misc", "y"},
        {"DB9B", "crt_lib", "y"},
    };

    @Override
    public void run() throws Exception {
        int renamed = 0, kept = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long lin = f.getEntryPoint().getOffset();   // SegmentedAddress offset = linear address
            int idx = -1;
            for (int i = 0; i < SEGS.length; i++) {
                if (Long.parseLong(SEGS[i][0], 16) * 16 <= lin) idx = i;
            }
            boolean ours = f.getSymbol().getSource() != SourceType.USER_DEFINED
                    && f.getName().matches(".*_[0-9A-F]{4}_[0-9A-F]{4}");
            if (idx < 0 || !(f.getName().startsWith("FUN_") || ours)) { kept++; continue; }
            long seg = Long.parseLong(SEGS[idx][0], 16);
            String mod = SEGS[idx][1];
            if (SEGS[idx][2].equals("n") && !mod.startsWith("maybe_") && !mod.startsWith("seg_")) mod = "maybe_" + mod;
            String name = String.format("%s_%04X_%04X", mod, seg, lin - seg * 16);
            f.setName(name, SourceType.ANALYSIS);
            renamed++;
        }
        println("TimeTravelerModules: renamed " + renamed + ", kept " + kept);
    }
}
