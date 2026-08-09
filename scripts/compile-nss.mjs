#!/usr/bin/env node

import fs from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import {
  fileURLToPath,
  pathToFileURL,
} from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const packageRoot = path.resolve(__dirname, "..");

function usage(exitCode = 0) {
  console.log(`
Usage:
  node compile-nss.mjs --package <path> --source <script.nss> --nwscript <nwscript.nss>
  node compile-nss.mjs --package <path> --source <script.nss> --game <target>

Options:
  --package <path>     Path to the compiled WASM package directory.
  --source <path>      Path to the NSS source file to compile.
  --nwscript <path>    Path to an explicit nwscript.nss language definition.
                       Takes precedence over --game.
  --game <target>      Embedded game target, e.g. k1 or k2.
  --out <path>         Output .ncs path. Defaults beside the source file.
  --debug              Request debugger output and write .ndb when produced.
  --include <dir>      Additional directory containing .nss include files.
                       May be supplied more than once.
  --list-games         Print embedded game targets and exit.
  -h, --help           Show this help.
`.trim());

  process.exit(exitCode);
}

function parseArgs(argv) {
  const args = {
    includeDirs: [],
    debug: false,
    listGames: false,
  };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];

    switch (arg) {

      case "--source":
        args.sourcePath = argv[++i];
        break;

      case "--disassemble":
        args.disassemblePath = argv[++i];
        break;

      case "--nwscript":
        args.nwscriptPath = argv[++i];
        break;

      case "--game":
        args.gameTarget = argv[++i];
        break;

      case "--out":
        args.outputPath = argv[++i];
        break;

      case "--include":
        args.includeDirs.push(argv[++i]);
        break;

      case "--debug":
        args.debug = true;
        break;

      case "--list-games":
        args.listGames = true;
        break;

      case "-h":
      case "--help":
        usage(0);
        break;

      default:
        throw new Error(`Unknown argument: ${arg}`);
    }
  }

  return args;
}

async function findPackageEntry(packagePath) {
  const resolved = path.resolve(packagePath);

  const candidates = [
    resolved,
    path.join(resolved, "index.mjs"),
    path.join(resolved, "dist", "index.mjs"),
  ];

  for (const candidate of candidates) {
    try {
      const stat = await fs.stat(candidate);

      if (stat.isFile()) {
        return candidate;
      }
    } catch {
      // Try next candidate.
    }
  }

  throw new Error(
    `Could not find package entry point under: ${resolved}\n` +
    "Expected index.mjs directly or under dist/.",
  );
}

function normalizeResRef(filePath) {
  return path
    .basename(filePath, path.extname(filePath))
    .toLowerCase();
}

async function addNssDirectory(
  compiler,
  directory,
  skipPaths = new Set(),
) {
  const absoluteDir = path.resolve(directory);
  const entries = await fs.readdir(absoluteDir, {
    withFileTypes: true,
  });

  for (const entry of entries) {
    if (
      !entry.isFile() ||
      path.extname(entry.name).toLowerCase() !== ".nss"
    ) {
      continue;
    }

    const filePath = path.join(
      absoluteDir,
      entry.name,
    );

    const normalizedPath = path
      .normalize(filePath)
      .toLowerCase();

    if (skipPaths.has(normalizedPath)) {
      continue;
    }

    const source = await fs.readFile(
      filePath,
      "utf8",
    );

    compiler.addSource(
      normalizeResRef(filePath),
      source,
    );
  }
}

