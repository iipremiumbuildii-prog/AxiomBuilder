# AxiomBuilder (LeviLamina mod)

## Build (no local toolchain needed)
1. Create a GitHub repo and push this folder to it.
2. Open the Actions tab, run "Build AxiomBuilder" (it also runs on push).
3. Download the `AxiomBuilder-mod` artifact. It contains `AxiomBuilder/` with
   `AxiomBuilder.dll` and `manifest.json`.
4. Put that `AxiomBuilder` folder in your LeviLauncher / BDS `plugins` (mods) folder.

## Build locally (Windows)
Install Visual Studio 2022 (C++ workload) and xmake, then:
    xmake repo -u
    xmake f -a x64 -m release -y
    xmake -y
Output is in `bin/AxiomBuilder/`.

## Version
Edit `LL_VERSION` at the top of `xmake.lua` to match the LeviLamina release for your game version.

## Commands (operator only, builds around your feet)
    /axiom sphere <radius> <block>     /axiom hsphere <radius> <block>
    /axiom cuboid <radius> <block>     /axiom hcuboid <radius> <block>
    /axiom undo
Block example: minecraft:stone
