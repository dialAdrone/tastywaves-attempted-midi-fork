# VAVEWave navigation and usability plan (proposal, 2026-10-20)

Status: **plan approved 2026-10-20 with the decisions in section 5; U1 written (host-tested, device test: DEVICE-TEST-PLAN section 19).** It reorganises how the existing parameters are reached, so no sound,
algorithm, patch format or MIDI CC changes. Saved patches load and sound exactly as before.

Reference: BaudGirl FM-1+VA (baudgirl.com/work/FM-1+VA, quick start and manual). It uses one model everywhere: SELECT moves
a highlight, ALGORITHM changes the highlighted value, SEL enters, HOME goes back, and KNOB1-4 play a "knob bank". Questions
default to Cancel, and the same parameter reads the same everywhere.

## 1. What is wrong today (from the code and the screens)

### 1.1 Six different ways to move inside a page

| Where | How you reach the next part |
| --- | --- |
| EDIT, FX, ENV, LFO, GLO | press the page button again (wraps) |
| EDIT | also: hold EDIT + black key F#3/G#3/A#3/C#4 |
| ARP, SEQ | hold 0.5 s to open (a tap switches it on or off); hold again for the second set |
| HOME | press again on HOME changes the *look*, not the knobs; hold 0.7 s = HELP |
| HELP | HOME steps the pages |
| GLO set 3 | a hidden diagnostic (BOOT KEYS) reached by a third press |

### 1.2 You cannot see where you are or what comes next

- EDIT shows chips (OSC1 OSC2 SUB NOISE). FX says `FILTER 1/2` although it has 6 sets, and the slots say `FX SLOT 1`
  with no count. ENV says `ENV AMP` with no count. GLO says `1/3`. SEQ shows nothing.
- A page always reopens on its first set, so FX slot 3 is four presses away every time.

### 1.3 Parameters are grouped by accident, not by function

- **FX** opens the *filter*. The effects are sets 3-6, behind two filter sets.
- **The second envelope** is called FILTER ENV, but it also moves position and BEND. Its ADSR is on ENV 2, its cutoff
  amount (FENV) on FX 1, and its POS and BEND amounts on FX 2.
- **Modulation is spread over five pages:** LFO (its amounts), FX 2 (envelope amounts), GLO 2 (wheel, aftertouch,
  velocity), SEQ 2 (the lane), SEL (BEND destination).
- **Position modulation alone is on four pages:** LFO>POS, E>POS, MW>POS and the lane. Position is the main control of a
  wavetable synth.
- **GLO ("global") holds patch parameters** (volume, drive, glide, bend range, wheel, aftertouch, velocity). The real
  device settings (HOME look, CPU max, boot keys) are scattered or hidden.
- **SEL** is a grab bag: voice mode, BEND destination (an oscillator setting), quality, envelope curve.
- **BEND** sits on every EDIT set, but its destination is on SEL.
- **Unreachable or empty:** OSC1 and OSC2 coarse tune (SEMI) have no knob (MIDI CC only). SUB, NOISE and ARP 2 leave knobs
  empty.
- **HELP is out of date:** "FX FILTER 1, 2", "GLO ... MIDI", and no ARP, SEQ or effects lines on page 2.

### 1.4 Hidden or overloaded gestures

The tap-or-hold rule on ARP and SEQ differs from every other page button. REC hold = clear, which is destructive and
asks nothing. EDIT + HOME resets the CPU max. GLO x3 opens the diagnostic. PLAY is latch or sequencer start depending on
state. SELECT moves the position everywhere except the lane screen, where it picks the step.

### What already works (keep it)

- **Four fixed knobs per screen, value on top.** It is fast for performance, and you confirmed this.
- **ALGORITHM = wavetable, SELECT = position, from any page.** This is the wavetable equivalent of BaudGirl's "ALGORITHM
  is the engine's core control", and it should stay the one fixed rule.
- **PRESETS browses, SAVE behaves well** (press again to confirm, any other button cancels).
- **The pictures:** the filter curve, the envelopes, the wave as read, the step grid.

## 2. Principles for the new model

We keep the knob-per-screen model and do not copy BaudGirl's list model: a list needs SELECT for scrolling, which would
take the position knob away from performance. We do take its consistency rules.

1. **Every page button works the same way.** Press = go to the page, opening on the tab you last used. Press again = next
   tab (wraps).
2. **Every page has a tab strip in the same place**, like the EDIT chips today: all tab names, the current one lit. The
   status line shows `PAGE  TAB  n/N`.
