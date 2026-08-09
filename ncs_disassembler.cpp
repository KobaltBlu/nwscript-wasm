// SPDX-License-Identifier: MIT
//
// Lightweight NCS disassembler for the WebAssembly package. The instruction
// decoding rules mirror neverwinter/nwscript/nwasm.nim without linking the Nim
// runtime into the compiler WASM module.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define NWSC_DISASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define NWSC_DISASM_EXPORT
#endif

namespace {

thread_local std::string gDisassembly;
thread_local std::string gDisassemblyError;

struct Instruction {
  size_t offset = 0;
  uint8_t op = 0;
  uint8_t aux = 0;
  std::vector<uint8_t> extra;
};

uint16_t readU16(const uint8_t* p) {
  return (static_cast<uint16_t>(p[0]) << 8) |
         static_cast<uint16_t>(p[1]);
}

int16_t readI16(const uint8_t* p) {
  return static_cast<int16_t>(readU16(p));
}

uint32_t readU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

int32_t readI32(const uint8_t* p) {
  return static_cast<int32_t>(readU32(p));
}

float readF32(const uint8_t* p) {
  const uint32_t bits = readU32(p);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string hexValue(uint64_t value, size_t width) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setfill('0') << std::setw(static_cast<int>(width)) << value;
  return out.str();
}

std::string escapeString(const uint8_t* data, size_t size) {
  std::ostringstream out;
  out << '"';
  for (size_t i = 0; i < size; ++i) {
    const unsigned char ch = data[i];
    switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (std::isprint(ch)) {
          out << static_cast<char>(ch);
        } else {
          out << "\\x" << hexValue(ch, 2);
        }
        break;
    }
  }
  out << '"';
  return out.str();
}

const char* opName(uint8_t op) {
  switch (op) {
    case 0x01: return "CPDOWNSP";
    case 0x02: return "RSADD";
    case 0x03: return "CPTOPSP";
    case 0x04: return "CONST";
    case 0x05: return "ACTION";
    case 0x06: return "LOGAND";
    case 0x07: return "LOGOR";
    case 0x08: return "INCOR";
    case 0x09: return "EXCOR";
    case 0x0A: return "BOOLAND";
    case 0x0B: return "EQUAL";
    case 0x0C: return "NEQUAL";
    case 0x0D: return "GEQ";
    case 0x0E: return "GT";
    case 0x0F: return "LT";
    case 0x10: return "LEQ";
    case 0x11: return "SHLEFT";
    case 0x12: return "SHRIGHT";
    case 0x13: return "USHRIGHT";
    case 0x14: return "ADD";
    case 0x15: return "SUB";
    case 0x16: return "MUL";
    case 0x17: return "DIV";
    case 0x18: return "MOD";
    case 0x19: return "NEG";
    case 0x1A: return "COMP";
    case 0x1B: return "MOVSP";
    case 0x1C: return "STOREIP";
    case 0x1D: return "JMP";
    case 0x1E: return "JSR";
    case 0x1F: return "JZ";
    case 0x20: return "RETN";
    case 0x21: return "DESTRUCT";
    case 0x22: return "NOT";
    case 0x23: return "DECSP";
    case 0x24: return "INCSP";
    case 0x25: return "JNZ";
    case 0x26: return "CPDOWNBP";
    case 0x27: return "CPTOPBP";
    case 0x28: return "DECBP";
    case 0x29: return "INCBP";
    case 0x2A: return "SAVEBP";
    case 0x2B: return "RESTOREBP";
    case 0x2C: return "STORE_STATE";
    case 0x2D: return "NOP";
    default: return nullptr;
  }
}

const char* auxSuffix(uint8_t aux) {
  switch (aux) {
    case 0x03: return "I";
    case 0x04: return "F";
    case 0x05: return "S";
    case 0x06: return "O";
    case 0x10: return "E0";
    case 0x11: return "E1";
    case 0x12: return "E2";
    case 0x13: return "E3";
    case 0x14: return "E4";
    case 0x15: return "E5";
    case 0x16: return "E6";
    case 0x17: return "E7";
    case 0x18: return "E8";
    case 0x19: return "E9";
    case 0x20: return "II";
    case 0x21: return "FF";
    case 0x22: return "OO";
    case 0x23: return "SS";
    case 0x24: return "TT";
    case 0x25: return "IF";
    case 0x26: return "FI";
    case 0x30: return "E0E0";
    case 0x31: return "E1E1";
    case 0x32: return "E2E2";
    case 0x33: return "E3E3";
    case 0x34: return "E4E4";
    case 0x35: return "E5E5";
    case 0x36: return "E6E6";
    case 0x37: return "E7E7";
    case 0x38: return "E8E8";
    case 0x39: return "E9E9";
    case 0x3A: return "VV";
    case 0x3B: return "VF";
    case 0x3C: return "FV";
    default: return nullptr;
  }
}

