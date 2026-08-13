# NWScript Compiler WASM

A standalone WebAssembly adapter for the NWScript compiler from [`niv/neverwinter.nim`](https://github.com/niv/neverwinter.nim).

**The upstream repository is a dependency of this project.**

The port owns only the WASM ABI, JavaScript/TypeScript wrapper, NCS disassembler, and build glue. At build time it compiles the native NWScript compiler sources directly from an external `neverwinter.nim` checkout.

## Dependency model

The canonical repository layout is:

```text
nwscript-wasm/
  compiler_wasm.cpp
  ncs_disassembler.cpp
  index.mjs
  index.d.ts
  package.json
  upstream.json
  games/
    k1/nwscript.nss
    k2/nwscript.nss
  scripts/
    build.mjs
    compile-nss.mjs
    fetch-upstream.mjs
  deps/
    neverwinter.nim/    <- external git dependency / submodule
```

`.gitmodules` declares `deps/neverwinter.nim` as a submodule. `upstream.json` pins the compiler revision used by this port so builds are reproducible.

The current pinned revision is:

```text
db755db7fdfa3e4245e30b2aebfbae6e75b6ea77
```

You can point the build at another checkout without copying it into this project:

```bash
NEVERWINTER_NIM_DIR=/path/to/neverwinter.nim npm run build
```

You can also test a different upstream revision when fetching the dependency:

```bash
NEVERWINTER_NIM_REF=master npm run deps
```

## Setup

With git metadata available:

```bash
git clone --recurse-submodules <this-repo>
cd nwscript-wasm
npm run build
```

Or let the project initialize/fetch the dependency:

```bash
npm run deps
npm run build
```

When distributed as a zip, `npm run deps` clones `niv/neverwinter.nim` into `deps/neverwinter.nim` because archive files cannot preserve a git submodule gitlink.

The Emscripten SDK must be installed and activated so `em++` is available on `PATH`.

A complete rebuild of the upstream dependency and WASM package can be performed with:

```bash
npm run rebuild
```

## Testing

The suite uses Vitest and exercises the published `dist/` package, so a WASM build must exist first:

```bash
npm run build
npm test
```

`npm test` copies `index.mjs` and `index.d.ts` into `dist/` before running, so JavaScript wrapper edits are tested without rebuilding the WASM module. Compile tests use the original language specification under `test/fixtures/` and do not require game `nwscript.nss` files.

Watch mode:

```bash
npm run test:watch
```

## What gets compiled

The build uses the upstream compiler translation units directly from the dependency:

```text
neverwinter/nwscript/native/exostring.cpp
neverwinter/nwscript/native/scriptcompcore.cpp
neverwinter/nwscript/native/scriptcomplexical.cpp
neverwinter/nwscript/native/scriptcompparsetree.cpp
neverwinter/nwscript/native/scriptcompidentspec.cpp
neverwinter/nwscript/native/scriptcompfinalcode.cpp
```

`compilerapi.cpp` is intentionally not linked. `compiler_wasm.cpp` is the WASM-specific host adapter around `CScriptCompiler`.

NCS disassembly is exposed through the same WASM module. The lightweight `ncs_disassembler.cpp` implementation follows the instruction decoding semantics used by the upstream NWScript tooling without pulling the Nim runtime into the WASM artifact.

The Nim runtime and `nwn_script_comp.nim` CLI are not part of the WASM artifact.

## Embedded game language specifications

The WASM build can include one or more game-specific `nwscript.nss` files so consumers do not need to provide the standard language specification at runtime.

Game assets are not distributed by this project. Place your own copies under:

```text
games/
  k1/nwscript.nss
  k2/nwscript.nss
```

Any directory name is a valid target name as long as it contains only letters, numbers, `.`, `_`, or `-`. This lets downstream builders add targets such as `nwn`, `nwn2`, or custom language specifications without changing the source code.

By default, `npm run build` embeds every discovered target.

Because `nwscript.nss` files are relatively large and compress well, embedded language specifications are compressed at build time before being stored in the WASM binary. They are decompressed in memory only when requested by the JavaScript wrapper.

This keeps support for multiple built-in game targets from unnecessarily bloating the final WASM binary.

Restrict a build with `NWSC_GAME_TARGETS`:

```powershell
$env:NWSC_GAME_TARGETS = "k1,k2"
npm run build
```

Or point at a different target root:

```powershell
$env:NWSC_GAME_TARGETS_DIR = "D:\GameLanguageSpecs"
npm run build
```

The external directory uses the same `<target>/nwscript.nss` layout.

At runtime, select an embedded target:

```js
const compiler = await NWScriptCompiler.create({
  gameTarget: "k1",
});
```

You can inspect what a particular build contains:

```js
const targets = await NWScriptCompiler.getEmbeddedGameTargets();
// ["k1", "k2"]
```

An explicit `languageSpec` always overrides `gameTarget`, so applications can replace the bundled copy without rebuilding the WASM module:

```js
const compiler = await NWScriptCompiler.create({
  gameTarget: "k1",
  languageSpec: myModifiedNwscriptNss,
});
```

This is useful for custom games, modified language specifications, or applications that want complete control over the compiler environment.

## JavaScript usage

### Compile using an embedded game target

```js
import {
  NWScriptCompiler,
  OptimizationFlags,
} from "./dist/index.mjs";

const compiler = await NWScriptCompiler.create({
  gameTarget: "k1",
  optimizationFlags: OptimizationFlags.O1,
});

const result = compiler.compile(
  "example",
  `
void main() {
  PrintString("Hello from WASM");
}
`,
);

if (!result.ok) {
  console.error(
    result.code,
    result.error,
  );
} else {
  const ncs = result.bytecode;
  const ndb = result.debugCode;
}

compiler.dispose();
```

### Compile using an explicit language specification

```js
const compiler = await NWScriptCompiler.create({
  languageSpec: nwscriptNssText,
  optimizationFlags: OptimizationFlags.O1,
});
```

The explicit language specification takes precedence over any `gameTarget`.

### Includes

Includes can be registered before compilation:

```js
compiler.addSource(
  "my_include",
  includeText,
);

const result = compiler.compile(
  "example",
  `
#include "my_include"

void main() {
  MyIncludeFunction();
}
`,
);
```

Multiple sources can be registered together:

```js
compiler.addSources({
  k_inc_utility: utilitySource,
  k_inc_debug: debugSource,
});
```

## NCS disassembly

Compiled NCS bytecode can be disassembled directly through the same WASM module:

```js
import fs from "node:fs";
import {
  NWScriptCompiler,
} from "./dist/index.mjs";

const compiler = await NWScriptCompiler.create({
  gameTarget: "k1",
});

const ncs = new Uint8Array(
  fs.readFileSync("./test.ncs"),
);

const asm = compiler.disassemble(ncs);

console.log(asm);

compiler.dispose();
```

The disassembler produces assembly containing instruction offsets, opcodes, operands, branch targets, and ACTION instructions.

Structured inspection uses the same decoder and reports header metadata, code/file offsets, and semantic operand ranges without requiring the caller to parse the text listing:

```js
const inspection = compiler.inspectNcs(ncs);
console.log(inspection.header.size, inspection.instructions[0].mnemonic);

const standalone = await NWScriptCompiler.inspectNcs(ncs);
```

When a language specification is available, ACTION IDs are resolved to their symbolic NWScript function names:

```text
00000024  05 00 00C8 02  ACTION GetObjectByTag(00C8), 02
00000029  05 00 023A 01  ACTION GetHasInventory(023A), 01
```

Branch destinations are labeled to make control flow easier to follow.

Disassembly reconstructs NCS assembly. It does **not** reconstruct original NSS source code and should not be confused with an NCS-to-NSS decompiler.

## Command-line usage

The package includes the `nwscript-compile` command for compiling NSS files and disassembling NCS files.

### Compile an NSS script

Using an embedded game target:

```bash
npx nwscript-compile --source test.nss --game k1
```

By default, the resulting `test.ncs` is written beside the source file.

Specify an output path with:

```bash
npx nwscript-compile \
  --source test.nss \
  --game k1 \
  --out output/test.ncs
```

Use an explicit `nwscript.nss` instead of an embedded target:

```bash
npx nwscript-compile \
  --source test.nss \
  --nwscript ./nwscript.nss
```

Additional include directories can be supplied with:

```bash
npx nwscript-compile \
  --source test.nss \
  --game k1 \
  --include ./includes
```

`--include` may be specified multiple times.

NSS files located beside the main source file are automatically made available to the compiler as include resources.

### Generate debug output

Use:

```bash
npx nwscript-compile \
  --source test.nss \
  --game k1 \
  --debug
```

When the compiler produces debugger data, an `.ndb` file is written beside the generated `.ncs`.

### List embedded game targets

```bash
npx nwscript-compile --list-games
```

Example:

```text
k1
k2
```

### Disassemble an NCS file

Print assembly directly to stdout:

```bash
npx nwscript-compile \
  --disassemble test.ncs \
  --game k1
```

Write it to a file:

```bash
npx nwscript-compile \
  --disassemble test.ncs \
  --game k1 \
  --out test.asm
```

An explicit language specification can also be used:

```bash
npx nwscript-compile \
  --disassemble test.ncs \
  --nwscript ./nwscript.nss
```

The selected language specification is used to resolve ACTION IDs to their corresponding function names.

`--source` and `--disassemble` are mutually exclusive.

## Resource behavior

The upstream compiler already abstracts resource I/O through `CScriptCompilerAPI`. The WASM adapter provides an in-memory implementation of those callbacks.

`SetIdentifierSpecification()` consumes the language specification during initialization, so `NWScriptCompiler.create()` registers `nwscript.nss` before initializing the native compiler.

Includes must be available synchronously during a compile. If your application resolves game resources asynchronously, preload the required NSS sources before calling `compile()`.

Names are normalized as resource names:

- case-insensitive
- path components removed
- `.nss` extension optional

This allows:

```js
compiler.addSource(
  "k_inc_debug",
  source,
);
```

to satisfy:

```c
#include "k_inc_debug"
```

## Compiler optimization

The wrapper exposes the upstream compiler optimization flags:

```js
import {
  OptimizationFlags,
} from "./dist/index.mjs";
```

Available presets are:

```js
OptimizationFlags.O0
OptimizationFlags.O1
OptimizationFlags.O2
OptimizationFlags.O3
```

Individual optimization flags are also exposed:

```js
OptimizationFlags.RemoveDeadCode
OptimizationFlags.MeldInstructions
OptimizationFlags.RemoveDeadBranches
```

For example:

```js
const compiler = await NWScriptCompiler.create({
  gameTarget: "k1",
  optimizationFlags: OptimizationFlags.O3,
});
```

## Output

A successful build produces:

```text
dist/
  nwscript-compiler.mjs
  nwscript-compiler.wasm
  index.mjs
  index.d.ts
```

NCS and optional NDB bytes are captured in memory and exposed as `Uint8Array` values.

Normal compilation does not write scripts or compiler output to Emscripten's virtual filesystem.

Filesystem support remains enabled because the upstream compiler still contains optional Graphviz `FILE*` code. The WASM adapter leaves Graphviz disabled.

## Concurrency

Compilation is synchronous and non-reentrant inside one Emscripten module instance because the upstream callback ABI has no user-data pointer.

For parallel compilation, create one module/compiler instance per Web Worker.

Do not share a single `NWScriptCompiler` instance between concurrent compilation operations.

## Updating upstream

Change the pinned `ref` in `upstream.json`, then run:

```bash
npm run deps
npm run build
```

Or:

```bash
npm run rebuild
```

Keeping the upstream compiler external means compiler fixes can be adopted by moving the dependency revision instead of copying patches between projects.

## License

The adapter code is MIT-licensed.

The linked WebAssembly output includes the upstream NWScript compiler and remains subject to the upstream project's overall GPL-3.0 licensing.