3. **Each control has one job everywhere.**
   - ALGORITHM = table of the oscillator shown (OSC1 unless the OSC2 tab is shown).
   - SELECT = position, except on a screen that shows a step cursor, where it moves the cursor (one stated exception,
     with the cursor always drawn).
   - KNOB1-4 = the four values printed above them.
   - PRESETS = patches.
4. **HOME = back to HOME, from anywhere, always.** Hold HOME = help for the page you are on.
5. **Anything destructive asks first.** "Press the same button again to confirm, any other cancels, 5 s timeout" applies
   to SAVE (already), delete (already) and REC clear (new).
6. **One home per parameter.** A parameter has one screen. The only deliberate duplicate is the CLOCK tab, which ARP and
   SEQ both show because they share it.
7. **No empty knobs** where a related parameter could fill them. **At most 4 tabs per page.**

## 3. Proposed page map

Signal-flow order, as wavetable synths usually present it (oscillators, filter, envelopes, LFO, modulation, effects,
voice, arp/seq). Button labels on the FM-1 are fixed, so the mapping works with them.

| Button | Page | Tab | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- | --- | --- |
| HOME | HOME | PLAY | CUTOFF | RESO | ATK | REL |
| EDIT | OSC | OSC1 | POS | SEMI **(new knob)** | FINE | LEVEL |
| | | OSC2 | POS | SEMI **(new knob)** | FINE | LEVEL |
| | | MIX | SUB | NOISE | BEND | BEND> (dest) |
| | | FILTER | CUTOFF | RESO | MODE | KEYTRK |
| FX | EFFECTS | SLOT 1..4 | TYPE | A | B | MIX |
| ENV | ENV | AMP | ATK | DEC | SUS | REL |
| | | MOD (was FILTER ENV) | ATK | DEC | SUS | REL |
| | | MOD> | >CUT (FENV) | >POS | >BEND | CURVE |
| LFO | LFO | LFO>1 | SHAPE | RATE | >POS | >PITCH |
| | | LFO>2 | SHAPE | RATE | >CUT | >AMP |
| SEL | VOICE | VOICE | MODE | GLIDE | PB RNG | QUAL |
| | | CONTROL | MW>POS | MW>VIB | AT>CUT | VEL>AMP |
| | | VELO | VEL>CUT (FVEL) | VOLUME | DRIVE | - |
| GLO | SYSTEM (device, not patch) | SETUP | HOME LOOK | TEMPO | - | - |
| | | INFO | firmware version, CPU max (KNOB to reset), boot keys | | | |
| ARP | ARP | ARP | MODE | OCT | RATE | GATE |
| | | CLOCK | RATE | GATE | SWING | TEMPO |
| SEQ | SEQ | CLOCK | RATE | GATE | SWING | TEMPO |
| | | LANE | TARGET | DEPTH | GLIDE | VALUE (SELECT = step cursor) |

What changes compared with today:

- **FX means effects.** The filter moves to EDIT next to the oscillators, where it sits in the signal path. FILTER on
  HOME still gives CUTOFF and RESO at once.
- **The second envelope** becomes MOD ENV, with all of its amounts on the tab beside it.
- **SEMI1 and SEMI2 get knobs.** BEND appears once, with its destination next to it.
- **GLO becomes real global settings.** It holds what is not saved in a patch: HOME look, tempo (or keep tempo per patch,
  see the decisions), info. VOLUME and DRIVE move to SEL because they are saved in the patch.
- **SEL holds voice and performance control:** the voice mode plus everything the wheel, aftertouch and velocity do.
- **Fewer presses.** The FX slots go from presses 3-6 to presses 1-4. With tab memory, a slot you return to is one press
  away.
- **Position modulation is two pages instead of four.** LFO>POS stays on LFO, and the lane stays on SEQ. E>POS joins
  MOD>, and MW>POS joins CONTROL. Option U4.2 shows the live position on screen.

## 4. Gestures, consolidated

| Control | Press | Hold | Notes |
| --- | --- | --- | --- |
| Page button | open (last tab) / next tab | ARP, SEQ: on/off (see decision D1) | one rule for all |
| HOME | back to HOME (on HOME: next HOME bank, if U4.1; else nothing) | help for this page | look cycling moves to GLO SETUP |
| SAVE | save (again = confirm a replace) | + PRESETS: pick the id; 2 s on a user patch: delete? | unchanged |
| REC | step input on/off | clear? (REC again to confirm) | confirm is new |
| PLAY | latch, or start/stop the pattern when SEQ is on | - | unchanged, the LED shows which |
| OCT-/OCT+ | octave | - | unchanged |
| EDIT + black keys | removed (tabs cover it), or kept as a shortcut | | D4 |
| EDIT + HOME | removed (GLO INFO resets the CPU max) | | |

