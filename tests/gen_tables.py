import unicodedata as u, sys

def ranges(cps):
    cps = sorted(set(cps)); out = []
    for c in cps:
        if out and out[-1][1] + 1 == c: out[-1][1] = c
        else: out.append([c, c])
    return out

other_ext = [0x09BE,0x09D7,0x0B3E,0x0B57,0x0BBE,0x0BD7,0x0CC2,0x0CD5,0x0CD6,0x0D3E,0x0D57,0x0DCF,0x0DDF,
             0x1B35,0x302E,0x302F,0x1133E,0x11357,0x114B0,0x114BD,0x115AF,0x11930,0x1D165,0x1D16E,0x1D16F,
             0x1D170,0x1D171,0x1D172,0xFF9E,0xFF9F,0x200C]
prepend = [*range(0x600,0x606),0x6DD,0x70F,0x890,0x891,0x8E2,0xD4E,0x110BD,0x110CD,0x111C2,0x111C3,0x1193F,
           0x11941,0x11A3A,*range(0x11A84,0x11A8A),0x11D46,0x11F02]
ext, spacing, control = [], [], []
for c in range(0x110000):
    if 0xD800 <= c <= 0xDFFF: continue
    cat = u.category(chr(c))
    if c in (0x200D,): continue            # ZWJ handled separately
    if c in prepend: continue
    if cat in ('Mn','Me') or c in other_ext or 0x1F3FB <= c <= 0x1F3FF or 0xE0020 <= c <= 0xE007F:
        ext.append(c)
    elif cat == 'Mc' or c in (0x0E33, 0x0EB3):
        spacing.append(c)
    elif cat in ('Cc','Zl','Zp') or (cat == 'Cf' and c != 0x200C):
        if not (0xE0020 <= c <= 0xE007F): control.append(c)
    elif cat == 'Cn' and (0xE0000 <= c <= 0xE0FFF): control.append(c)   # default-ignorable unassigned
# a few Mc that are Extend by Other_Grapheme_Extend were routed above
# Wide (East Asian W/F)
wide = [c for c in range(0x110000) if u.east_asian_width(chr(c)) in ('W','F') and not 0xD800<=c<=0xDFFF]

def emit(name, rs, f):
    f.write(f"inline constexpr char32_t {name}[][2] = {{\n")
    line = "   "
    for a,b in rs:
        item = f" {{0x{a:X},0x{b:X}}},"
        if len(line)+len(item) > 78: f.write(line+"\n"); line = "   "
        line += item
    f.write(line+"\n};\n\n")

with open("grapheme_tables.inc","w") as f:
    f.write("// Generated from Python unicodedata 15.0 by gen_tables.py. Do not edit.\n")
    emit("kGcbExtend", ranges(ext), f)
    emit("kGcbSpacing", ranges(spacing), f)
    emit("kGcbPrepend", ranges(prepend), f)
    emit("kGcbControl", ranges(control), f)
    emit("kEaWide", ranges(wide), f)
print(len(ranges(ext)),len(ranges(spacing)),len(ranges(control)),len(ranges(wide)))
