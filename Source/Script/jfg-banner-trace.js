// Read-only JFG USA pickup-banner trace. Requires Project64's interpreter.
// Run before loading a state that shows the banner; no guest memory is changed.
(function () {
    "use strict";
    var path = pj64.installDirectory + "JfgBannerTrace.log";
    var rows = ["JFG pickup banner trace " + new Date().toISOString()];
    var seen = {}, count = 0, bannerText = null, widthCall = null;
    function hex(n) { return ("00000000" + (n >>> 0).toString(16)).slice(-8); }
    function record(name, value) {
        var key = name + " " + value;
        if (seen[key] || count >= 2048) { return; }
        seen[key] = true;
        count++;
        rows.push(key);
        fs.writefile(path, rows.join("\n") + "\n");
    }
    function overlayOffset(pc) {
        var table = mem.u32[0x800FEAA0] >>> 0;
        if (table < 0x80000000 || table >= 0x807FFE00) { return -1; }
        var base = mem.u32[table + 14 * 0x20] >>> 0;
        return ((pc >>> 0) - base) | 0;
    }
    function label(address) {
        var result = "";
        address = address >>> 0;
        if (address < 0x80000000 || address >= 0x807FFF00) { return hex(address); }
        for (var i = 0; i < 120; i++) {
            var c = mem.u8[address + i];
            if (!c) { break; }
            result += c >= 32 && c < 127 ? String.fromCharCode(c) : "\\x" + ("0" + c.toString(16)).slice(-2);
        }
        return result;
    }
    function state() {
        return " res=" + mem.u8[0x800FECA8] + " scope=" + mem.u8[0x80102553] +
            " capX=" + mem.f32[0x800FF82C] + " capY=" + mem.f32[0x800FF830];
    }
    // Read the already converted glyph stream. Calling the guest conversion or
    // width function from this trace would change emulation state, so mirror the
    // width loop at 80070728 instead. Raw ASCII cannot be measured as glyph IDs.
    function convertedWidth(address, font) {
        address = address >>> 0;
        var descriptors = mem.u32[0x80103BA4] >>> 0;
        if (font > 7 || address < 0x80000000 || address >= 0x807FFE00 ||
            descriptors < 0x80000000 || descriptors >= 0x807FFF80) { return "invalid"; }
        var widths = mem.u32[0x801040E8 + font * 4] >>> 0;
        if (widths < 0x80000000 || widths >= 0x807FFF00) { return "invalid widths"; }
        var space = mem.u8[descriptors + font * 16 + 2], width = 0;
        var bytes = [], i = 0, complete = false;
        while (i < 511) {
            var prefix = mem.u8[address + i++];
            bytes.push(("0" + prefix.toString(16)).slice(-2));
            if (!prefix) { complete = true; break; }
            var step = space;
            if (prefix & 0x80) {
                var glyph = mem.u8[address + i++];
                bytes.push(("0" + glyph.toString(16)).slice(-2));
                if (glyph !== 0 && glyph !== 15) { step = mem.u8[widths + glyph]; }
            }
            width += step;
        }
        return "font=" + font + " width=" + width + " complete=" + complete +
            " converted=" + bytes.join(" ");
    }
    events.onexec(0x8005A0D0, function () {
        if ((cpu.gpr.a0 | 0) !== 5) { return; }
        record("cap input", "ra=" + hex(cpu.gpr.ra) + state());
    });
    events.onexec(0x80041724, function () {
        if ((cpu.gpr.v1 >>> 0) !== 0x800FF820) { return; }
        record("cap vertex", "ra=" + hex(cpu.gpr.ra) + " item=" + hex(cpu.gpr.v1) +
            " x=" + (cpu.gpr.t8 | 0) + state());
    });
    events.onexec(0x800498E8, function () {
        if (overlayOffset(cpu.gpr.ra) !== 0x1B38) { return; }
        record("bar matrix", "x=" + cpu.fpr.f12 + " y=" + cpu.fpr.f14 + state());
    });
    events.onexec(0x8006FCA8, function () {
        bannerText = null;
        var offset = overlayOffset(cpu.gpr.ra);
        // Also recognize the capacity string when a wrapper changes the return
        // address. It is message 0x87 in the USA message table.
        var strings = mem.u32[0x800A51C8] >>> 0;
        var capacity = strings >= 0x80000000 && strings < 0x807FFDE0 ?
            mem.u32[strings + 0x87 * 4] >>> 0 : 0;
        if (offset !== 0x1F2C && offset !== 0x1FB8 && (cpu.gpr.a3 >>> 0) !== capacity) { return; }
        bannerText = "caller=" + hex(cpu.gpr.ra) + " offset=" + hex(offset) + " x=" + (cpu.gpr.a1 | 0) +
            " y=" + (cpu.gpr.a2 | 0) + " flags=" + mem.u32[(cpu.gpr.sp >>> 0) + 0x10] +
            " label=" + label(cpu.gpr.a3) + state();
        record("text", bannerText);
    });
    events.onexec(0x8006FFC0, function () {
        if (!bannerText) { return; }
        var window = cpu.gpr.s0 >>> 0;
        if (window < 0x80000000 || window >= 0x807FFFD8) { return; }
        record("text measured", bannerText + " " + convertedWidth(cpu.gpr.s5, mem.u8[window + 0x1D]));
    });
    events.onexec(0x800706D4, function () {
        widthCall = bannerText ? "caller=" + hex(cpu.gpr.ra) + " font=" + (cpu.gpr.a1 | 0) +
            " convert=" + (cpu.gpr.a2 | 0) : null;
    });
    events.onexec(0x80070784, function () {
        if (!widthCall) { return; }
        // The return's delay slot copies a2 into v0; a2 is final here.
        record("text native width", bannerText + " " + widthCall + " width=" + (cpu.gpr.a2 | 0));
        widthCall = null;
    });
    events.onexec(0x80070038, function () {
        if (!bannerText) { return; }
        record("text aligned", bannerText + " left=" + (cpu.gpr.s1 | 0) + " top=" + (cpu.gpr.s4 | 0));
    });
    events.onexec(0x8006FCE4, function () { bannerText = null; widthCall = null; });
    rows.push("Hooks installed; load a pickup-banner state.");
    fs.writefile(path, rows.join("\n") + "\n");
}());
