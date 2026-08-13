import { spawn } from "node:child_process";
import { mkdtemp, mkdir, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { cliPath, fixturesDir, readFixture } from "./helpers/compiler.mjs";

function runCli(args, { cwd } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [cliPath, ...args], {
      cwd,
      windowsHide: true,
    });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk) => {
      stdout += chunk;
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk;
    });
    child.on("error", reject);
    child.on("close", (code) => {
      resolve({ code, stdout, stderr });
    });
  });
}

describe("compile-nss CLI", () => {
  /** @type {string[]} */
  const tempDirs = [];

  afterEach(async () => {
    // Leave temp dirs for OS cleanup; no recursive rm required for assertions.
    tempDirs.length = 0;
  });

  async function makeTemp() {
    const dir = await mkdtemp(join(tmpdir(), "nwsc-cli-"));
    tempDirs.push(dir);
    return dir;
  }

  it("prints help with --disassemble and without --package", async () => {
    const result = await runCli(["--help"]);
    expect(result.code).toBe(0);
    expect(result.stdout).toMatch(/--disassemble/);
    expect(result.stdout).not.toMatch(/--package/);
  });

  it("lists embedded game targets", async () => {
    const result = await runCli(["--list-games"]);
    expect(result.code).toBe(0);
  });

  it("compiles --source with --nwscript to an .ncs file", async () => {
    const dir = await makeTemp();
    const sourcePath = join(dir, "hello.nss");
    const outPath = join(dir, "hello.ncs");
    await writeFile(sourcePath, await readFixture("scripts", "hello.nss"));

    const result = await runCli([
      "--source",
      sourcePath,
      "--nwscript",
      join(fixturesDir, "nwscript.nss"),
      "--out",
      outPath,
    ]);
    expect(result.code).toBe(0);
    const ncs = await readFile(outPath);
    expect(ncs.byteLength).toBeGreaterThan(13);
    expect(ncs.subarray(0, 8).toString()).toBe("NCS V1.0");
  });

  it("writes .ndb beside the ncs when --debug is set", async () => {
    const dir = await makeTemp();
    const sourcePath = join(dir, "hello.nss");
    const outPath = join(dir, "hello.ncs");
    await writeFile(sourcePath, await readFixture("scripts", "hello.nss"));

    const result = await runCli([
      "--source",
      sourcePath,
      "--nwscript",
      join(fixturesDir, "nwscript.nss"),
      "--out",
      outPath,
      "--debug",
    ]);
    expect(result.code).toBe(0);
    const ndb = await readFile(join(dir, "hello.ndb"));
    expect(ndb.byteLength).toBeGreaterThan(0);
  });

  it("compiles with --include", async () => {
    const dir = await makeTemp();
    const srcDir = join(dir, "src");
    const incDir = join(dir, "inc");
    await mkdir(srcDir);
    await mkdir(incDir);
    const sourcePath = join(srcDir, "needs_include.nss");
    const outPath = join(dir, "needs_include.ncs");
    await writeFile(sourcePath, await readFixture("scripts", "needs_include.nss"));
    await writeFile(join(incDir, "inc_helper.nss"), await readFixture("scripts", "inc_helper.nss"));

    const result = await runCli([
      "--source",
      sourcePath,
      "--nwscript",
      join(fixturesDir, "nwscript.nss"),
      "--include",
      incDir,
      "--out",
      outPath,
    ]);
    expect(result.code).toBe(0);
    const ncs = await readFile(outPath);
    expect(ncs.byteLength).toBeGreaterThan(13);
  });

  it("disassembles an NCS file to assembly containing ACTION", async () => {
    const dir = await makeTemp();
    const sourcePath = join(dir, "hello.nss");
    const ncsPath = join(dir, "hello.ncs");
    const asmPath = join(dir, "hello.asm");
    await writeFile(sourcePath, await readFixture("scripts", "hello.nss"));

    const compiled = await runCli([
      "--source",
      sourcePath,
      "--nwscript",
      join(fixturesDir, "nwscript.nss"),
      "--out",
      ncsPath,
    ]);
    expect(compiled.code).toBe(0);

    const result = await runCli([
      "--disassemble",
      ncsPath,
      "--nwscript",
      join(fixturesDir, "nwscript.nss"),
      "--out",
      asmPath,
    ]);
    expect(result.code).toBe(0);
    const asm = await readFile(asmPath, "utf8");
    expect(asm).toMatch(/ACTION/);
  });

  it("rejects --source and --disassemble together", async () => {
    const result = await runCli([
      "--source",
      "a.nss",
      "--disassemble",
      "a.ncs",
      "--nwscript",
      join(fixturesDir, "nwscript.nss"),
    ]);
    expect(result.code).not.toBe(0);
    expect(result.stderr).toMatch(/cannot be used together/);
  });

  it("rejects a compile without --nwscript or --game", async () => {
    const dir = await makeTemp();
    const sourcePath = join(dir, "hello.nss");
    await writeFile(sourcePath, await readFixture("scripts", "hello.nss"));
    const result = await runCli(["--source", sourcePath]);
    expect(result.code).not.toBe(0);
    expect(result.stderr).toMatch(/--nwscript|--game/);
  });
});
