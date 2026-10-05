// CYBERSKRATCH minimal top plate - keypad_synth_revB. ONE piece of 3 mm acrylic (CO2 cut), sitting 5 mm above the
// main board on M3 standoffs at the board's 4 holes. Cut-outs around everything taller than the OLED glass,
// a plain window over the OLED (no chimney), and a wing beside the keypad for the pot (open air under it).
//
// Coordinates = case coordinates (X left->right, Y front(keypad)->back, origin = front-left corner of the main
// board, top view). Part positions read from keypad_synth_revB.kicad_pcb (footprint pads / outlines).
// BOTTOM plate: one more 3 mm piece under the board + keypad; the board stands on it on 6 mm M3 standoffs, the
// keypad (5.8 mm below the board bottom) rests on it.
// part = "plate" | "bottom" | "engrave" (2D, export) | "preview" (3D with ghost parts)
// ENGRAVE = legend for the top plate (same content as ../overlay.scad, re-laid-out around the cut-outs). No logo.
part = "preview";
$fn = 64;

BW = 96;  BH = 66;  T = 3;  above = 5;         // plate thickness, gap board top -> plate underside (standoffs)
below = 6;                                     // standoffs board bottom -> bottom plate top
holes = [[4,4],[92,4],[4,62],[92,62]];
c = 1.0;                                       // clearance around parts

kp = [24.8, -36.0, 70.9, 0];                   // keypad module (case X/Y), hangs 6 mm lower
mcu = [3.0, 38.4, 29.1, 58.4];                 // SuperMini + its socket pads, USB-C at the left edge
amp = [1.4, 7.8, 20.8, 26.5];                  // MAX98357A on its socket (bottom kept clear of the H4 standoff)
potcon = [53.7, 17.6, 61.8, 22.4];             // 3-pin pot wire header (pads at X 55.2 57.7 60.3, Y 20.0)
oled_glass = [43.45, 30.96, 73.45, 57.56];     // OLED display area (F.Fab inner rect)
oled_hdr = [37.9, 38.9, 41.0, 49.6];           // OLED header pins (X 39.45, Y 40.4..48.1)
wing = [73.5, -30, BW, 0];                     // pot wing beside the keypad
pot = [85.0, -15.0];  pot_hole = 7.1;          // pot thread 6.5 mm

module rrect(x0, y0, x1, y1, r) { translate([x0+r, y0+r]) offset(r=r) square([x1-x0-2*r, y1-y0-2*r]); }
module box(b, g=c, r=1) { rrect(b[0]-g, b[1]-g, b[2]+g, b[3]+g, r); }
module to_left(b, g=c) { rrect(-5, b[1]-g, b[2]+g, b[3]+g, 1.5); }   // cut-out open to the left edge

module outline() {
    rrect(0, 0, BW, BH, 4);
    rrect(wing[0], wing[1], wing[2], wing[3] + 8, 4);                // wing (its back corners hide inside the board)
    translate([wing[0] - 4, -4]) difference() { square([4.01, 4.01]); circle(r=4); }   // inside corner fillet
}

module plate() {
    difference() {
        outline();
        for (h = holes) translate(h) circle(d=3.4);
        to_left(mcu);
        to_left(amp);
        box(potcon, 1.0, 1);
        box(oled_glass, 0.5, 1.5);
        box(oled_hdr, 0.8, 1);
        translate(pot) circle(d=pot_hole);
    }
}

module bottom() {
    difference() {
        rrect(0, kp[1] - 2, BW, BH, 4);            // board + keypad + wing footprint as one rectangle
        for (h = holes) translate(h) circle(d=3.4);
    }
}

// ---------- engrave legend ----------
font  = "Liberation Sans:style=Bold";
fontr = "Liberation Sans";
module tick(a, r0, r1, w=0.45) { rotate(a) translate([-w/2, r0]) square([w, r1-r0]); }
module ring2d(w=0.25) { difference() { offset(delta=w/2) children(); offset(delta=-w/2) children(); } }

// quick-reference table in the free strip right of the OLED (from overlay.scad / cyberskratch_3d)
kt_x = 76.0;  kt_x2 = 83.2;  kt_y = 56.0;  kt_lh = 2.7;  kt_sz = 1.5;  kt_w = 19.0;
kt_rows = [
    ["#", "TAP"], ["MODE", "mode"], ["FX", "voice fx"], ["SEQ", "rec"], ["SHIFT", "play / stop"], ["~", "hold = menu"],
    [" ", ""],
    ["#", "KNOB"], ["-", "fx amount"], ["SHIFT", "scratch"], ["PAD", "scratch/tune"], ["FX", "reverb"],
    ["SEQ", "tempo"], ["MODE", "volume"],
    [" ", ""],
    ["=", "SEQ+SHIFT"], ["~", "vj / hud"],
];
module key_table() {
    for (i = [0:len(kt_rows)-1]) {
        r = kt_rows[i];  y = kt_y - i*kt_lh;
        if (r[0] == "#") {
            translate([kt_x, y]) text(r[1], size=1.7, font=font, spacing=1.25);
            translate([kt_x, y-0.7]) square([kt_w, 0.25]);
        } else if (r[0] == "~") translate([kt_x2, y]) text(r[1], size=kt_sz, font=fontr);
        else if (r[0] == "=") translate([kt_x, y]) text(r[1], size=kt_sz, font=font);
        else if (r[0] == "-") { translate([kt_x, y]) text("alone", size=kt_sz, font=fontr);
                                translate([kt_x2, y]) text(r[1], size=kt_sz, font=fontr); }
        else if (r[0] != " ") { translate([kt_x, y]) text(r[0], size=kt_sz, font=font);
                                translate([kt_x2, y]) text(r[1], size=kt_sz, font=fontr); }
    }
}