async function main() {
  const args = parseArgs(
    process.argv.slice(2),
  );

  const packageEntry = path.join(
    packageRoot,
    "dist",
    "index.mjs",
  );

  const pkg = await import(
    pathToFileURL(packageEntry).href
  );

  const {
    NWScriptCompiler,
  } = pkg;

  if (!NWScriptCompiler) {
    throw new Error(
      `NWScriptCompiler was not exported by ${packageEntry}`,
    );
  }

  if (args.listGames) {
    const targets =
      await NWScriptCompiler
        .getEmbeddedGameTargets();

    if (!targets.length) {
      console.log(
        "No embedded game targets are present in this WASM build.",
      );

      return;
    }

    for (const target of targets) {
      console.log(target);
    }

    return;
  }

  if (!args.sourcePath && !args.disassemblePath) {
    throw new Error(
      "Specify --source <script.nss> or --disassemble <script.ncs>.",
    );
  }
  
  if (args.sourcePath && args.disassemblePath) {
    throw new Error(
      "--source and --disassemble cannot be used together.",
    );
  }

  if (
    !args.nwscriptPath &&
    !args.gameTarget
  ) {
    throw new Error(
      "Specify either --nwscript <path> or --game <target>.",
    );
  }

  let languageSpec;

  if (args.nwscriptPath) {
    languageSpec =
      await fs.readFile(
        path.resolve(
          args.nwscriptPath,
        ),
        "utf8",
      );
  }

  const compiler =
    await NWScriptCompiler.create({
      ...(languageSpec !== undefined
        ? {
            languageSpec,
          }
        : {
            gameTarget:
              args.gameTarget,
          }),

      writeDebug:
        args.debug,
    });

  if (args.disassemblePath) {
    try {
      const ncsPath = path.resolve(
        args.disassemblePath,
      );
  
      const ncs = new Uint8Array(
        await fs.readFile(ncsPath),
      );
  
      const asm = compiler.disassemble(ncs);

      if(!args.outputPath) {
        args.outputPath = args.disassemblePath.replace(
          /\.ncs$/i,
          ".asm",
        );
      }
  
      if (args.outputPath) {
        const outputPath = path.resolve(
          args.outputPath,
        );
  
        await fs.mkdir(
          path.dirname(outputPath),
          { recursive: true },
        );
  
        await fs.writeFile(
          outputPath,
          asm,
          "utf8",
        );
  
        console.log(`Disassembled: ${ncsPath}`);
        console.log(`ASM:          ${outputPath}`);
      } else {
        process.stdout.write(asm);
  
        if (!asm.endsWith("\n")) {
          process.stdout.write("\n");
        }
      }
  
      return;
    } finally {
      compiler.dispose?.();
    }
  }
  
  const sourcePath = path.resolve(
    args.sourcePath,
  );
  
  const sourceText = await fs.readFile(
    sourcePath,
    "utf8",
  );

  try {
    const skipPaths = new Set([
      path
        .normalize(sourcePath)
        .toLowerCase(),
    ]);

    if (args.nwscriptPath) {
      skipPaths.add(
        path
          .normalize(
            path.resolve(
              args.nwscriptPath,
            ),
          )
          .toLowerCase(),
      );
    }

    // Automatically expose .nss files
    // beside the main script as includes.
    await addNssDirectory(
      compiler,
      path.dirname(sourcePath),
      skipPaths,
    );

    // Additional include directories.
    for (
      const includeDir
      of args.includeDirs
    ) {
      await addNssDirectory(
        compiler,
        includeDir,
        skipPaths,
      );
    }

    const scriptName =
      normalizeResRef(
        sourcePath,
      );

    const result =
      compiler.compile(
        scriptName,
        sourceText,
      );

    if (!result.ok) {
      const code =
        result.code ??
        "unknown";

      const message =
        result.error ??
        result.message ??
        result.str ??
        "Compilation failed.";

      throw new Error(
        `NWScript compile failed (${code}): ${message}`,
      );
    }

    const outputPath =
      path.resolve(
        args.outputPath ??
          path.join(
            path.dirname(
              sourcePath,
            ),
            `${
              path.basename(
                sourcePath,
                path.extname(
                  sourcePath,
                ),
              )
            }.ncs`,
          ),
      );

    await fs.mkdir(
      path.dirname(outputPath),
      {
        recursive: true,
      },
    );

    await fs.writeFile(
      outputPath,
      Buffer.from(
        result.bytecode,
      ),
    );

    console.log(
      `Compiled: ${sourcePath}`,
    );

    console.log(
      `NCS:      ${outputPath}`,
    );

    console.log(
      `Size:     ${result.bytecode.byteLength} bytes`,
    );

    if (
      args.debug &&
      result.debugcode?.byteLength
    ) {
      const debugPath =
        outputPath.replace(
          /\.ncs$/i,
          ".ndb",
        );

      await fs.writeFile(
        debugPath,
        Buffer.from(
          result.debugcode,
        ),
      );

      console.log(
        `NDB:      ${debugPath}`,
      );
    }
  } finally {
    compiler.dispose?.();
  }
}

main().catch(
  (error) => {
    console.error(
      error?.stack ??
      error,
    );

    process.exitCode = 1;
  },
);