// SPDX-License-Identifier: MIT
//
// NDB V1.0 inspector. The text layout matches the native compiler's
// debugger output and neverwinter/nwscript/ndb.nim.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define NWSC_NDB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define NWSC_NDB_EXPORT
#endif

namespace {

thread_local std::string gNdbInspection;
thread_local std::string gNdbError;

std::string jsonEscape(const std::string& value) {
  std::string out = "\"";
  for (unsigned char ch : value) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20 || ch >= 0x7f) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04X", ch);
          out += buf;
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string ndbTypeName(const std::string& raw) {
  if (raw.empty()) return "unknown";
  switch (raw[0]) {
    case 'f': return "float";
    case 'i': return "int";
    case 'v': return "void";
    case 'o': return "object";
    case 's': return "string";
    case 'e': return "effect";
    case 't': return "struct";
    default: return "unknown";
  }
}

uint32_t parseHex(const std::string& text) {
  return static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
}

uint32_t parseDec(const std::string& text) {
  return static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 10));
}

uint32_t toCodeOffset(uint32_t fileOffset) {
  return fileOffset >= 13 ? fileOffset - 13 : fileOffset;
}

std::vector<std::string> splitWs(const std::string& line) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : line) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

struct NdbStructField {
  std::string type;
  std::string label;
};

struct NdbStruct {
  std::string label;
  std::vector<NdbStructField> fields;
};

struct NdbFunction {
  std::string label;
  uint32_t fileStart = 0;
  uint32_t fileEnd = 0;
  std::string returnType;
  std::vector<std::string> args;
};

struct NdbVariable {
  std::string label;
  std::string type;
  uint32_t fileStart = 0;
  uint32_t fileEnd = 0;
  uint32_t stackLoc = 0;
};

struct NdbLine {
  int fileIndex = 0;
  int line = 0;
  uint32_t fileStart = 0;
  uint32_t fileEnd = 0;
};

} // namespace