// key map in the front band, its columns straight behind the real keypad columns; left column named
km_cols = [30.6, 42.1, 53.6, 65.1];  km_w = 9.0;  km_h = 2.9;  km_top = 14.6;
km_names = ["MODE", "FX", "SEQ", "SHIFT"];      // back row (nearest the board) -> front row
module key_map() {
    translate([km_cols[0] - km_w/2, km_top + 1.0]) text("KEYS", size=1.7, font=font, spacing=1.25);
    for (r = [0:3], k = [0:3]) {
        cx = km_cols[k];  y1 = km_top - r * (km_h + 0.5);  y0 = y1 - km_h;
        if (k == 0) {
            translate([cx - km_w/2, y0]) square([km_w, km_h]);                     // filled cell...
        } else ring2d() translate([cx - km_w/2, y0]) square([km_w, km_h]);
        if (k > 0) translate([cx, (y0 + y1) / 2]) circle(d=0.8, $fn=16);
    }
}
module key_map_text() {                                                           // ...with the name knocked out
    for (r = [0:3]) translate([km_cols[0], km_top - r * (km_h + 0.5) - km_h/2])
        text(km_names[r], size=1.6, font=font, halign="center", valign="center", spacing=1.1);
}

module engrave() {
    // pot dial on the wing: 11 ticks over 270 deg, ends + centre long
    translate(pot) {
        for (i = [0:10]) tick(135 - i*27, (i==0||i==5||i==10) ? 6.6 : 7.4, 8.7);
        translate([0, 9.6]) text("FX", size=2.0, font=font, halign="center");
    }
    translate([oled_glass[2] + 0.5, oled_glass[3] + 1.8]) text("SIGNAL", size=1.8, font=fontr, halign="right", spacing=1.2);
    translate([8.0, 61.0]) text("< USB-C", size=1.7, font=fontr, spacing=1.15);
    translate([24.0, 30.5]) text("KS-B", size=1.7, font=fontr, spacing=1.2);
    key_table();
    difference() { key_map(); key_map_text(); }
}

if (part == "plate") plate();
if (part == "engrave") intersection() { engrave(); plate(); }
if (part == "bottom") bottom();
if (part == "preview") {
    color("peru") translate([0, 0, -1.6]) linear_extrude(1.6) rrect(0, 0, BW, BH, 4);
    color("royalblue") translate([kp[0], kp[1], -7.4]) cube([kp[2]-kp[0], kp[3]-kp[1]-4, 1.4]);
    color("dimgray") translate([mcu[0]+1, mcu[1]+1, 0]) cube([mcu[2]-mcu[0]-2, mcu[3]-mcu[1]-2, 16.5]);
    color("purple") translate([amp[0]+1, amp[1], 0]) cube([amp[2]-amp[0]-2, amp[3]-amp[1], 11]);
    color("navy") translate([37.7, 26.3, 0]) cube([40.9, 35.8, 2.5]);
    color("black") translate([oled_glass[0], oled_glass[1], 2.5]) cube([30, 26.6, 1.5]);
    color("black") translate([potcon[0]+1, potcon[1]+1, 0]) cube([potcon[2]-potcon[0]-2, 3, 9]);
    for (h = holes) color("gold") translate([h[0], h[1], 0]) cylinder(d=5.5, h=above, $fn=6);
    color("silver") translate([pot[0], pot[1], above - 9]) cylinder(d=17, h=9);   // 12.6 mm free under the wing (down to the bottom plate)
    color("silver") translate([pot[0], pot[1], above]) cylinder(d=6, h=T + 12);
    color([0.75, 0.88, 0.95, 0.6]) translate([0, 0, above]) linear_extrude(T) plate();
    color("white") translate([0, 0, above + T]) linear_extrude(0.05) intersection() { engrave(); plate(); }
    for (h = holes) color("gold") translate([h[0], h[1], -1.6 - below]) cylinder(d=5.5, h=below, $fn=6);
    color([0.2, 0.2, 0.22, 0.9]) translate([0, 0, -1.6 - below - T]) linear_extrude(T) bottom();
}
