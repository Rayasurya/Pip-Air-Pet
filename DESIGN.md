# Air Pet: character design (v3, "Pip")

- **v1:** a solid blob with dot eyes. Too basic, and some moods could not be read at a glance.
- **v2:** an outline blob with big lit eyes, a cause bubble and acted loops. Readable, but generic. The code is kept in `art/pet-v2-blob.cpp.txt`.
- **v3, Pip:** a sprout creature chosen from 6 generated concepts (Gemini). The expression sheet is in `art/Pip_Sprout_Expression_Sheet.png`. Pip won because the leaves change the whole outline, so the mood reads before you even see the face. Chirp the canary had the better story, but its moods looked alike and the feather detail turned to noise at 1-bit.

At native size the sheet's Pip is only about 26x34 px (`art/pip-native/`), too small for a readable face on the OLED. So Pip is redrawn in code at about 2x: a bulb body, stubby feet, a stem, and two almond leaves with a midrib. The leaves are drawn along a bending line (angle, bend, length, width). That lets them droop, curl, fan and flap, and morph smoothly between moods.

The rules from v2 still apply:

## The rules

1. **Eyes carry the emotion.** Big lit eyes, shaped by eyelids. A tilted upper lid reads as worried (inner end up) or tired and cross (inner end down). A curved lower lid makes smiling eyes. Special eyes only where a shape says it fastest: hearts (loved), hollow eyes with brows (panic), `> <` (yawn), closed arcs (asleep).
2. **Every mood also has its own mouth.** The face research below suggests eyes carry fear, sadness and anger, while the mouth carries happiness. So no two moods share both eyes and mouth.
3. **The face says how it feels. The bubble says why.** A thought bubble shows the cause in one word: `CO2`, `HOT`, `DRY`, `DAMP`. When gasping, it alternates `CO2` with a window icon (the fix).
4. **Comic symbols back up the face:** flying sweat drops (stress), wavy heat lines, `!`, `Zzz`, floating hearts, sparkles.
5. **Each mood is a short acted loop, not a pose.** The pet does things, so it feels alive.

Pip has a **solid body**, as in the sheet, because the silhouette is its charm. That keeps more pixels lit than v2's outline body did. To limit OLED burn-in, Pip never holds still (bobbing and swaying), and the screen dims while it sleeps. If burn-in shows up during the week of living with it, switch to an outline body.

## What each mood does

| Mood | Leaves | Face | Acting | Bubble |
|------|--------|------|--------|--------|
| Happy | Upright V, swaying | Round eyes with a glint, smile, blush | 12 s loop: looks around, double blink, hops (leaves trail), curious look, waves a leaf | none |
| Stuffy (CO2 1,000+) | Sag further as it gets drowsy | Heavy, slightly worried lids, small frown | 20 s loop: droops, nods off, jolts awake (leaves spring up), yawns with `> <` and lifts its leaves | `CO2` |
| Gasping (CO2 1,500+) | Flapping | "O" eyes, tiny shaking pupils, worried brows, gasping mouth | Shakes, sweat flies, `!` blinks | `CO2` / window |
| Hot | One hanging, one fanning | Tired lids, panting, tongue out | Fans itself, heat lines, sweat drips | `HOT` |
| Dry | Thin and curled | Tired squint, cracked lips | Glances up at the bubble | `DRY` |
| Damp | Heavy, hanging past the sides | Worried droopy eyes, wavy mouth | Body squashes, slow heavy blinks, drips | `DAMP` |
| Asleep | Folded down like a blanket | Closed arcs, tiny smile | Breathes, `Zzz`, screen dims | none |
| Relieved | Spread wide, then settle | Smiling eyes, big grin | Big stretch, sigh puff, sparkles | none |
| Loved | Perky, bouncing | Pulsing heart eyes, blush | Wiggles, hearts float up | none |

Mood changes blend over 0.5 s: the body and eyelids morph, and the pet blinks while the face features swap.

## Sources

- **Comic symbols.** Mort Walker, *The Lexicon of Comicana* (1980). It names the symbols used here: plewds (flying sweat drops for stress), indotherms (wavy rising heat lines) and crottles (X eyes). Summary checked on [Wikipedia](https://en.wikipedia.org/wiki/The_Lexicon_of_Comicana); the book itself was not read.
- **Which part of the face carries which emotion.** Wegrzyn et al., "Mapping the emotional face", *PLOS ONE* (2017), [link](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC5426715/). Only the summary was checked: eyes matter most for sadness, fear and anger, and the mouth for happiness.
- **Robot pets with only eyes.** Anki's Cozmo team on animating a physical robot: GDC 2017, "Cozmo: Animation Pipeline for a Physical Robot" ([abstract](https://gdcvault.com/play/1024221/Cozmo-Animation-Pipeline-for-a)). Only the abstract was checked. The eyelid-shaped eye style here is inspired by Cozmo and Vector. It is not a claim about Anki's internal method.
- **OLED burn-in.** Fewer static lit pixels plus moving content is common maker advice ([Arduino forum thread](https://forum.arduino.cc/t/oled-screen-burn-in-how-to-prevent-this/929884)). This is community advice, not a measured result.

## Honesty notes

- No user testing has happened. "Readable at a glance" is a design goal. Test it by showing the stills to 3 to 5 people and asking what each pet feels. Do that before claiming it in a case study.
- The preview uses a smaller font than the OLED, so text on the device looks larger.
