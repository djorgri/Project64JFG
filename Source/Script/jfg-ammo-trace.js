// Read-only JFG USA renderer trace for Project64's debugger scripting engine.
// Requires the interpreter core. Put in <exe>/Scripts and enable autorun, or
// run from the debugger's Scripts window before loading a gameplay state.
// Writes distinct argument sets (bounded to 512) beside the executable.
(function () {
    "use strict";
    var path = pj64.installDirectory + "JfgAmmoRendererTrace.log";
    var rows = ["JFG renderer trace; read-only; started " + new Date().toISOString()];
    var seen = {};
    var count = 0;
    function hex(n) { return ("00000000" + (n >>> 0).toString(16)).slice(-8); }
    function u32(address) { return mem.u32[address >>> 0] >>> 0; }
    function flush() { fs.writefile(path, rows.join("\n") + "\n"); }
    function record(name, args) {
        var key = name + " " + args;
        if (seen[key] || count >= 512) { return; }
        seen[key] = true;
        rows.push(key);
        count++;
        flush();
    }
    function entry(name, address, stackArguments) {
        events.onexec(address, function () {
            var args = "ra=" + hex(cpu.gpr.ra) + " a0=" + hex(cpu.gpr.a0) +
                " a1=" + (cpu.gpr.a1 | 0) + " a2=" + (cpu.gpr.a2 | 0) +
                " a3=" + hex(cpu.gpr.a3);
            if (stackArguments) {
                var sp = cpu.gpr.sp >>> 0;
                args += " minDigits=" + u32(sp + 0x10) +
                    " color=" + hex(u32(sp + 0x14)) +
                    " zeroColor=" + hex(u32(sp + 0x18));
            }
            args += " resolution=" + mem.u8[0x800FECA8] +
                " hudScope=" + mem.u8[0x80102553];
            record(name, args);
        });
    }
    flush();
    entry("frontPrintNum", 0x80058EF0, true);
    events.onexec(0x80059014, function () {
        var sp = cpu.gpr.sp >>> 0;
        record("frontPrintNum geometry", "texture=" + hex(cpu.gpr.s2) +
            " left10.2=" + (cpu.gpr.t2 | 0) + " right10.2=" + (cpu.gpr.t0 | 0) +
            " sourceWidth=" + u32(sp + 0x9C) + " stride=" + u32(sp + 0xA0) +
            " stepLocal=" + hex(u32(sp + 0x88)) +
            " resolution=" + mem.u8[0x800FECA8] + " hudScope=" + mem.u8[0x80102553]);
    });
    events.onexec(0x800590F8, function () {
        record("frontPrintNum significant step", hex(cpu.gpr.s4));
    });
    events.onexec(0x8005922C, function () {
        record("frontPrintNum leading-zero step", hex(cpu.gpr.s4));
    });
    entry("fxInttostr", 0x8006D70C, false);
    entry("fxTinyPrint", 0x8006D60C, false);
    entry("tinyRender", 0x8006E188, false);
    events.onexec(0x8006ED3C, function () {
        record("tinyAdvance", "ra=" + hex(cpu.gpr.ra));
    });
    rows.push("Hooks installed; load gameplay and change ammunition.");
    flush();
}());
