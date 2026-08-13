// SPDX-License-Identifier: MIT

import { access, copyFile, mkdir, readFile } from "node:fs/promises";
import { constants } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { generateGameTargets } from "./generate-game-targets.mjs";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const config = JSON.parse(await readFile(resolve(projectRoot, "upstream.json"), "utf8"));
const upstreamRoot = resolve(
  projectRoot,
  process.env.NEVERWINTER_NIM_DIR ?? config.path,
);
const nwscriptRoot = resolve(upstreamRoot, "neverwinter/nwscript");
const outDir = resolve(projectRoot, process.env.NWSC_OUT_DIR ?? "dist");

async function exists(path) {
  try {
    await access(path, constants.F_OK);
    return true;
  } catch {
    return false;
  }
}

if (!(await exists(resolve(nwscriptRoot, "native/scriptcomp.h")))) {
  throw new Error(
    "neverwinter.nim dependency is missing. Run `npm run deps` first, " +
    "or set NEVERWINTER_NIM_DIR to an existing checkout.",
  );
}

await mkdir(outDir, { recursive: true });

const generatedDir = resolve(projectRoot, "generated");
const embedded = await generateGameTargets({ projectRoot, outDir: generatedDir });

const exportedFunctions = [
  "_malloc",
  "_free",
  "_nwsc_abi_version",
  "_nwsc_create",
  "_nwsc_destroy",
  "_nwsc_add_source",
  "_nwsc_remove_source",
  "_nwsc_clear_sources",
  "_nwsc_init",
  "_nwsc_compile",
  "_nwsc_set_optimization_flags",
  "_nwsc_set_debug_output",
  "_nwsc_last_code",
  "_nwsc_error_data",
  "_nwsc_error_size",
  "_nwsc_bytecode_data",
  "_nwsc_bytecode_size",
  "_nwsc_debugcode_data",
  "_nwsc_debugcode_size",
  "_nwsc_embedded_target_count",
  "_nwsc_embedded_target_name",
  "_nwsc_embedded_target_data",
  "_nwsc_embedded_target_size",
  "_nwsc_disassemble",
  "_nwsc_disassembly_data",
  "_nwsc_disassembly_size",
  "_nwsc_disassembly_error_data",
  "_nwsc_disassembly_error_size",
  "_nwsc_inspect_ncs",
  "_nwsc_inspection_data",
  "_nwsc_inspection_size",
];

const exportedRuntimeMethods = ["HEAPU8"];

const native = resolve(nwscriptRoot, "native");
const args = [
  "-std=c++14",
  "-O3",
  "--no-entry",
  `-I${nwscriptRoot}`,
  `-I${native}`,
  "-include",
  resolve(projectRoot, "compat", "emscripten.h"),
  resolve(native, "exostring.cpp"),
  resolve(native, "scriptcompcore.cpp"),
  resolve(native, "scriptcomplexical.cpp"),
  resolve(native, "scriptcompparsetree.cpp"),
  resolve(native, "scriptcompidentspec.cpp"),
  resolve(native, "scriptcompfinalcode.cpp"),
  resolve(projectRoot, "compiler_wasm.cpp"),
  resolve(projectRoot, "ncs_disassembler.cpp"),
  embedded.sourceFile,
  "-sWASM=1",
  "-sMODULARIZE=1",
  "-sEXPORT_ES6=1",
  "-sENVIRONMENT=web,worker,node",
  "-sALLOW_MEMORY_GROWTH=1",
  "-sFILESYSTEM=1",
  "-sUSE_ZLIB=1",
  `-sEXPORTED_FUNCTIONS=${JSON.stringify(exportedFunctions)}`,
  `-sEXPORTED_RUNTIME_METHODS=${JSON.stringify(exportedRuntimeMethods)}`,
  "-o",
  resolve(outDir, "nwscript-compiler.mjs"),
];

function resolveCompiler() {
  if (process.platform !== "win32") {
    return {
      command: process.env.EMXX ?? "em++",
      prefixArgs: [],
      display: process.env.EMXX ?? "em++",
    };
  }

  // em++ is a .bat wrapper in a normal Windows emsdk install. Spawning that
  // through shell:true makes cmd.exe re-parse every source path and breaks
  // directories containing spaces. Invoke em++.py with emsdk's Python instead.
  const emsdk = process.env.EMSDK;
  const python = process.env.EMSDK_PYTHON;
  if (emsdk && python) {
    const empp = resolve(emsdk, "upstream", "emscripten", "em++.py");
    return {
      command: python,
      prefixArgs: [empp],
      display: `${python} ${empp}`,
    };
  }

  throw new Error(
    "Emscripten environment is not loaded. In PowerShell run " +
    "`D:\\Tools\\emsdk\\emsdk_env.ps1` (or your emsdk path) before `npm run build`. " +
    "The build intentionally avoids shell:true so Windows paths containing spaces remain intact.",
  );
}

const compiler = resolveCompiler();
console.log(`Building against external dependency: ${upstreamRoot}`);
console.log(`Compiler: ${compiler.display}`);
if (embedded.targets.length === 0) {
  console.log(`Embedded game targets: none (looked in ${embedded.targetRoot})`);
} else {
  console.log(`Embedded game targets: ${embedded.targets.map((target) => `${target.name} (${target.size} -> ${target.compressedSize} bytes)`).join(", ")}`);
}

const result = spawnSync(compiler.command, [...compiler.prefixArgs, ...args], {
  cwd: projectRoot,
  stdio: "inherit",
  shell: false,
});

if (result.error) {
  throw result.error;
}
if (result.status !== 0) {
  throw new Error(`Emscripten compiler failed with exit code ${result.status}`);
}

await copyFile(resolve(projectRoot, "index.mjs"), resolve(outDir, "index.mjs"));
await copyFile(resolve(projectRoot, "index.d.ts"), resolve(outDir, "index.d.ts"));

console.log(`Built NWScript WASM package in ${outDir}`);
