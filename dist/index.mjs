// SPDX-License-Identifier: MIT

import createNWScriptModule from "./nwscript-compiler.mjs";

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export const OptimizationFlags = Object.freeze({
  None: 0,
  RemoveDeadCode: 0x1,
  MeldInstructions: 0x2,
  RemoveDeadBranches: 0x4,
  O0: 0,
  O1: 0x1,
  O2: 0x1 | 0x4,
  O3: 0x1 | 0x4 | 0x2,
});

const EXPECTED_ABI_VERSION = 1;

function assertAbiVersion(module) {
  const version = module._nwsc_abi_version();
  if (version !== EXPECTED_ABI_VERSION) {
    throw new Error(
      `Unsupported NWScript WASM ABI version ${version}; expected ${EXPECTED_ABI_VERSION}`,
    );
  }
}


function readCString(module, ptr) {
  if (!ptr) {
    return "";
  }
  let end = ptr;
  while (module.HEAPU8[end] !== 0) {
    end += 1;
  }
  return decoder.decode(module.HEAPU8.subarray(ptr, end));
}

function getEmbeddedTargetNames(module) {
  const count = module._nwsc_embedded_target_count();
  const targets = [];
  for (let i = 0; i < count; i += 1) {
    const name = readCString(module, module._nwsc_embedded_target_name(i));
    if (name) {
      targets.push(name);
    }
  }
  return targets;
}

function getEmbeddedLanguageSpec(module, target, withCString) {
  return withCString(target, (targetPtr) => {
    const ptr = module._nwsc_embedded_target_data(targetPtr);
    const size = module._nwsc_embedded_target_size(targetPtr);
    if (!ptr || !size) {
      return null;
    }
    return module.HEAPU8.slice(ptr, ptr + size);
  });
}


function extractActionNames(languageSpec) {
  const text = decoder.decode(asBytes(languageSpec));
  const names = [];

  let statement = "";
  let quote = null;
  let escaped = false;
  let lineComment = false;
  let blockComment = false;

  const flush = () => {
    const value = statement.trim();
    statement = "";

    if (!value) {
      return;
    }

    const match = value.match(/\b([A-Za-z_][A-Za-z0-9_]*)\s*\(/);
    if (match) {
      names.push(match[1]);
    }
  };

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    const next = text[i + 1];

    if (lineComment) {
      if (ch === "\n") {
        lineComment = false;
        statement += ch;
      }
      continue;
    }

    if (blockComment) {
      if (ch === "*" && next === "/") {
        blockComment = false;
        i += 1;
      }
      continue;
    }

    if (quote) {
      statement += ch;

      if (escaped) {
        escaped = false;
      } else if (ch === "\\") {
        escaped = true;
      } else if (ch === quote) {
        quote = null;
      }
      continue;
    }

    if (ch === "/" && next === "/") {
      lineComment = true;
      i += 1;
      continue;
    }

    if (ch === "/" && next === "*") {
      blockComment = true;
      i += 1;
      continue;
    }

    if (ch === '"' || ch === "'") {
      quote = ch;
      statement += ch;
      continue;
    }

    if (ch === ";") {
      flush();
      continue;
    }

    statement += ch;
  }

  return names;
}

function asBytes(value) {
  if (value instanceof Uint8Array) {
    return value;
  }
  if (typeof value === "string") {
    return encoder.encode(value);
  }
  throw new TypeError("Expected a string or Uint8Array");
}

function readModuleBuffer(module, ptr, size) {
  if (!ptr || !size) {
    return new Uint8Array(0);
  }
  return module.HEAPU8.slice(ptr, ptr + size);
}

function inspectNcsWithModule(module, ncs, actionNames) {
  const bytes = asBytes(ncs);
  const names = Array.isArray(actionNames) ? actionNames.join("\n") : "";

  const dataPtr = module._malloc(Math.max(bytes.byteLength, 1));
  if (!dataPtr) {
    throw new Error("WASM allocation failed");
  }

  try {
    if (bytes.byteLength > 0) {
      module.HEAPU8.set(bytes, dataPtr);
    }

    const nameBytes = encoder.encode(names);
    const namesPtr = module._malloc(nameBytes.byteLength + 1);
    if (!namesPtr) {
      throw new Error("WASM allocation failed");
    }

    try {
      module.HEAPU8.set(nameBytes, namesPtr);
      module.HEAPU8[namesPtr + nameBytes.byteLength] = 0;

      const code = module._nwsc_inspect_ncs(dataPtr, bytes.byteLength, namesPtr);
      if (code !== 0) {
        const message = decoder.decode(
          readModuleBuffer(
            module,
            module._nwsc_disassembly_error_data(),
            module._nwsc_disassembly_error_size(),
          ),
        ).trim();
        throw new Error(message || `Failed to inspect NCS (${code})`);
      }

      const json = decoder.decode(
        readModuleBuffer(
          module,
          module._nwsc_inspection_data(),
          module._nwsc_inspection_size(),
        ),
      );
      return JSON.parse(json);
    } finally {
      module._free(namesPtr);
    }
  } finally {
    module._free(dataPtr);
  }
}

