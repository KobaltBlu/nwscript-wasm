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
thread_local std::string gInspection;
thread_local std::string gDisassemblyError;
thread_local size_t gErrorFileOffset = static_cast<size_t>(-1);

struct Instruction {
  size_t offset = 0;      // code offset (header excluded)
  size_t fileOffset = 0;  // physical file offset
  size_t size = 0;
  uint8_t op = 0;
  uint8_t aux = 0;
  std::vector<uint8_t> extra;
};

struct NcsHeader {
  bool present = false;
  size_t size = 0;
  std::string version;
  uint32_t fileSize = 0;
};

struct InstructionPart {
  const char* kind = "unknown";
  size_t fileOffset = 0;
  size_t length = 0;
  std::string text;
  bool hasNumber = false;
  bool numberIsInteger = true;
  double numberValue = 0;
  bool hasString = false;
  std::string stringValue;
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

std::string jsonEscape(const char* data, size_t size) {
  std::string out;
  out.push_back('"');
  for (size_t i = 0; i < size; ++i) {
    const unsigned char ch = static_cast<unsigned char>(data[i]);
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20 || ch >= 0x7f) {
          out += "\\u";
          out += hexValue(ch, 4);
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string jsonEscape(const std::string& value) {
  return jsonEscape(value.data(), value.size());
}

std::string jsonNumber(double value, bool integer) {
  if (integer) {
    std::ostringstream out;
    out << static_cast<int64_t>(value);
    return out.str();
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.9g", value);
  return buffer;
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

bool parseInstructions(
    const uint8_t* data,
    size_t size,
    std::vector<Instruction>& out,
    NcsHeader& header,
    bool allowPartial) {
  header = {};
  gErrorFileOffset = static_cast<size_t>(-1);
  if (data == nullptr || size == 0) {
    gDisassemblyError = "NCS input is empty";
    gErrorFileOffset = 0;
    return false;
  }

  size_t pos = 0;
  if (size >= 8 && std::memcmp(data, "NCS V1.0", 8) == 0) {
    if (size < 13) {
      gDisassemblyError = "Truncated NCS header";
      gErrorFileOffset = 0;
      return false;
    }
    if (data[8] != 'B') {
      gDisassemblyError = "Invalid NCS header byte at offset 8";
      gErrorFileOffset = 8;
      return false;
    }
    header.present = true;
    header.size = 13;
    header.version = "V1.0";
    header.fileSize = readU32(data + 9);
    pos = 13;
  }

  auto fail = [&](const std::string& message, size_t fileOffset) {
    gDisassemblyError = message;
    gErrorFileOffset = fileOffset;
    return allowPartial && !out.empty();
  };

  while (pos < size) {
    if (size - pos < 2) {
      return fail("Truncated NCS instruction at offset 0x" + hexValue(pos, 8), pos);
    }

    Instruction ins;
    ins.fileOffset = pos;
    ins.offset = pos - header.size;
    ins.op = data[pos++];
    ins.aux = data[pos++];

    if (opName(ins.op) == nullptr) {
      return fail(
          "Unknown NCS opcode 0x" + hexValue(ins.op, 2) +
              " at offset 0x" + hexValue(ins.offset, 8),
          ins.fileOffset);
    }

    size_t extraSize = fixedExtraSize(ins.op, ins.aux);
    if (extraSize == static_cast<size_t>(-1)) {
      if (size - pos < 2) {
        return fail("Truncated NCS string length at offset 0x" + hexValue(ins.offset, 8), ins.fileOffset);
      }
      extraSize = 2 + readU16(data + pos);
    }

    if (extraSize > size - pos) {
      return fail("Truncated NCS operand at offset 0x" + hexValue(ins.offset, 8), ins.fileOffset);
    }

    ins.extra.assign(data + pos, data + pos + extraSize);
    pos += extraSize;
    ins.size = pos - ins.fileOffset;
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

std::string formatJumpOperand(
    const Instruction& ins,
    const std::unordered_map<size_t, std::string>& labels,
    int32_t* relativeOut = nullptr,
    int64_t* targetOut = nullptr) {
  const int32_t rel = readI32(ins.extra.data());
  const int64_t target = static_cast<int64_t>(ins.offset) + rel;
  if (relativeOut) *relativeOut = rel;
  if (targetOut) *targetOut = target;
  auto it = labels.find(static_cast<size_t>(target));
  if (target >= 0 && it != labels.end()) {
    return it->second;
  }
  std::ostringstream out;
  out << (rel < 0 ? "-" : "+") << "0x"
      << hexValue(static_cast<uint32_t>(rel < 0 ? -rel : rel), 8);
  return out.str();
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
    case 0x25:
      out << formatJumpOperand(ins, labels);
      break;

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

std::unordered_map<size_t, std::string> buildLabels(const std::vector<Instruction>& instructions) {
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
  return labels;
}

InstructionPart makePart(
    const char* kind,
    size_t fileOffset,
    size_t length,
    const std::string& text = {}) {
  InstructionPart part;
  part.kind = kind;
  part.fileOffset = fileOffset;
  part.length = length;
  part.text = text;
  return part;
}

InstructionPart makeIntPart(
    const char* kind,
    size_t fileOffset,
    size_t length,
    const std::string& text,
    int64_t value) {
  InstructionPart part = makePart(kind, fileOffset, length, text);
  part.hasNumber = true;
  part.numberIsInteger = true;
  part.numberValue = static_cast<double>(value);
  return part;
}

InstructionPart makeFloatPart(
    size_t fileOffset,
    size_t length,
    const std::string& text,
    double value) {
  InstructionPart part = makePart("float", fileOffset, length, text);
  part.hasNumber = true;
  part.numberIsInteger = false;
  part.numberValue = value;
  return part;
}

InstructionPart makeStringPart(
    const char* kind,
    size_t fileOffset,
    size_t length,
    const std::string& text,
    const std::string& value) {
  InstructionPart part = makePart(kind, fileOffset, length, text);
  part.hasString = true;
  part.stringValue = value;
  return part;
}

void appendUnknownTail(
    std::vector<InstructionPart>& parts,
    const Instruction& ins,
    size_t extraConsumed) {
  if (extraConsumed >= ins.extra.size()) {
    return;
  }
  const size_t remaining = ins.extra.size() - extraConsumed;
  parts.push_back(makePart(
      "unknown",
      ins.fileOffset + 2 + extraConsumed,
      remaining,
      hexValue(ins.extra[extraConsumed], 2)));
}

std::vector<InstructionPart> describeParts(
    const Instruction& ins,
    const std::unordered_map<size_t, std::string>& labels,
    const std::vector<std::string>& actionNames) {
  std::vector<InstructionPart> parts;
  parts.push_back(makeIntPart(
      "opcode",
      ins.fileOffset,
      1,
      hexValue(ins.op, 2),
      ins.op));
  parts.push_back(makeIntPart(
      "aux",
      ins.fileOffset + 1,
      1,
      hexValue(ins.aux, 2),
      ins.aux));

  const auto* x = ins.extra.data();
  const size_t extraBase = ins.fileOffset + 2;
  size_t consumed = 0;

  switch (ins.op) {
    case 0x04:
      switch (ins.aux) {
        case 0x03: {
          const int32_t value = readI32(x);
          parts.push_back(makeIntPart(
              "integer",
              extraBase,
              4,
              hexValue(static_cast<uint32_t>(value), 8),
              value));
          consumed = 4;
          break;
        }
        case 0x04: {
          const float value = readF32(x);
          std::ostringstream text;
          text << std::fixed << std::setprecision(6) << value;
          parts.push_back(makeFloatPart(extraBase, 4, text.str(), value));
          consumed = 4;
          break;
        }
        case 0x06: {
          const uint32_t value = readU32(x);
          parts.push_back(makeIntPart(
              "object",
              extraBase,
              4,
              hexValue(value, 8),
              value));
          consumed = 4;
          break;
        }
        case 0x05:
        case 0x17: {
          const uint16_t len = readU16(x);
          parts.push_back(makeIntPart(
              "stringLength",
              extraBase,
              2,
              hexValue(len, 4),
              len));
          const size_t dataLen = ins.extra.size() >= 2 ? ins.extra.size() - 2 : 0;
          parts.push_back(makeStringPart(
              "stringData",
              extraBase + 2,
              dataLen,
              escapeString(x + 2, len),
              std::string(reinterpret_cast<const char*>(x + 2), len)));
          consumed = ins.extra.size();
          break;
        }
        case 0x12: {
          const uint32_t value = readU32(x);
          parts.push_back(makeIntPart(
              "integer",
              extraBase,
              4,
              hexValue(value, 8),
              value));
          consumed = 4;
          break;
        }
        default:
          break;
      }
      break;

    case 0x05: {
      const uint16_t id = readU16(x);
      const uint8_t argc = x[2];
      std::string actionText;
      if (id < actionNames.size() && !actionNames[id].empty()) {
        actionText = actionNames[id] + "(" + hexValue(id, 4) + ")";
      } else {
        actionText = hexValue(id, 4);
      }
      parts.push_back(makeIntPart("actionId", extraBase, 2, actionText, id));
      parts.push_back(makeIntPart(
          "argumentCount",
          extraBase + 2,
          1,
          hexValue(argc, 2),
          argc));
      consumed = 3;
      break;
    }

    case 0x1D:
    case 0x1E:
    case 0x1F:
    case 0x25: {
      int32_t rel = 0;
      int64_t target = 0;
      const std::string text = formatJumpOperand(ins, labels, &rel, &target);
      parts.push_back(makeIntPart("address", extraBase, 4, text, rel));
      consumed = 4;
      break;
    }

    case 0x1B:
    case 0x23:
    case 0x24:
    case 0x28:
    case 0x29: {
      const int32_t value = readI32(x);
      parts.push_back(makeIntPart(
          "stackOffset",
          extraBase,
          4,
          hexValue(static_cast<uint32_t>(value), 8),
          value));
      consumed = 4;
      break;
    }

    case 0x2C: {
      const uint32_t first = readU32(x);
      const uint32_t second = readU32(x + 4);
      parts.push_back(makeIntPart("integer", extraBase, 4, hexValue(first, 8), first));
      parts.push_back(makeIntPart("integer", extraBase + 4, 4, hexValue(second, 8), second));
      consumed = 8;
      break;
    }

    case 0x03:
    case 0x27:
    case 0x01:
    case 0x26: {
      const int32_t stack = readI32(x);
      const uint16_t copySize = readU16(x + 4);
      parts.push_back(makeIntPart(
          "stackOffset",
          extraBase,
          4,
          hexValue(static_cast<uint32_t>(stack), 8),
          stack));
      parts.push_back(makeIntPart(
          "size",
          extraBase + 4,
          2,
          hexValue(copySize, 4),
          copySize));
      consumed = 6;
      break;
    }

    case 0x21: {
      const uint16_t a = readU16(x);
      const uint16_t b = readU16(x + 2);
      const uint16_t c = readU16(x + 4);
      parts.push_back(makeIntPart("size", extraBase, 2, hexValue(a, 4), a));
      parts.push_back(makeIntPart("size", extraBase + 2, 2, hexValue(b, 4), b));
      parts.push_back(makeIntPart("size", extraBase + 4, 2, hexValue(c, 4), c));
      consumed = 6;
      break;
    }

    case 0x0B:
    case 0x0C:
      if (ins.extra.size() == 2) {
        const uint16_t value = readU16(x);
        parts.push_back(makeIntPart("size", extraBase, 2, hexValue(value, 4), value));
        consumed = 2;
      }
      break;

    default:
      break;
  }

  appendUnknownTail(parts, ins, consumed);
  return parts;
}

void appendJsonPart(std::ostringstream& out, const InstructionPart& part) {
  out << "{\"kind\":" << jsonEscape(part.kind)
      << ",\"fileOffset\":" << part.fileOffset
      << ",\"length\":" << part.length;
  if (!part.text.empty()) {
    out << ",\"text\":" << jsonEscape(part.text);
  }
  if (part.hasNumber) {
    out << ",\"value\":" << jsonNumber(part.numberValue, part.numberIsInteger);
  } else if (part.hasString) {
    out << ",\"value\":" << jsonEscape(part.stringValue);
  }
  out << '}';
}

std::vector<InstructionPart> describeHeaderParts(const NcsHeader& header) {
  std::vector<InstructionPart> parts;
  if (!header.present) {
    return parts;
  }
  parts.push_back(makeStringPart("unknown", 0, 8, "NCS V1.0", "NCS V1.0"));
  parts.push_back(makeStringPart("unknown", 8, 1, "B", "B"));
  parts.push_back(makeIntPart(
      "size",
      9,
      4,
      hexValue(header.fileSize, 8),
      header.fileSize));
  return parts;
}

std::string buildInspectionJson(
    const NcsHeader& header,
    const std::vector<Instruction>& instructions,
    const std::unordered_map<size_t, std::string>& labels,
    const std::vector<std::string>& actionNames) {
  std::ostringstream out;
  out << "{\"header\":{\"present\":" << (header.present ? "true" : "false")
      << ",\"size\":" << header.size;
  if (header.present) {
    out << ",\"version\":" << jsonEscape(header.version)
        << ",\"fileSize\":" << header.fileSize;
  }
  out << ",\"parts\":[";
  const auto headerParts = describeHeaderParts(header);
  for (size_t i = 0; i < headerParts.size(); ++i) {
    if (i > 0) out << ',';
    appendJsonPart(out, headerParts[i]);
  }
  out << "]},\"instructions\":[";

  for (size_t i = 0; i < instructions.size(); ++i) {
    const Instruction& ins = instructions[i];
    if (i > 0) out << ',';

    const std::string mnemonic = canonicalName(ins);
    const std::string operand = formatOperand(ins, labels, actionNames);
    const std::string raw = rawBytes(ins);
    const auto parts = describeParts(ins, labels, actionNames);

    out << "{\"index\":" << i
        << ",\"codeOffset\":" << ins.offset
        << ",\"fileOffset\":" << ins.fileOffset
        << ",\"size\":" << ins.size
        << ",\"opcode\":" << static_cast<unsigned>(ins.op)
        << ",\"aux\":" << static_cast<unsigned>(ins.aux)
        << ",\"mnemonic\":" << jsonEscape(mnemonic)
        << ",\"operandText\":" << jsonEscape(operand)
        << ",\"rawText\":" << jsonEscape(raw)
        << ",\"parts\":[";
    for (size_t p = 0; p < parts.size(); ++p) {
      if (p > 0) out << ',';
      appendJsonPart(out, parts[p]);
    }
    out << ']';

    if (isJump(ins.op) && ins.extra.size() == 4) {
      const int32_t rel = readI32(ins.extra.data());
      const int64_t target = static_cast<int64_t>(ins.offset) + rel;
      if (target >= 0) {
        out << ",\"jumpTarget\":" << target;
      }
    }

    if (ins.op == 0x05 && ins.extra.size() >= 3) {
      const uint16_t id = readU16(ins.extra.data());
      out << ",\"actionId\":" << id;
      if (id < actionNames.size() && !actionNames[id].empty()) {
        out << ",\"actionName\":" << jsonEscape(actionNames[id]);
      }
    }

    out << '}';
  }

  out << ']';
  if (!gDisassemblyError.empty()) {
    out << ",\"error\":{\"message\":" << jsonEscape(gDisassemblyError);
    if (gErrorFileOffset != static_cast<size_t>(-1)) {
      out << ",\"fileOffset\":" << gErrorFileOffset;
    }
    out << '}';
  }
  out << '}';
  return out.str();
}

bool decodeNcs(
    const uint8_t* data,
    size_t size,
    NcsHeader& header,
    std::vector<Instruction>& instructions,
    std::unordered_map<size_t, std::string>& labels,
    bool allowPartial) {
  if (!parseInstructions(data, size, instructions, header, allowPartial)) {
    return false;
  }
  labels = buildLabels(instructions);
  return true;
}

} // namespace

extern "C" {

NWSC_DISASM_EXPORT int32_t nwsc_disassemble(
    const uint8_t* data,
    size_t size,
    const char* actionNames) {
  gDisassembly.clear();
  gDisassemblyError.clear();

  NcsHeader header;
  std::vector<Instruction> instructions;
  std::unordered_map<size_t, std::string> labels;
  if (!decodeNcs(data, size, header, instructions, labels, false)) {
    return 1;
  }

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

NWSC_DISASM_EXPORT int32_t nwsc_inspect_ncs(
    const uint8_t* data,
    size_t size,
    const char* actionNames) {
  gInspection.clear();
  gDisassemblyError.clear();

  NcsHeader header;
  std::vector<Instruction> instructions;
  std::unordered_map<size_t, std::string> labels;
  if (!decodeNcs(data, size, header, instructions, labels, true)) {
    return 1;
  }

  gInspection = buildInspectionJson(header, instructions, labels, parseActionNames(actionNames));
  return 0;
}

NWSC_DISASM_EXPORT const char* nwsc_inspection_data() {
  return gInspection.c_str();
}

NWSC_DISASM_EXPORT size_t nwsc_inspection_size() {
  return gInspection.size();
}

}
