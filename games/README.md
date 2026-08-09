# Embedded game targets

Place each game's `nwscript.nss` in a subdirectory named for the target you want
exposed to JavaScript:

```text
games/
  k1/
    nwscript.nss
  k2/
    nwscript.nss
  nwn/
    nwscript.nss
```

The build discovers these directories and embeds the selected language specs
into the WebAssembly binary. Game files are intentionally not included in this
repository.

By default every discovered target is embedded. To build only selected targets,
set `NWSC_GAME_TARGETS` to a comma-separated list:

```powershell
$env:NWSC_GAME_TARGETS = "k1,k2"
npm run build
```

To keep game files elsewhere, set `NWSC_GAME_TARGETS_DIR`:

```powershell
$env:NWSC_GAME_TARGETS_DIR = "D:\KotOR\nwscript-targets"
npm run build
```

The target directory still uses the same `<target>/nwscript.nss` layout.

## Embedded size

Language specifications are compressed with zlib/DEFLATE at build time and stored
compressed in the WebAssembly binary. The selected target is decompressed lazily
into WebAssembly memory the first time it is requested. Explicit `languageSpec`
values supplied by JavaScript are not compressed or otherwise transformed.
