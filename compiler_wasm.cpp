// SPDX-License-Identifier: MIT
//
// WebAssembly-facing, in-memory adapter for the NWScript compiler.
// The linked compiler remains subject to the license of neverwinter.nim.

#include "native/scriptcomp.h"
#include "native/scripterrors.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define NWSC_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define NWSC_EXPORT
#endif

namespace {

constexpr int32_t NWSC_ERR_INVALID_HANDLE = -1;
constexpr int32_t NWSC_ERR_NOT_INITIALIZED = -2;
constexpr int32_t NWSC_ERR_INVALID_ARGUMENT = -3;
constexpr int32_t NWSC_ERR_REENTRANT_CALL = -4;

struct CompilerContext {
  RESTYPE sourceType;
  RESTYPE binaryType;
  RESTYPE debugType;

  std::unique_ptr<CScriptCompiler> compiler;
  std::unordered_map<std::string, std::vector<uint8_t>> sources;

  std::vector<uint8_t> bytecode;
  std::vector<uint8_t> debugcode;
  std::string error;
  std::string lastMissingResource;
  int32_t code = 0;
  bool initialized = false;
};

CompilerContext* gActiveContext = nullptr;

class ActiveContextGuard {
public:
  explicit ActiveContextGuard(CompilerContext* context)
      : previous_(gActiveContext), valid_(previous_ == nullptr || previous_ == context) {
    if (valid_) {
      gActiveContext = context;
    }
  }

  ~ActiveContextGuard() {
    if (valid_) {
      gActiveContext = previous_;
    }
  }

  bool valid() const { return valid_; }

private:
  CompilerContext* previous_;
  bool valid_;
};

CompilerContext* fromHandle(uintptr_t handle) {
  return reinterpret_cast<CompilerContext*>(handle);
}

std::string normalizeName(const char* rawName) {
  if (rawName == nullptr) {
    return {};
  }

  std::string name(rawName);
  std::replace(name.begin(), name.end(), '\\', '/');

  const auto slash = name.find_last_of('/');
  if (slash != std::string::npos) {
    name.erase(0, slash + 1);
  }

  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  // Resource-manager requests use resrefs, while browser callers commonly have
  // filenames. Accept both "foo" and "foo.nss" by normalizing to the resref.
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    name.erase(dot);
  }

  return name;
}

std::string sourceKey(const char* name, RESTYPE type) {
  return normalizeName(name) + "#" + std::to_string(static_cast<uint32_t>(type));
}

std::string resourceDisplayName(const CompilerContext* context, const char* rawName, RESTYPE type) {
  if (rawName == nullptr || *rawName == '\0') {
    return {};
  }

  std::string name(rawName);
  std::replace(name.begin(), name.end(), '\\', '/');

  const auto slash = name.find_last_of('/');
  if (slash != std::string::npos) {
    name.erase(0, slash + 1);
  }

  // Source requests made by the NWScript compiler are NSS resources. The
  // native compiler usually requests them by resref, so restore the extension
  // in diagnostics to make the missing file immediately actionable.
  if (context != nullptr && type == context->sourceType && name.find_last_of('.') == std::string::npos) {
    name += ".nss";
  }

  return name;
}

void setLocalError(CompilerContext* context, int32_t code, const char* message) {
  if (context == nullptr) {
    return;
  }
  context->code = code;
  context->error = message != nullptr ? message : "";
}

int32_t toPublicCompilerCode(int32_t nativeCode) {
  // The native compiler uses negative TLK strrefs. The Nim wrapper exposes them
  // as positive error codes, so preserve that public behavior for WASM callers.
  return nativeCode < 0 ? -nativeCode : nativeCode;
}

BOOL updateResourceDirectory(const char*) {
  // Browser output is captured in memory. There is no resource directory to update.
  return FALSE;
}

int32_t writeResource(const char*, RESTYPE type, const uint8_t* data, size_t size, bool) {
  CompilerContext* context = gActiveContext;
  if (context == nullptr || data == nullptr) {
    return STRREF_CSCRIPTCOMPILER_ERROR_FATAL_COMPILER_ERROR;
  }

  std::vector<uint8_t>* target = nullptr;
  if (type == context->binaryType) {
    target = &context->bytecode;
  } else if (type == context->debugType) {
    target = &context->debugcode;
  } else {
    return STRREF_CSCRIPTCOMPILER_ERROR_FATAL_COMPILER_ERROR;
  }

  target->assign(data, data + size);
  return 0;
}

