import { mkdtemp, mkdir, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { generateGameTargets } from "../scripts/generate-game-targets.mjs";

async function withEnv(values, fn) {
  const previous = {};
  for (const key of Object.keys(values)) {
    previous[key] = process.env[key];
    const value = values[key];
    if (value === undefined) {
      delete process.env[key];
    } else {
      process.env[key] = value;
    }
  }
  try {
    return await fn();
  } finally {
    for (const [key, value] of Object.entries(previous)) {
      if (value === undefined) {
        delete process.env[key];
      } else {
        process.env[key] = value;
      }
    }
  }
}

describe("generateGameTargets", () => {
  afterEach(async () => {
    await withEnv(
      {
        NWSC_GAME_TARGETS_DIR: undefined,
        NWSC_GAME_TARGETS: undefined,
      },
      async () => {},
    );
  });

  it("emits kTargetCount 0 when the games directory is missing", async () => {
    const root = await mkdtemp(join(tmpdir(), "nwsc-games-"));
    const outDir = join(root, "generated");
    await withEnv(
      { NWSC_GAME_TARGETS_DIR: undefined, NWSC_GAME_TARGETS: undefined },
      async () => {
        const result = await generateGameTargets({ projectRoot: root, outDir });
        const source = await readFile(result.sourceFile, "utf8");
        expect(source).toMatch(/kTargetCount = 0/);
        expect(source).toMatch(/nwsc_embedded_target_count/);
        expect(result.targets).toEqual([]);
      },
    );
  });

  it("embeds a discovered target as a zlib blob with export hooks", async () => {
    const root = await mkdtemp(join(tmpdir(), "nwsc-games-"));
    await mkdir(join(root, "games", "k1"), { recursive: true });
    await writeFile(join(root, "games", "k1", "nwscript.nss"), "void PrintString(string sString);\n");
    const outDir = join(root, "generated");

    await withEnv(
      { NWSC_GAME_TARGETS_DIR: undefined, NWSC_GAME_TARGETS: undefined },
      async () => {
        const result = await generateGameTargets({ projectRoot: root, outDir });
        const source = await readFile(result.sourceFile, "utf8");
        expect(result.targets).toHaveLength(1);
        expect(result.targets[0].name).toBe("k1");
        expect(source).toMatch(/"k1"/);
        expect(source).toMatch(/kTargetData0/);
        expect(source).toMatch(/kTargetCount = 1/);
        expect(source).toMatch(/nwsc_embedded_target_count/);
        expect(source).toMatch(/nwsc_embedded_target_name/);
        expect(source).toMatch(/nwsc_embedded_target_data/);
        expect(source).toMatch(/nwsc_embedded_target_size/);
      },
    );
  });

  it("throws when an nwscript.nss file is empty", async () => {
    const root = await mkdtemp(join(tmpdir(), "nwsc-games-"));
    await mkdir(join(root, "games", "k1"), { recursive: true });
    await writeFile(join(root, "games", "k1", "nwscript.nss"), "");
    await withEnv(
      { NWSC_GAME_TARGETS_DIR: undefined, NWSC_GAME_TARGETS: undefined },
      async () => {
        await expect(
          generateGameTargets({ projectRoot: root, outDir: join(root, "generated") }),
        ).rejects.toThrow(/empty/);
      },
    );
  });

  it("throws on an invalid target directory name", async () => {
    const root = await mkdtemp(join(tmpdir(), "nwsc-games-"));
    await mkdir(join(root, "games", "bad name"), { recursive: true });
    await withEnv(
      { NWSC_GAME_TARGETS_DIR: undefined, NWSC_GAME_TARGETS: undefined },
      async () => {
        await expect(
          generateGameTargets({ projectRoot: root, outDir: join(root, "generated") }),
        ).rejects.toThrow(/Invalid game target directory/);
      },
    );
  });

  it("throws when NWSC_GAME_TARGETS names a missing target", async () => {
    const root = await mkdtemp(join(tmpdir(), "nwsc-games-"));
    await mkdir(join(root, "games"), { recursive: true });
    await withEnv(
      { NWSC_GAME_TARGETS_DIR: undefined, NWSC_GAME_TARGETS: "missing" },
      async () => {
        await expect(
          generateGameTargets({ projectRoot: root, outDir: join(root, "generated") }),
        ).rejects.toThrow(/not found/);
      },
    );
  });

  it("selects only the requested target", async () => {
    const root = await mkdtemp(join(tmpdir(), "nwsc-games-"));
    await mkdir(join(root, "games", "k1"), { recursive: true });
    await mkdir(join(root, "games", "k2"), { recursive: true });
    await writeFile(join(root, "games", "k1", "nwscript.nss"), "k1 spec\n");
    await writeFile(join(root, "games", "k2", "nwscript.nss"), "k2 spec\n");

    await withEnv(
      { NWSC_GAME_TARGETS_DIR: undefined, NWSC_GAME_TARGETS: "k1" },
      async () => {
        const result = await generateGameTargets({
          projectRoot: root,
          outDir: join(root, "generated"),
        });
        const source = await readFile(result.sourceFile, "utf8");
        expect(result.targets.map((target) => target.name)).toEqual(["k1"]);
        expect(source).toMatch(/"k1"/);
        expect(source).not.toMatch(/"k2"/);
      },
    );
  });
});
