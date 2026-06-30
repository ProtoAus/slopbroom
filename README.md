# slopbroom

**slopbroom** is an AI-built fork of [TrenchBroom](https://github.com/TrenchBroom/TrenchBroom),
the level editor for Quake-engine games. Effectively every change in this fork was written by an
AI coding assistant — hence the affectionate name — directed by the repository owner. It adds
editor features used by an FTEQW-based CS/CoD-style mod:

- Hammer-style **VisGroups** (multi-membership visibility groups)
- Hammer **`.vmf` / `.jmf` import**
- Visual **material-decal** and **model/sprite pickers** (browse `gfx/decals`, `models/`, `sprites/`)
- A **default texture-scale** preference + Draw-Shape toolbar control
- An **entity report** dialog
- Model/sprite **rendering** work: IQM forward-axis fix, GoldSrc studiomodel crash guard,
  image-sprite **alpha blending** + Half-Life/Source **`rendermode`** (additive/glow/color/alpha),
  and infodecal scaling

It tracks upstream TrenchBroom and stays licensed under the **GNU GPL v3** (see
[LICENSE.txt](LICENSE.txt)); all upstream copyright notices are preserved, and upstream history is
kept in git (and as the `upstream` remote).

> This is a personal, AI-built fork ("slop" — but it works well). Not affiliated with or endorsed
> by upstream TrenchBroom; please file issues here, not upstream.

---

# TrenchBroom

[![TrenchBroom Icon](app/TrenchBroom/resources/graphics/images/AppIcon.png)](https://www.youtube.com/watch?v=shcAvnYp9ow)

TrenchBroom is a modern cross-platform level editor for Quake-engine based games.

- Trailer:   https://www.youtube.com/watch?v=shcAvnYp9ow
- Website:   https://github.com/TrenchBroom/TrenchBroom
- Discord:   https://discord.gg/WGf9uve
- Mastodon:  https://mastodon.gamedev.place/@trenchbroom
- Bluesky:   https://bsky.app/profile/trenchbroom.bsky.social
- Video Tutorial Series:  https://www.youtube.com/playlist?list=PLgDKRPte5Y0AZ_K_PZbWbgBAEt5xf74aE
- Manual:    https://trenchbroom.github.io/manual/latest

## Features
* **General**
  - Full support for editing in 3D and in up to three 2D views
  - High performance renderer with support for huge maps
  - Unlimited Undo and Redo
  - Macro-like command repetition
  - Issue browser with automatic quick fixes
  - Point file support
  - Automatic backups
  - .obj file export
  - Free and cross platform
* **Brush Editing**
  - Robust vertex editing with edge and face splitting and manipulating multiple vertices together
  - Clipping tool with two and three points
  - Scaling and shearing tools
  - CSG operations: merge, subtract, intersect
  - UV view for easy texture manipulations
  - Precise texture lock for all brush editing operations
  - Multiple material collections
* **Entity Editing**
  - Entity browser with drag and drop support
  - Support for FGD and DEF files for entity definitions
  - Mod support
  - Entity link visualization
  - Displays 3D models in the editor
  - Smart entity property editors
* **Supported Games**
  - Quake (Standard and Valve 220 file formats)
  - Quake 2
  - Quake 3 (partial, no patches or brush primitives yet)
  - Hexen 2
  - Daikatana
  - Generic (for custom engines)
  - More games can be supported with custom game configurations


## Releases

Binary builds are available from [releases](https://github.com/kduske/TrenchBroom/releases).

## Compiling

Read [BUILD.md](BUILD.md) for instructions.

# Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for more information.

# Credits
- [Qt](https://www.qt.io/)
- [FreeType](https://www.freetype.org/)
- [FreeImage](https://freeimage.sourceforge.io/)
- [TinyXML](http://www.grinninglizard.com/tinyxml/)
- miniz
- [Assimp](https://www.assimp.org/)
- [Catch2](https://github.com/catchorg/Catch2)
- [CMake](https://cmake.org/)
- [vcpkg](https://www.vcpkg.io/)
- [Pandoc](https://www.pandoc.org/)
- Quake icons by [Th3 ProphetMan](https://www.deviantart.com/th3-prophetman)
- Hexen 2 icon by [thedoctor45](https://www.deviantart.com/thedoctor45)
- [Source Sans Pro](https://fonts.google.com/specimen/Source+Sans+Pro) font

## Changes

See [releases](https://github.com/TrenchBroom/TrenchBroom/releases) for latest changes.
