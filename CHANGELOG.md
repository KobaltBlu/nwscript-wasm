# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.1] - 2026-08-12

### Added

- TypeScript declaration for `NWScriptCompiler.disassemble()`.
- Runtime check of `_nwsc_abi_version` when loading the WASM module.

### Fixed

- CLI `--debug` now writes `.ndb` output from `result.debugCode`.
- `removeSource()` treats a negative WASM return as an error instead of a successful removal.
- CLI help matches the implemented flags (`--disassemble` documented; unused `--package` removed).

## [0.1.0] - 2026-08-09

### Added

- Initial WebAssembly adapter, JavaScript/TypeScript wrapper, NCS disassembler, and `nwscript-compile` CLI.
