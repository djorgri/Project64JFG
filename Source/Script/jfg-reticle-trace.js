// Read-only JFG USA reticle trace. Requires Project64's interpreter.
// Run manually before loading an aiming state or entering the aiming mode.
// No guest memory, registers, or input are changed. Stops after 512 distinct rows.
(function () {
    "use strict";
    var path = pj64.installDirectory + "JfgReticleTrace.log";
    var rows = ["JFG reticle trace " + new Date().toISOString()];
    var seen = {}, count = 0;
    var correctedReturns = {
        0xC70: true, 0xCFC: true, 0xD50: true, 0xDA4: true,
        0xE04: true, 0xE54: true, 0xEA0: true
    };
    var edgeReturns = { 0xB18: true, 0xB68: true, 0xBC4: true, 0xC14: true };
    function hex(n) { return ("00000000" + (n >>> 0).toString(16)).slice(-8); }
    function record(name, value) {
        var key = name + " " + value;
        if (seen[key] || count >= 512) { return; }
        seen[key] = true;
        count++;
        rows.push(key);
        fs.writefile(path, rows.join("\n") + "\n");
    }
    function overlayOffset(pc) {
        var table = mem.u32[0x800FEAA0] >>> 0;
        if (table < 0x80000000 || table >= 0x807FFE00) { return -1; }
        var base = mem.u32[table + 13 * 0x20] >>> 0;
        if (base < 0x80000000 || base >= 0x807FE000) { return -1; }
        return ((pc >>> 0) - base) | 0;
    }
    function values(offset) {
        var resolution = mem.u8[0x800FECA8];
        var segment = cpu.gpr.s2 >>> 0, type = -1;
        if (segment >= 0x80000000 && segment < 0x807FFFF6) {
            type = ((mem.u8[segment + 8] << 8) | mem.u8[segment + 9]) >>> 11;
        }
        return "ra=" + hex(cpu.gpr.ra) + " overlay13+=" + hex(offset) +
            " res=" + resolution + " widescreen=" + (resolution & 1) +
            " scope=" + mem.u8[0x80102553] +
            " centerX=" + (cpu.gpr.s4 | 0) + " centerY=" + (cpu.gpr.s3 | 0) +
            " x1=" + (cpu.gpr.a0 | 0) + " y1=" + (cpu.gpr.a1 | 0) +
            " x2=" + (cpu.gpr.a2 | 0) + " y2=" + (cpu.gpr.a3 | 0) +
            " type=" + type;
    }
    events.onexec(0x80067790, function () {
        var offset = overlayOffset(cpu.gpr.ra);
        if (!correctedReturns[offset]) { return; }
        record("reticle source", values(offset));
    });
    events.onexec(0x8006D58C, function () {
        var offset = overlayOffset(cpu.gpr.ra);
        if (correctedReturns[offset]) {
            record("reticle output", values(offset));
        } else if (edgeReturns[offset]) {
            record("edge output (not targeted)", values(offset));
        }
    });
    rows.push("Hooks installed. Source/output should keep center and Y unchanged; HUD scope should be zero.");
    rows.push("The log is replaced on each script start; at most 512 distinct source/output/edge rows are saved.");
    fs.writefile(path, rows.join("\n") + "\n");
}());