let standaloneInspectModulePromise;

function getStandaloneInspectModule(moduleOptions) {
  if (moduleOptions && Object.keys(moduleOptions).length > 0) {
    return createNWScriptModule(moduleOptions).then((module) => {
      assertAbiVersion(module);
      return module;
    });
  }

  if (!standaloneInspectModulePromise) {
    standaloneInspectModulePromise = createNWScriptModule({}).then((module) => {
      assertAbiVersion(module);
      return module;
    });
  }

  return standaloneInspectModulePromise;
}

export class NWScriptCompiler {
  static async getEmbeddedGameTargets(moduleOptions = {}) {
    const module = await createNWScriptModule(moduleOptions);
    assertAbiVersion(module);
    return getEmbeddedTargetNames(module);
  }

  static async create(options) {
    const {
      languageSpec,
      gameTarget,
      languageSpecName = "nwscript",
      sourceResType = 2009,
      binaryResType = 2010,
      debugResType = 2064,
      writeDebug = false,
      maxIncludeDepth = 16,
      optimizationFlags = OptimizationFlags.O1,
      moduleOptions = {},
    } = options ?? {};

    const module = await createNWScriptModule(moduleOptions);
    assertAbiVersion(module);
    const compiler = new NWScriptCompiler(module, {
      sourceResType,
      binaryResType,
      debugResType,
    });

    let resolvedLanguageSpec = languageSpec;
    if (resolvedLanguageSpec == null && gameTarget != null) {
      resolvedLanguageSpec = getEmbeddedLanguageSpec(
        module,
        String(gameTarget),
        compiler.#withCString.bind(compiler),
      );
      if (resolvedLanguageSpec == null) {
        const available = getEmbeddedTargetNames(module);
        compiler.dispose();
        throw new RangeError(
          `Unknown embedded game target ${JSON.stringify(String(gameTarget))}. ` +
          `Available targets: ${available.length > 0 ? available.join(", ") : "none"}`,
        );
      }
    }

    if (resolvedLanguageSpec == null) {
      const available = getEmbeddedTargetNames(module);
      compiler.dispose();
      throw new TypeError(
        "Either languageSpec or gameTarget is required. " +
        `Embedded targets: ${available.length > 0 ? available.join(", ") : "none"}`,
      );
    }

    compiler.#actionNames = extractActionNames(resolvedLanguageSpec);

    // Explicit languageSpec always wins over an embedded gameTarget.
    compiler.addSource(languageSpecName, resolvedLanguageSpec, sourceResType);
    const code = compiler.#withCString(languageSpecName, (namePtr) =>
      module._nwsc_init(
        compiler.#handle,
        namePtr,
        writeDebug ? 1 : 0,
        maxIncludeDepth,
        optimizationFlags,
      ),
    );

    if (code !== 0) {
      const message = compiler.#readError() || `Failed to initialize NWScript compiler (${code})`;
      compiler.dispose();
      throw new Error(message);
    }

