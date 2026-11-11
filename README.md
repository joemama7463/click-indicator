# Click Guide

Geode mod (Geode v5.11.2, GD 2.2081) that tells you when to click.

Notes slide down a rhythm-game track and hit the baseline at the exact moment you should click. Long notes mean hold. You get PERFECT / GREAT / MISS for each one. No sound effects.

## Where the clicks come from
1. **Bot macros:** pause, tap **Load Macro**, and pick a `.gdr` or `.gdr2` file (xdBot, Mega Hack and other GDR bots). Player 1 jump inputs become the route.
2. **Orb scan:** levels with no route get a note on every orb automatically (approximate, orbs only).
3. **Your own completion:** the mod records your inputs and saves them as the route when you beat the level:
- Beat it in **practice mode** and it still works: inputs after each checkpoint respawn are rewound, so the saved route is one clean run.
- Runs from a start position are not saved.
- Routes are stored per level in the mod's save folder (`routes/<levelID>.txt`).

## Settings
Orb scan on/off, note approach time, PERFECT/GREAT timing windows, re-record on completion, judgement text, scale, opacity, position.

## Build
Push to GitHub. The Actions workflow builds every platform and publishes the `.geode` on the Releases page.
