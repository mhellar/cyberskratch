# CYBERSKRATCH manual

## The controls

```
   [MODE ] [ pad 0 ] [ pad 1 ] [ pad 2 ]      <- back row (nearest the screen)
   [FX   ] [ pad 3 ] [ pad 4 ] [ pad 5 ]
   [SEQ  ] [ pad 6 ] [ pad 7 ] [ pad 8 ]
   [SHIFT] [ pad 9 ] [ pad 10] [ pad 11]      <- front row
```

The **left column** keys are modifiers: **tap** one for its quick action, **hold** one and the screen shows what the
12 pads do while you hold it. The **knob** is the pot on the case's wing.

## Turning it on

Plug in USB-C. The intro film plays: a 3D scene where the cyclops sits behind the decks, the platter rises, the
laser cuts the grooves and the name gets scratched in. Then you land in the **VJ view** in DJ mode.

- **MODE + SHIFT** held ~1 s replays the intro (with credits).
- **SEQ + SHIFT** held ~0.6 s switches between the **VJ view** (3D visuals) and the classic **HUD** (scope, deck,
  sequencer lanes). The hold menus show in both.

## The three modes (tap MODE to step through)

| Mode | The 12 pads | Tap | Hold |
|---|---|---|---|
| **DJ** | 24 SAM words in two pages of 12 | say the word | tempo-synced turntable scratch (the style is on the SHIFT menu) |
| **TALK** | 12 Speak & Spell words (4 banks) | say the word | freeze the mouth on one frame, humming the chord |
| **BEAT** | 12 drums: KICK SNARE CLAP / HAT OPEN RIM / TOM-L TOM-H 808 / COWBL ZAP CRASH | hit | roll |

## Modifier keys

| Key | Tap | Hold + pad |
|---|---|---|
| **MODE** | next mode (DJ > TALK > BEAT) | modes, word page, volume, talk bank, chord progression, robot voice, replay |
| **FX** | next voice FX | CLEAN CRUSH ECHO RADIO GRAIN SWARM DEEP VOX CHOIR, plus STUTTER / REVERSE / TAPE |
| **SEQ** | record on / off | play, groove, auto, BPM, mutate, crosstalk, clears, silence |
| **SHIFT** | play / stop | per-mode extras, plus UNDERWATER and INFINITE reverb in every mode |

## The knob

| Hold | Knob does |
|---|---|
| nothing | the current voice FX's main control (pitch, bits, echo feedback, radio tuning, grain density...) |
| SHIFT | **scratch** the current word by hand, like a record under your fingers |
| a pad | scratch **that** word (DJ / TALK) or tune that drum (BEAT) |
| FX | SPACE: from dry and close to a huge, long reverb |
| SEQ | tempo |
| MODE | volume |

The knob locks when you let go of it: only a real turn wakes it again, so it doesn't drift by itself.

## Recording a loop

1. Tap **SHIFT** to start the sequencer.
2. Tap **SEQ** to switch recording on.
3. Play words, scratches and drums: they're quantized into the 16 steps.
4. Tap **SEQ** again to stop recording. Hold **SEQ** for grooves, BPM, mutate and the clears.

## The VJ view

The visuals react to whatever is playing:

- **DJ:** a solid that changes shape with every word (ico, cube, octa, torus), spun by the record. Scratch it and
  it whips. There's a grid floor, the output scope as a halo around it, and grains fly off as shards.
- **TALK:** the Speak & Spell mouth as a wireframe landscape rolling towards you (the LPC coefficients are the terrain).
- **BEAT:** a tunnel where every drum fires its own ring.
- **Hits:** a kick pulses, a snare slices the picture, a crash flashes white, and a new word shows up as a double image.
- **Each FX has its own look:** ECHO trails, RADIO scanlines, CRUSH big pixels, CHOIR particles, UNDERWATER wobble,
  GRAIN scatter, SWARM clones, DEEP vignette, VOX kaleidoscope, STUTTER freeze-frame, REVERSE negative,
  TAPE smear, INFINITE zoom feedback.

## Headphones

The MAX98357A is a bridge-tied amp. For a TRRS jack, wire tip + ring 1 to OUT+ through ~100 ohm each, and
ring 2 + sleeve to OUT-. **Never** connect the jack to board GND. Keep the volume low (MODE + knob).