    return compiler;
  }

  static async inspectNcs(ncs, options = {}) {
    const { actionNames = [], moduleOptions } = options ?? {};
    const module = await getStandaloneInspectModule(moduleOptions);
    return inspectNcsWithModule(module, ncs, actionNames);
  }

  #module;
  #handle;
  #sourceResType;
  #actionNames = [];
  #disposed = false;

  constructor(module, { sourceResType, binaryResType, debugResType }) {
    this.#module = module;
    this.#sourceResType = sourceResType;
    this.#handle = module._nwsc_create(sourceResType, binaryResType, debugResType);
    if (!this.#handle) {
      throw new Error("Failed to allocate NWScript compiler");
    }
  }

  addSource(name, source, resType = this.#sourceResType) {
    this.#assertAlive();
    const bytes = asBytes(source);

    const code = this.#withCString(name, (namePtr) =>
      this.#withBytes(bytes, (dataPtr) =>
        this.#module._nwsc_add_source(
          this.#handle,
          namePtr,
          resType,
          dataPtr,
          bytes.byteLength,
        ),
      ),
    );

    if (code !== 0) {
      throw new Error(this.#readError() || `Failed to register source ${name} (${code})`);
    }
    return this;
  }

  addSources(sources, resType = this.#sourceResType) {
    for (const [name, source] of Object.entries(sources)) {
      this.addSource(name, source, resType);
    }
    return this;
  }

  removeSource(name, resType = this.#sourceResType) {
    this.#assertAlive();
    const code = this.#withCString(name, (namePtr) =>
      this.#module._nwsc_remove_source(this.#handle, namePtr, resType),
    );
    if (code < 0) {
      throw new Error(this.#readError() || `Failed to remove source ${name} (${code})`);
    }
    return code === 1;
  }

  clearSources() {
    this.#assertAlive();
    this.#module._nwsc_clear_sources(this.#handle);
  }

  setOptimizationFlags(flags) {
    this.#assertAlive();
    this.#module._nwsc_set_optimization_flags(this.#handle, flags >>> 0);
  }

  setDebugOutput(enabled) {
    this.#assertAlive();
    this.#module._nwsc_set_debug_output(this.#handle, enabled ? 1 : 0);
  }

  compile(name, source = undefined) {
    this.#assertAlive();
    if (source !== undefined) {
      this.addSource(name, source);
    }

    const code = this.#withCString(name, (namePtr) =>
      this.#module._nwsc_compile(this.#handle, namePtr),
    );

    return {
      ok: code === 0,
      code,
      error: this.#readError(),
      bytecode: this.#readBuffer(
        this.#module._nwsc_bytecode_data(this.#handle),
        this.#module._nwsc_bytecode_size(this.#handle),
      ),
      debugCode: this.#readBuffer(
        this.#module._nwsc_debugcode_data(this.#handle),
        this.#module._nwsc_debugcode_size(this.#handle),
      ),
    };
  }


  disassemble(ncs) {
    this.#assertAlive();

    const bytes = asBytes(ncs);
    const actionNames = this.#actionNames.join("\n");

    const code = this.#withBytes(
      bytes,
      (dataPtr) =>
        this.#withCString(
          actionNames,
          (actionsPtr) =>
            this.#module._nwsc_disassemble(
              dataPtr,
              bytes.byteLength,
              actionsPtr,
            ),
        ),
    );

    if (code !== 0) {
      const message = decoder.decode(
        this.#readBuffer(
          this.#module._nwsc_disassembly_error_data(),
          this.#module._nwsc_disassembly_error_size(),
        ),
      ).trim();

      throw new Error(
        message || `Failed to disassemble NCS (${code})`,
      );
    }

    return decoder.decode(
      this.#readBuffer(
        this.#module._nwsc_disassembly_data(),
        this.#module._nwsc_disassembly_size(),
      ),
    );
  }

  inspectNcs(ncs) {
    this.#assertAlive();
    return inspectNcsWithModule(this.#module, ncs, this.#actionNames);
  }

  dispose() {
    if (!this.#disposed) {
      this.#module._nwsc_destroy(this.#handle);
      this.#handle = 0;
      this.#disposed = true;
    }
  }

  #assertAlive() {
    if (this.#disposed) {
      throw new Error("NWScriptCompiler has been disposed");
    }
  }

  #withCString(value, fn) {
    const bytes = encoder.encode(String(value));
    const ptr = this.#module._malloc(bytes.byteLength + 1);
    if (!ptr) {
      throw new Error("WASM allocation failed");
    }

    try {
      this.#module.HEAPU8.set(bytes, ptr);
      this.#module.HEAPU8[ptr + bytes.byteLength] = 0;
      return fn(ptr);
    } finally {
      this.#module._free(ptr);
    }
  }

  #withBytes(bytes, fn) {
    const ptr = this.#module._malloc(Math.max(bytes.byteLength, 1));
    if (!ptr) {
      throw new Error("WASM allocation failed");
    }

    try {
      if (bytes.byteLength > 0) {
        this.#module.HEAPU8.set(bytes, ptr);
      }
      return fn(ptr);
    } finally {
      this.#module._free(ptr);
    }
  }

  #readError() {
    return decoder.decode(
      this.#readBuffer(
        this.#module._nwsc_error_data(this.#handle),
        this.#module._nwsc_error_size(this.#handle),
      ),
    ).trim();
  }

  #readBuffer(ptr, size) {
    if (!ptr || !size) {
      return new Uint8Array(0);
    }
    return this.#module.HEAPU8.slice(ptr, ptr + size);
  }
}

export default NWScriptCompiler;