bool loadResource(const char* name, RESTYPE type) {
  CompilerContext* context = gActiveContext;
  if (context == nullptr || context->compiler == nullptr) {
    return false;
  }

  const auto it = context->sources.find(sourceKey(name, type));
  if (it == context->sources.end()) {
    context->lastMissingResource = resourceDisplayName(context, name, type);
    return false;
  }

  const auto& bytes = it->second;
  if (bytes.empty()) {
    context->lastMissingResource = resourceDisplayName(context, name, type);
    return false;
  }

  context->compiler->DeliverRequestedFile(
      reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

CScriptCompilerAPI makeCompilerApi() {
  CScriptCompilerAPI api;
  api.ResManUpdateResourceDirectory = updateResourceDirectory;
  api.ResManWriteToFile = writeResource;
  api.ResManLoadScriptSourceFile = loadResource;
  return api;
}

void captureCompilerError(CompilerContext* context, int32_t nativeCode) {
  if (context == nullptr || context->compiler == nullptr) {
    return;
  }

  if (nativeCode == 1 || nativeCode == -1) {
    nativeCode = static_cast<int32_t>(context->compiler->GetCapturedErrorStrRef());
    if (nativeCode == 0) {
      nativeCode = STRREF_CSCRIPTCOMPILER_ERROR_FATAL_COMPILER_ERROR;
    }
  }

  context->code = toPublicCompilerCode(nativeCode);
  if (context->code != 0) {
    const CExoString* captured = context->compiler->GetCapturedError();
    const std::string nativeError = captured != nullptr ? captured->CStr() : "";

    if (!context->lastMissingResource.empty()) {
      context->error = "Missing resource: " + context->lastMissingResource;
      if (!nativeError.empty()) {
        context->error += "\n" + nativeError;
      }
    } else {
      context->error = nativeError;
    }
  } else {
    context->error.clear();
  }
}

} // namespace

extern "C" {

NWSC_EXPORT int32_t nwsc_abi_version() {
  return 1;
}

NWSC_EXPORT uintptr_t nwsc_create(uint16_t sourceType, uint16_t binaryType, uint16_t debugType) {
  auto* context = new CompilerContext{
      static_cast<RESTYPE>(sourceType),
      static_cast<RESTYPE>(binaryType),
      static_cast<RESTYPE>(debugType)};
  return reinterpret_cast<uintptr_t>(context);
}

NWSC_EXPORT void nwsc_destroy(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  if (context == nullptr) {
    return;
  }

  if (gActiveContext == context) {
    gActiveContext = nullptr;
  }
  delete context;
}

NWSC_EXPORT int32_t nwsc_add_source(
    uintptr_t handle,
    const char* name,
    uint16_t type,
    const uint8_t* data,
    size_t size) {
  CompilerContext* context = fromHandle(handle);
  if (context == nullptr) {
    return NWSC_ERR_INVALID_HANDLE;
  }
  if (name == nullptr || *name == '\0' || data == nullptr || size == 0) {
    setLocalError(context, NWSC_ERR_INVALID_ARGUMENT, "Invalid source registration");
    return NWSC_ERR_INVALID_ARGUMENT;
  }

  const std::string key = sourceKey(name, static_cast<RESTYPE>(type));
  if (key.empty()) {
    setLocalError(context, NWSC_ERR_INVALID_ARGUMENT, "Invalid source name");
    return NWSC_ERR_INVALID_ARGUMENT;
  }

  context->sources[key] = std::vector<uint8_t>(data, data + size);
  return 0;
}

NWSC_EXPORT int32_t nwsc_remove_source(uintptr_t handle, const char* name, uint16_t type) {
  CompilerContext* context = fromHandle(handle);
  if (context == nullptr) {
    return NWSC_ERR_INVALID_HANDLE;
  }
  if (name == nullptr || *name == '\0') {
    return NWSC_ERR_INVALID_ARGUMENT;
  }

  return context->sources.erase(sourceKey(name, static_cast<RESTYPE>(type))) > 0 ? 1 : 0;
}

NWSC_EXPORT void nwsc_clear_sources(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  if (context != nullptr) {
    context->sources.clear();
  }
}

NWSC_EXPORT int32_t nwsc_init(
    uintptr_t handle,
    const char* languageSpecName,
    int32_t writeDebug,
    uint32_t maxIncludeDepth,
    uint32_t optimizationFlags) {
  CompilerContext* context = fromHandle(handle);
  if (context == nullptr) {
    return NWSC_ERR_INVALID_HANDLE;
  }
  if (languageSpecName == nullptr || *languageSpecName == '\0') {
    setLocalError(context, NWSC_ERR_INVALID_ARGUMENT, "Missing language specification name");
    return NWSC_ERR_INVALID_ARGUMENT;
  }

  ActiveContextGuard guard(context);
  if (!guard.valid()) {
    setLocalError(context, NWSC_ERR_REENTRANT_CALL, "Compiler call is already active");
    return NWSC_ERR_REENTRANT_CALL;
  }

  context->bytecode.clear();
  context->debugcode.clear();
  context->error.clear();
  context->lastMissingResource.clear();
  context->code = 0;
  context->initialized = false;

  context->compiler = std::make_unique<CScriptCompiler>(
      context->sourceType, context->binaryType, context->debugType, makeCompilerApi());

  context->compiler->SetGenerateDebuggerOutput(writeDebug != 0 ? TRUE : FALSE);
  context->compiler->SetOptimizationFlags(optimizationFlags);
  context->compiler->SetCompileConditionalOrMain(TRUE);
  context->compiler->SetOutputAlias("scriptout");
  context->compiler->SetMaxIncludeDepth(maxIncludeDepth);
  context->compiler->SetGraphvizOutputPath("");

  // This immediately loads and parses the language specification through the
  // resource callback, so callers must add it before nwsc_init().
  context->compiler->SetIdentifierSpecification(normalizeName(languageSpecName).c_str());

  const int32_t capturedCode = static_cast<int32_t>(context->compiler->GetCapturedErrorStrRef());
  if (capturedCode != 0) {
    captureCompilerError(context, capturedCode);
    return context->code;
  }

  context->initialized = true;
  return 0;
}

NWSC_EXPORT int32_t nwsc_compile(uintptr_t handle, const char* scriptName) {
  CompilerContext* context = fromHandle(handle);
  if (context == nullptr) {
    return NWSC_ERR_INVALID_HANDLE;
  }
  if (!context->initialized || context->compiler == nullptr) {
    setLocalError(context, NWSC_ERR_NOT_INITIALIZED, "Compiler has not been initialized");
    return NWSC_ERR_NOT_INITIALIZED;
  }
  if (scriptName == nullptr || *scriptName == '\0') {
    setLocalError(context, NWSC_ERR_INVALID_ARGUMENT, "Missing script name");
    return NWSC_ERR_INVALID_ARGUMENT;
  }

  ActiveContextGuard guard(context);
  if (!guard.valid()) {
    setLocalError(context, NWSC_ERR_REENTRANT_CALL, "Compiler call is already active");
    return NWSC_ERR_REENTRANT_CALL;
  }

  context->bytecode.clear();
  context->debugcode.clear();
  context->error.clear();
  context->lastMissingResource.clear();
  context->code = 0;

  const std::string normalizedName = normalizeName(scriptName);
  const int32_t nativeCode = context->compiler->CompileFile(normalizedName.c_str());
  captureCompilerError(context, nativeCode);
  return context->code;
}

NWSC_EXPORT void nwsc_set_optimization_flags(uintptr_t handle, uint32_t flags) {
  CompilerContext* context = fromHandle(handle);
  if (context != nullptr && context->compiler != nullptr) {
    context->compiler->SetOptimizationFlags(flags);
  }
}

NWSC_EXPORT void nwsc_set_debug_output(uintptr_t handle, int32_t enabled) {
  CompilerContext* context = fromHandle(handle);
  if (context != nullptr && context->compiler != nullptr) {
    context->compiler->SetGenerateDebuggerOutput(enabled != 0 ? TRUE : FALSE);
  }
}

NWSC_EXPORT int32_t nwsc_last_code(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  return context != nullptr ? context->code : NWSC_ERR_INVALID_HANDLE;
}

NWSC_EXPORT const uint8_t* nwsc_error_data(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  if (context == nullptr || context->error.empty()) {
    return nullptr;
  }
  return reinterpret_cast<const uint8_t*>(context->error.data());
}

NWSC_EXPORT size_t nwsc_error_size(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  return context != nullptr ? context->error.size() : 0;
}

NWSC_EXPORT const uint8_t* nwsc_bytecode_data(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  return context != nullptr && !context->bytecode.empty() ? context->bytecode.data() : nullptr;
}

NWSC_EXPORT size_t nwsc_bytecode_size(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  return context != nullptr ? context->bytecode.size() : 0;
}

NWSC_EXPORT const uint8_t* nwsc_debugcode_data(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  return context != nullptr && !context->debugcode.empty() ? context->debugcode.data() : nullptr;
}

NWSC_EXPORT size_t nwsc_debugcode_size(uintptr_t handle) {
  CompilerContext* context = fromHandle(handle);
  return context != nullptr ? context->debugcode.size() : 0;
}

} // extern "C"