extern "C" {

NWSC_NDB_EXPORT int32_t nwsc_inspect_ndb(const uint8_t* data, size_t size) {
  gNdbInspection.clear();
  gNdbError.clear();
  if (data == nullptr || size == 0) {
    gNdbError = "NDB input is empty";
    return 1;
  }

  std::string text(reinterpret_cast<const char*>(data), size);
  std::vector<std::string> lines;
  std::string current;
  for (char ch : text) {
    if (ch == '\n') {
      if (!current.empty() && current.back() == '\r') current.pop_back();
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    if (current.back() == '\r') current.pop_back();
    lines.push_back(current);
  }

  if (lines.empty() || lines[0] != "NDB V1.0") {
    gNdbError = "Invalid NDB header; expected NDB V1.0";
    return 1;
  }

  std::vector<std::string> files;
  std::vector<NdbStruct> structs;
  std::vector<NdbFunction> functions;
  std::vector<NdbVariable> variables;
  std::vector<NdbLine> sourceLines;

  for (size_t i = 1; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (line.empty() || line[0] == '#') continue;
    const auto parts = splitWs(line);
    if (parts.empty()) continue;

    if (i == 1 && parts.size() == 5 && std::isdigit(static_cast<unsigned char>(parts[0][0]))) {
      continue;
    }

    if (parts[0][0] == 'N' || parts[0][0] == 'n') {
      if (parts.size() < 2) {
        gNdbError = "Truncated NDB file entry";
        return 1;
      }
      files.push_back(parts[1]);
    } else if (parts[0] == "s") {
      if (parts.size() < 3) {
        gNdbError = "Truncated NDB struct entry";
        return 1;
      }
      NdbStruct item;
      item.label = parts[2];
      structs.push_back(std::move(item));
    } else if (parts[0] == "sf") {
      if (parts.size() < 3 || structs.empty()) {
        gNdbError = "Invalid NDB struct field";
        return 1;
      }
      structs.back().fields.push_back({parts[1], parts[2]});
    } else if (parts[0] == "f") {
      if (parts.size() < 6) {
        gNdbError = "Truncated NDB function entry";
        return 1;
      }
      NdbFunction fn;
      fn.fileStart = parseHex(parts[1]);
      fn.fileEnd = parseHex(parts[2]);
      fn.returnType = parts[4];
      fn.label = parts[5];
      functions.push_back(std::move(fn));
    } else if (parts[0] == "fp") {
      if (parts.size() < 2 || functions.empty()) {
        gNdbError = "Invalid NDB function parameter";
        return 1;
      }
      functions.back().args.push_back(parts[1]);
    } else if (parts[0] == "v") {
      if (parts.size() < 6) {
        gNdbError = "Truncated NDB variable entry";
        return 1;
      }
      NdbVariable var;
      var.fileStart = parseHex(parts[1]);
      var.fileEnd = parseHex(parts[2]);
      var.stackLoc = parseHex(parts[3]);
      var.type = parts[4];
      var.label = parts[5];
      variables.push_back(std::move(var));
    } else if (parts[0][0] == 'l') {
      if (parts.size() < 4) {
        gNdbError = "Truncated NDB line entry";
        return 1;
      }
      NdbLine src;
      src.fileIndex = static_cast<int>(parseDec(parts[0].substr(1)));
      src.line = static_cast<int>(parseDec(parts[1]));
      src.fileStart = parseHex(parts[2]);
      src.fileEnd = parseHex(parts[3]);
      sourceLines.push_back(src);
    }
  }

  std::ostringstream out;
  out << "{\"version\":\"V1.0\",\"files\":[";
  for (size_t i = 0; i < files.size(); ++i) {
    if (i) out << ',';
    out << jsonEscape(files[i]);
  }
  out << "],\"structs\":[";
  for (size_t i = 0; i < structs.size(); ++i) {
    if (i) out << ',';
    out << "{\"label\":" << jsonEscape(structs[i].label) << ",\"fields\":[";
    for (size_t f = 0; f < structs[i].fields.size(); ++f) {
      if (f) out << ',';
      out << "{\"type\":" << jsonEscape(ndbTypeName(structs[i].fields[f].type))
          << ",\"label\":" << jsonEscape(structs[i].fields[f].label) << '}';
    }
    out << "]}";
  }
  out << "],\"functions\":[";
  for (size_t i = 0; i < functions.size(); ++i) {
    if (i) out << ',';
    const auto& fn = functions[i];
    out << "{\"label\":" << jsonEscape(fn.label)
        << ",\"fileOffsetStart\":" << fn.fileStart
        << ",\"fileOffsetEnd\":" << fn.fileEnd
        << ",\"codeOffsetStart\":" << toCodeOffset(fn.fileStart)
        << ",\"codeOffsetEnd\":" << toCodeOffset(fn.fileEnd)
        << ",\"returnType\":" << jsonEscape(ndbTypeName(fn.returnType))
        << ",\"args\":[";
    for (size_t a = 0; a < fn.args.size(); ++a) {
      if (a) out << ',';
      out << jsonEscape(ndbTypeName(fn.args[a]));
    }
    out << "]}";
  }
  out << "],\"variables\":[";
  for (size_t i = 0; i < variables.size(); ++i) {
    if (i) out << ',';
    const auto& var = variables[i];
    out << "{\"label\":" << jsonEscape(var.label)
        << ",\"type\":" << jsonEscape(ndbTypeName(var.type))
        << ",\"fileOffsetStart\":" << var.fileStart
        << ",\"fileOffsetEnd\":" << var.fileEnd
        << ",\"codeOffsetStart\":" << toCodeOffset(var.fileStart)
        << ",\"codeOffsetEnd\":" << toCodeOffset(var.fileEnd)
        << ",\"stackLoc\":" << var.stackLoc << '}';
  }
  out << "],\"lines\":[";
  for (size_t i = 0; i < sourceLines.size(); ++i) {
    if (i) out << ',';
    const auto& line = sourceLines[i];
    out << "{\"fileIndex\":" << line.fileIndex
        << ",\"file\":" << jsonEscape(line.fileIndex >= 0 && static_cast<size_t>(line.fileIndex) < files.size() ? files[static_cast<size_t>(line.fileIndex)] : "")
        << ",\"line\":" << line.line
        << ",\"fileOffsetStart\":" << line.fileStart
        << ",\"fileOffsetEnd\":" << line.fileEnd
        << ",\"codeOffsetStart\":" << toCodeOffset(line.fileStart)
        << ",\"codeOffsetEnd\":" << toCodeOffset(line.fileEnd) << '}';
  }
  out << "]}";
  gNdbInspection = out.str();
  return 0;
}

NWSC_NDB_EXPORT const char* nwsc_ndb_inspection_data() {
  return gNdbInspection.c_str();
}

NWSC_NDB_EXPORT size_t nwsc_ndb_inspection_size() {
  return gNdbInspection.size();
}

NWSC_NDB_EXPORT const char* nwsc_ndb_error_data() {
  return gNdbError.c_str();
}

NWSC_NDB_EXPORT size_t nwsc_ndb_error_size() {
  return gNdbError.size();
}

}