size_t fixedExtraSize(uint8_t op, uint8_t aux) {
  switch (op) {
    case 0x04:
      switch (aux) {
        case 0x03:
        case 0x04:
        case 0x06:
        case 0x12: return 4;
        case 0x05:
        case 0x17: return static_cast<size_t>(-1); // uint16 length + bytes
        default: return 0;
      }
    case 0x1D:
    case 0x1E:
    case 0x1F:
    case 0x25:
    case 0x1B:
    case 0x23:
    case 0x24:
    case 0x28:
    case 0x29: return 4;
    case 0x2C: return 8;
    case 0x05: return 3;
    case 0x03:
    case 0x27:
    case 0x01:
    case 0x26:
    case 0x21: return 6;
    case 0x0B:
    case 0x0C: return aux == 0x24 ? 2 : 0;
    default: return 0;
  }
}

bool parseInstructions(const uint8_t* data, size_t size, std::vector<Instruction>& out) {
  if (data == nullptr || size == 0) {
    gDisassemblyError = "NCS input is empty";
    return false;
  }

  size_t pos = 0;
  if (size >= 13 && std::memcmp(data, "NCS V1.0", 8) == 0) {
    if (data[8] != 'B') {
      gDisassemblyError = "Invalid NCS header byte at offset 8";
      return false;
    }
    pos = 13;
  }

  while (pos < size) {
    if (size - pos < 2) {
      gDisassemblyError = "Truncated NCS instruction at offset 0x" + hexValue(pos, 8);
      return false;
    }

    Instruction ins;
    ins.offset = pos - (pos >= 13 && std::memcmp(data, "NCS V1.0", 8) == 0 ? 13 : 0);
    ins.op = data[pos++];
    ins.aux = data[pos++];

    if (opName(ins.op) == nullptr) {
      gDisassemblyError = "Unknown NCS opcode 0x" + hexValue(ins.op, 2) +
                          " at offset 0x" + hexValue(ins.offset, 8);
      return false;
    }

    size_t extraSize = fixedExtraSize(ins.op, ins.aux);
    if (extraSize == static_cast<size_t>(-1)) {
      if (size - pos < 2) {
        gDisassemblyError = "Truncated NCS string length at offset 0x" + hexValue(ins.offset, 8);
        return false;
      }
      extraSize = 2 + readU16(data + pos);
    }

    if (extraSize > size - pos) {
      gDisassemblyError = "Truncated NCS operand at offset 0x" + hexValue(ins.offset, 8);
      return false;
    }

    ins.extra.assign(data + pos, data + pos + extraSize);
    pos += extraSize;
    out.push_back(std::move(ins));
  }

  return true;
}

std::vector<std::string> parseActionNames(const char* names) {
  std::vector<std::string> result;
  if (names == nullptr || *names == '\0') {
    return result;
  }

  const char* start = names;
  const char* p = names;
  while (true) {
    if (*p == '\n' || *p == '\0') {
      result.emplace_back(start, p - start);
      if (*p == '\0') break;
      start = p + 1;
    }
    ++p;
  }
  return result;
}

std::string canonicalName(const Instruction& ins) {
  std::string name = opName(ins.op);
  if (ins.op == 0x20) {
    return name;
  }
  const char* suffix = auxSuffix(ins.aux);
  if (suffix != nullptr) {
    name += suffix;
  }
  return name;
}

bool isJump(uint8_t op) {
  return op == 0x1D || op == 0x1E || op == 0x1F || op == 0x25;
}

