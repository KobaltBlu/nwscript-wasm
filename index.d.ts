export declare const OptimizationFlags: Readonly<{
  None: 0;
  RemoveDeadCode: 1;
  MeldInstructions: 2;
  RemoveDeadBranches: 4;
  O0: 0;
  O1: 1;
  O2: 5;
  O3: 7;
}>;

export interface NWScriptCompilerCreateOptions {
  /** Explicit language specification. When supplied, this overrides gameTarget. */
  languageSpec?: string | Uint8Array;
  /** Name of an nwscript.nss target embedded at build time, e.g. "k1" or "k2". */
  gameTarget?: string;
  languageSpecName?: string;
  sourceResType?: number;
  binaryResType?: number;
  debugResType?: number;
  writeDebug?: boolean;
  maxIncludeDepth?: number;
  optimizationFlags?: number;
  moduleOptions?: Record<string, unknown>;
}

export interface NWScriptCompileResult {
  ok: boolean;
  code: number;
  error: string;
  bytecode: Uint8Array;
  debugCode: Uint8Array;
}

export type NcsInstructionPartKind =
  | "opcode"
  | "aux"
  | "integer"
  | "float"
  | "stringLength"
  | "stringData"
  | "object"
  | "actionId"
  | "argumentCount"
  | "address"
  | "stackOffset"
  | "size"
  | "unknown";

export interface NcsInstructionPart {
  kind: NcsInstructionPartKind;
  fileOffset: number;
  length: number;
  text?: string;
  value?: string | number;
}

export interface NcsInstruction {
  index: number;
  codeOffset: number;
  fileOffset: number;
  size: number;
  opcode: number;
  aux: number;
  mnemonic: string;
  operandText: string;
  rawText: string;
  parts: NcsInstructionPart[];
  jumpTarget?: number;
  actionId?: number;
  actionName?: string;
}

export interface NcsHeader {
  present: boolean;
  size: number;
  version?: string;
  fileSize?: number;
  parts: NcsInstructionPart[];
}

export interface NcsInspection {
  header: NcsHeader;
  instructions: NcsInstruction[];
}

export interface NcsInspectOptions {
  actionNames?: string[];
  moduleOptions?: Record<string, unknown>;
}

export declare class NWScriptCompiler {
  /** List game targets whose nwscript.nss files were embedded into this WASM build. */
  static getEmbeddedGameTargets(moduleOptions?: Record<string, unknown>): Promise<string[]>;

  static create(options?: NWScriptCompilerCreateOptions): Promise<NWScriptCompiler>;

  /**
   * Inspect NCS bytecode without a language specification. ACTION IDs stay numeric
   * unless `actionNames` is supplied.
   */
  static inspectNcs(
    ncs: string | Uint8Array,
    options?: NcsInspectOptions,
  ): Promise<NcsInspection>;

  addSource(name: string, source: string | Uint8Array, resType?: number): this;
  addSources(sources: Record<string, string | Uint8Array>, resType?: number): this;
  removeSource(name: string, resType?: number): boolean;
  clearSources(): void;
  setOptimizationFlags(flags: number): void;
  setDebugOutput(enabled: boolean): void;
  compile(name: string, source?: string | Uint8Array): NWScriptCompileResult;
  disassemble(ncs: string | Uint8Array): string;
  inspectNcs(ncs: string | Uint8Array): NcsInspection;
  dispose(): void;
}

export default NWScriptCompiler;
