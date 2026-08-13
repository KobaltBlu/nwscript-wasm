import { afterEach, describe, expect, it } from "vitest";
import { NWScriptCompiler } from "../dist/index.mjs";
import { createCompiler, readFixture } from "./helpers/compiler.mjs";

function makeNcs(instructionBytes) {
  const body = Uint8Array.from(instructionBytes);
  const fileSize = 13 + body.byteLength;
  const out = new Uint8Array(fileSize);
  out.set(new TextEncoder().encode("NCS V1.0"), 0);
  out[8] = 0x42;
  out[9] = (fileSize >>> 24) & 0xff;
  out[10] = (fileSize >>> 16) & 0xff;
  out[11] = (fileSize >>> 8) & 0xff;
  out[12] = fileSize & 0xff;
  out.set(body, 13);
  return out;
}

describe("disassemble and inspect", () => {
  /** @type {import("../dist/index.mjs").default | undefined} */
  let compiler;

  afterEach(() => {
    compiler?.dispose();
    compiler = undefined;
  });

  it("disassembles compiled bytecode with ACTION PrintString", async () => {
    compiler = await createCompiler();
    const compiled = compiler.compile("hello", await readFixture("scripts", "hello.nss"));
    expect(compiled.ok).toBe(true);

    const asm = compiler.disassemble(compiled.bytecode);
    expect(asm).toMatch(/ACTION/);
    expect(asm).toMatch(/PrintString/);
  });

  it("inspects compiled bytecode including ACTION metadata", async () => {
    compiler = await createCompiler();
    const compiled = compiler.compile("hello", await readFixture("scripts", "hello.nss"));
    expect(compiled.ok).toBe(true);

    const inspection = compiler.inspectNcs(compiled.bytecode);
    expect(inspection.header.present).toBe(true);
    expect(inspection.header.size).toBe(13);
    expect(inspection.instructions.length).toBeGreaterThan(0);

    for (const instruction of inspection.instructions) {
      expect(typeof instruction.opcode).toBe("number");
      expect(typeof instruction.mnemonic).toBe("string");
      expect(Array.isArray(instruction.parts)).toBe(true);
      expect(typeof instruction.fileOffset).toBe("number");
    }

    const action = inspection.instructions.find((instruction) => instruction.opcode === 0x05);
    expect(action).toBeTruthy();
    expect(action.actionId).toBe(0);
    expect(action.actionName).toBe("PrintString");
  });

  it("reports jumpTarget on branch instructions", async () => {
    compiler = await createCompiler();
    const compiled = compiler.compile(
      "branch",
      `
void main() {
  if (TRUE) {
    PrintString("yes");
  } else {
    PrintString("no");
  }
}
`,
    );
    expect(compiled.ok).toBe(true);

    const inspection = compiler.inspectNcs(compiled.bytecode);
    const jump = inspection.instructions.find((instruction) => instruction.jumpTarget != null);
    expect(jump).toBeTruthy();
    expect(jump.jumpTarget).toBeGreaterThanOrEqual(0);
  });

  it("inspects bytecode without a compiler instance", async () => {
    compiler = await createCompiler();
    const compiled = compiler.compile("hello", await readFixture("scripts", "hello.nss"));
    expect(compiled.ok).toBe(true);
    compiler.dispose();
    compiler = undefined;

    const inspection = await NWScriptCompiler.inspectNcs(compiled.bytecode);
    expect(inspection.header.present).toBe(true);
    expect(inspection.instructions.length).toBeGreaterThan(0);
  });

  it("resolves ACTION id 0 from static actionNames", async () => {
    const ncs = makeNcs([0x05, 0x00, 0x00, 0x00, 0x01, 0x20, 0x00]);
    const inspection = await NWScriptCompiler.inspectNcs(ncs, {
      actionNames: ["PrintString"],
    });
    const action = inspection.instructions.find((instruction) => instruction.opcode === 0x05);
    expect(action.actionId).toBe(0);
    expect(action.actionName).toBe("PrintString");
  });

  it("throws on empty, truncated, and unknown-opcode NCS", async () => {
    await expect(NWScriptCompiler.inspectNcs(new Uint8Array(0))).rejects.toThrow(
      /NCS input is empty/,
    );

    const truncated = new Uint8Array(9);
    truncated.set(new TextEncoder().encode("NCS V1.0"), 0);
    truncated[8] = 0x42;
    await expect(NWScriptCompiler.inspectNcs(truncated)).rejects.toThrow(/Truncated NCS header/);

    const unknown = makeNcs([0xff, 0x00]);
    await expect(NWScriptCompiler.inspectNcs(unknown)).rejects.toThrow(/Unknown NCS opcode/);
  });

  it("inspects a hand-built RETN NCS header and instruction", async () => {
    const ncs = makeNcs([0x20, 0x00]);
    const inspection = await NWScriptCompiler.inspectNcs(ncs);
    expect(inspection.header.present).toBe(true);
    expect(inspection.header.size).toBe(13);
    expect(inspection.header.version).toBe("V1.0");
    expect(inspection.header.parts.length).toBeGreaterThan(0);
    expect(inspection.instructions).toHaveLength(1);
    expect(inspection.instructions[0].opcode).toBe(0x20);
    expect(inspection.instructions[0].mnemonic).toBe("RETN");
  });
});