std::string formatOperand(
    const Instruction& ins,
    const std::unordered_map<size_t, std::string>& labels,
    const std::vector<std::string>& actionNames) {
  const auto* x = ins.extra.data();
  const size_t n = ins.extra.size();
  std::ostringstream out;

  switch (ins.op) {
    case 0x04:
      switch (ins.aux) {
        case 0x03: out << hexValue(static_cast<uint32_t>(readI32(x)), 8); break;
        case 0x04: out << std::fixed << std::setprecision(6) << readF32(x); break;
        case 0x06: out << hexValue(readU32(x), 8); break;
        case 0x05:
        case 0x17: {
          const uint16_t len = readU16(x);
          out << hexValue(len, 4) << " str " << escapeString(x + 2, len);
          break;
        }
        case 0x12: out << hexValue(readU32(x), 8); break;
        default: break;
      }
      break;

    case 0x05: {
      const uint16_t id = readU16(x);
      const uint8_t argc = x[2];
      if (id < actionNames.size() && !actionNames[id].empty()) {
        out << actionNames[id] << '(' << hexValue(id, 4) << "), " << hexValue(argc, 2);
      } else {
        out << hexValue(id, 4) << ", " << hexValue(argc, 2);
      }
      break;
    }

    case 0x1D:
    case 0x1E:
    case 0x1F:
    case 0x25: {
      const int32_t rel = readI32(x);
      const size_t target = static_cast<size_t>(static_cast<int64_t>(ins.offset) + rel);
      auto it = labels.find(target);
      if (it != labels.end()) out << it->second;
      else out << (rel < 0 ? "-" : "+") << "0x" << hexValue(static_cast<uint32_t>(rel < 0 ? -rel : rel), 8);
      break;
    }

    case 0x1B:
    case 0x23:
    case 0x24:
    case 0x28:
    case 0x29:
      out << hexValue(static_cast<uint32_t>(readI32(x)), 8);
      break;

    case 0x2C:
      out << hexValue(readU32(x), 8) << ", " << hexValue(readU32(x + 4), 8);
      break;

    case 0x03:
    case 0x27:
      out << hexValue(static_cast<uint32_t>(readI32(x)), 8) << ", " << hexValue(readU16(x + 4), 4);
      break;

    case 0x01:
    case 0x26:
      out << hexValue(static_cast<uint32_t>(readI32(x)), 8) << ", " << hexValue(readU16(x + 4), 4);
      break;

    case 0x21:
      out << hexValue(readU16(x), 4) << ", " << hexValue(readU16(x + 2), 4)
          << ", " << hexValue(readU16(x + 4), 4);
      break;

    case 0x0B:
    case 0x0C:
      if (n == 2) out << hexValue(readU16(x), 4);
      break;

    default:
      break;
  }

  return out.str();
}

std::string rawBytes(const Instruction& ins) {
  std::ostringstream out;
  out << hexValue(ins.op, 2) << ' ' << hexValue(ins.aux, 2);
  for (uint8_t b : ins.extra) out << ' ' << hexValue(b, 2);
  return out.str();
}

} // namespace

extern "C" {

NWSC_DISASM_EXPORT int32_t nwsc_disassemble(
    const uint8_t* data,
    size_t size,
    const char* actionNames) {
  gDisassembly.clear();
  gDisassemblyError.clear();

  std::vector<Instruction> instructions;
  if (!parseInstructions(data, size, instructions)) {
    return 1;
  }

  std::unordered_set<size_t> jsrTargets;
  std::unordered_set<size_t> branchTargets;
  for (const Instruction& ins : instructions) {
    if (!isJump(ins.op) || ins.extra.size() != 4) continue;
    const int32_t rel = readI32(ins.extra.data());
    const int64_t target = static_cast<int64_t>(ins.offset) + rel;
    if (target < 0) continue;
    if (ins.op == 0x1E) jsrTargets.insert(static_cast<size_t>(target));
    else branchTargets.insert(static_cast<size_t>(target));
  }

  std::unordered_map<size_t, std::string> labels;
  for (size_t target : branchTargets) labels[target] = "off_" + hexValue(target, 8);
  for (size_t target : jsrTargets) labels[target] = "fn_" + hexValue(target, 8);

  const auto actions = parseActionNames(actionNames);
  std::ostringstream out;
  for (const Instruction& ins : instructions) {
    const auto label = labels.find(ins.offset);
    if (label != labels.end()) out << label->second << ":\n";

    const std::string raw = rawBytes(ins);
    const std::string operand = formatOperand(ins, labels, actions);

    out << hexValue(ins.offset, 8) << "  "
        << std::left << std::setw(30) << raw << std::right << "  "
        << canonicalName(ins);
    if (!operand.empty()) out << ' ' << operand;
    out << '\n';
  }

  gDisassembly = out.str();
  return 0;
}

NWSC_DISASM_EXPORT const char* nwsc_disassembly_data() {
  return gDisassembly.c_str();
}

NWSC_DISASM_EXPORT size_t nwsc_disassembly_size() {
  return gDisassembly.size();
}

NWSC_DISASM_EXPORT const char* nwsc_disassembly_error_data() {
  return gDisassemblyError.c_str();
}

NWSC_DISASM_EXPORT size_t nwsc_disassembly_error_size() {
  return gDisassemblyError.size();
}

}
