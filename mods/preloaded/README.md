# Preloaded mods

This project's mod packages. They are compiled into the game as trusted
plugins and carry no game data:

| Package | What it does | On by default |
|---|---|---|
| `bbt.quick-start` | straight to the main menu, no long intro, short loading screens | yes |
| `bbt.own-view` | every player's own camera; in a match each PC shows its own player | yes |
| `bbt.third-character` | Daffy Duck as the third player (and older developer tests) | Daffy: yes |
| `bbt.decor` | a Turkish flag at the windows of Granny's house | no |
| `bbt.shared` | hooks shared by the features above; turned on by them | (internal) |

Every package's version is `0.<N>.0`, where N is the netplay compatibility
number shared with `VERSION` (docs/WINDOWS_AND_LAN.md, section 8).

```text
packages/<package-id>/<version>/
  manifest.toml
```

The build copies `mods/preloaded/packages` next to the game executable as
`mods/bundled/`. That tree is build output: every build wipes and re-stages
it, so nothing placed there by hand survives.

Player-installed `.psxmod` archives live in `mods/installed/`, which the
launcher owns and no build touches. Install them through the launcher's Mods
manager rather than committing them here.

See `psxrecomp/docs/MOD_PACKAGES.md`.