## 5. Decisions (user, 2026-10-20)

D1 (a): press opens, hold switches ARP/SEQ on or off. D2: tempo stays per patch (so GLO SETUP has no TEMPO; tempo stays on the CLOCK tab). D3 (a): HOME on HOME does nothing (no knob banks; U4.1 dropped). D4: the EDIT + black key shortcut is dropped. D5: tab memory for the session only; only patches are saved.

The options as they were put:

- **D1. ARP/SEQ on/off.** (a) **Recommended:** press opens the page like every other button, and hold (0.4 s) switches it
  on or off, with the LED showing the state. This is one rule, and "hold = switch on/off" is also BaudGirl's convention
  (ENV hold, PLAY hold). The cost: switching on or off during a performance takes a short hold instead of a tap.
  (b) Keep today's tap = on/off, hold = page, and accept the one exception.
- **D2. Tempo:** per patch (today; a patch remembers its tempo) or global (GLO, BaudGirl-style; the tempo stays when you
  change patch). Changing it is a small patch-format question: the stored value would be ignored, not removed.
- **D3. HOME again on HOME:** (a) nothing, (b) **recommended later (U4.1):** next HOME knob bank (PLAY = CUTOFF RESO ATK
  REL; MOVE = LFO RATE, LFO>POS, E>POS, BEND; SPACE = DRIVE and the effect mixes). Either way, the look moves to GLO.
- **D4.** Keep EDIT + black key as a shortcut to OSC1/OSC2/MIX/FILTER, or drop it.
- **D5. Tab memory:** for the session only (**recommended**; power-on starts on tab 1), or saved in the settings.

## 6. Stages

Each stage is shippable alone, host-tested with a screenshot sheet of every page and tab, and has a DEVICE-TEST-PLAN
section. It ends with updates to `UI-DESIGN.md` and HELP. None of them touches the audio ISR, the patch record or the CC map.

- **U1. The frame (no parameter moves).** Done in code: tab strip at y 54 on every page but HOME (pictures moved to y 72, 44 rows; ENV curves 54 rows; SEQ tiles 18 rows), `PAGE n/N` on the status line, tab memory (`pn.tab`, RAM only), REC hold asks `CLEAR PATTERN?`, HELP page 1 generated per page from `PG` (the page HOME was held on), page 2 the buttons. Tab names for now: EDIT OSC1 OSC2 SUB NOISE; FX FLT1 FLT2 FX1-FX4; ENV AMP FILTER; LFO POS PITCH, CUT AMP; GLO OUTPUT CONTROL BOOT; ARP ARP TIME; SEQ NOTES LANE.
- **U1b. Picture first (done in code).** The middle list of all eight values is gone (it repeated the knobs); each tab's picture fills y 66..199. Knob row 36 rows (values FONT_M 12x24), tab strip y 38 with a set tag under the tab shown when a tab holds two knob sets, status y 202, patch name FONT_M at y 218. Tabs are pictures now: FX FILT (2 sets) FX1-FX4; LFO one tab (2 sets); ARP one tab (2 sets); the others one set a tab (`TABG` in panel.h). New pictures: FX slot chain, GLO CONTROL routes, ARP pattern, SEL voice blocks; ENV shows the tab's envelope only.
  - Tab strip and `PAGE TAB n/N` on every page.
  - Tab memory.
  - Fix the wrong labels (FX `1/2` of 6, missing counts on ENV and SEQ).
  - REC clear asks first.
  - HELP rewritten and per page.
  - Small, low risk, and it removes the "where am I" problem at once.
- **U2. The page map** (section 3). This means a new `PG` table in `panel.h`, the SEMI knobs, and moving the filter tab and
  the MOD ENV amounts. The FX, VOICE and GLO pages need new middle pictures (the filter picture moves with the FILTER tab).
- **U3. Gestures** (section 4 and decisions D1, D3, D4). HOME's look cycling goes to GLO, and the hidden combinations are
  removed.
- **U4. Options, after U1-U3 are on the device.**
  - U4.1: HOME knob banks.
  - U4.2: live position marker on EDIT and HOME. `eng_pos_now()` exists; it was left open in UI-DESIGN.
  - U4.3: edit a recorded note step on the SEQ grid (SELECT cursor, knobs: note, tie, rest).
  - U4.4: hold FX = bypass the chain.

Effort guess: U1 small, U2 medium (mostly tables and pictures), U3 small, U4 each small to medium.

- **U3 part done early (2026-10-24): D1 (a) in code.** ARP and SEQ: a press opens the page (again: the next tab / set), a hold of 500 ms switches on / off (at the half second, the release does nothing more). Help texts follow.
