import { describe, expect, it } from "vitest";
import { OptimizationFlags } from "../dist/index.mjs";

describe("OptimizationFlags", () => {
  it("is frozen", () => {
    expect(Object.isFrozen(OptimizationFlags)).toBe(true);
  });

  it("matches the documented bit values", () => {
    expect(OptimizationFlags.None).toBe(0);
    expect(OptimizationFlags.O0).toBe(0);
    expect(OptimizationFlags.RemoveDeadCode).toBe(1);
    expect(OptimizationFlags.O1).toBe(1);
    expect(OptimizationFlags.MeldInstructions).toBe(2);
    expect(OptimizationFlags.RemoveDeadBranches).toBe(4);
    expect(OptimizationFlags.O2).toBe(5);
    expect(OptimizationFlags.O3).toBe(7);
  });
});
