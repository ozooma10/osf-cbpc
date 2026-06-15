# OSF Body Physics

A **CBPC-lite** body-physics SFSE plugin for Starfield: a per-bone spring/damper
jiggle solver (breast / belly / butt) that drives the engine's own rig bones at
runtime. It is **standalone** — it works with the game's native animation and
does **not** require OSF Animation — and it reuses no third-party rig code, only
the reverse-engineered engine layout facts (see `docs/RE.md`).

> Status: **scaffold (M0)**. Builds against Starfield 1.16.244 + the CLSF `forge`
> branch. The hot path, hook, config, and solver are implemented; actor coverage
> is player-only and collision is not built yet. Roadmap in `docs/DESIGN.md`.

## How it works (one paragraph)

It installs a single vtable hook on `BGSModelNode::Update` (slot 2) — the
RE-proven, once-per-skeleton-per-frame point where a write into the flat rig
buffer survives the engine's compose+commit. Pre-orig, for each tracked actor it
reads the parent bone's world motion, runs a spring/damper per jiggle bone, and
**adds** the resulting offset to that bone's animated local transform (a *partial*
stamp — it touches only jiggle bones, which are disjoint from animation bones, so
it layers cleanly over the game's animation). Tuning comes from JSON profiles.

## Build

```bat
git submodule update --init --recursive   :: pulls lib/commonlibsf (forge)
xmake                                       :: configure (-y first time), build, deploy
```

- C++23, **xmake only** (no CMake/vcpkg). CLSF is a submodule at `lib/commonlibsf`
  on branch `forge` (fork `ozooma10/commonlibsf`).
- With `XSE_SF_MODS_PATH` set, the build deploys `OSF Body Physics.dll` and the
  bundled profiles to `%XSE_SF_MODS_PATH%\OSF Body Physics\`.
- Requires SFSE + Address Library + the version DB for 1.16.244, same as your
  other OSF plugins.

## Install / test

Enable in MO2, launch via SFSE, then read
`Documents\My Games\Starfield\SFSE\Logs\OSF Body Physics.log`. The feature report
line tells you whether the hook verified and installed on this game build.

## Config

Profiles live in `Data\SFSE\Plugins\OSF Body Physics\profiles\*.json`
(`dist\...` in this repo is the source). See `vanilla-female.json`. Each entry
binds a `bone` to a driving `parent` and sets per-axis spring tuning. These
profiles are intended to become **OSF Body contract** data, validated by its
Python tooling.

## License

GPL-3.0 (matches CommonLibSF and the rest of the OSF workspace). No NAFSF/SAF
code is included; if CLSF's modding exception permits, this could be relaxed to
MIT later.
