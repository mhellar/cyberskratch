CYBERSKRATCH MINIMAL CASE - keypad_synth_revB, two pieces of 3 mm acrylic (top + bottom plate), CO2 cut
=====================================================================================
top_plate.svg / .dxf     96 x 96 mm: covers the main board plus a pot wing beside the keypad
bottom_plate.svg / .dxf  96 x 104 mm: one rectangle under the board + keypad + wing, 4 holes under the board's
top_plate_LASER.svg      top plate with the ENGRAVE layer: black fill = engrave, red hairline = cut (engrave first,
                         then cut). top_engrave.dxf = the legend alone. Preview: top_plate_LASER.png
(true mm, red hairline = cut). Source top_plate.scad: part="plate" / "bottom" to export, "preview" for preview.png.

WHAT IT DOES
  Sits 5 mm above the main board on M3 standoffs at the board's 4 holes, just above the OLED glass (4 mm).
  Cut-outs: SuperMini + its sockets and the amp (both open to the left edge, so USB-C and speaker wires get out),
  a plain OLED window over the display, a slot over the OLED header pins, and the pot wire header.
  The pot mounts in the wing (front right, beside the keypad): its body hangs into open air there.
  The keypad stays uncovered, and the TRRS breakout stays loose on its wires.

BUY / HAVE
  4x M3 standoffs 5 mm (board top -> top plate), 4x M3 standoffs 6 mm (bottom plate -> board bottom),
  8x M3x6 screws (4 into the top plate, 4 up through the bottom plate), 4 rubber feet under the bottom plate.
  The keypad sits 5.8 mm below the board bottom, so with 6 mm standoffs it rests on the bottom plate
  (a strip of foam tape under it stops it rattling). 12.6 mm free under the pot wing for the pot body.

CHECK FIRST (1:1 paper print on the board)
  Positions come from the KiCad footprints. Check: SuperMini + amp cut-outs clear their sockets, the OLED window
  sits on the glass, and the header slot is over the OLED's 4 pins (if those pins stick up more than 4.5 mm,
  use 6 mm standoffs on top). The pot wires leave their header upwards and loop over the plate to the pot.
CO2: cut ~10-15 mm/s, 70-90 %, 1 pass, air assist, keep the film on.
     Engrave (peel the film off first) ~300 mm/s, 15-25 %, 0.1 mm interval. Cast acrylic engraves frosty white.
     Clear acrylic: mirror the engrave layer and burn it on the underside - reads through, never wears.

LEGEND (engraved, no logo): TAP / KNOB key table right of the screen, SIGNAL over the screen, FX dial around the pot,
  "< USB-C" by the SuperMini slot, and a KEYS map in front of the plate's front edge: its 4 columns sit straight
  behind the real keypad columns, left column = MODE / FX / SEQ / SHIFT (back row -> front row).
