// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Definitions and functions for invoking and using Far main functions that
// support multiple and extensible arc types.

#include <fst/extensions/far/util.h>

#include <string>
#include <vector>

#include <fstream>

namespace fst {

// Return the 'FarType' value corresponding to a far type name.
FarType FarTypeFromString(const string &str) {
  FarType type = FAR_DEFAULT;
  if (str == "fst")
    type = FAR_FST;
  else if (str == "stlist")
    type = FAR_STLIST;
  else if (str == "sttable")
    type = FAR_STTABLE;
  else if (str == "default")
    type = FAR_DEFAULT;
  return type;
}

// Return the textual name  corresponding to a 'FarType;.
string FarTypeToString(FarType type) {
  switch (type) {
    case FAR_FST:
      return "fst";
    case FAR_STLIST:
      return "stlist";
    case FAR_STTABLE:
      return "sttable";
    case FAR_DEFAULT:
      return "default";
    default:
      return "<unknown>";
  }
}

FarEntryType StringToFarEntryType(const string &s) {
  if (s == "line") {
    return FET_LINE;
  } else if (s == "file") {
    return FET_FILE;
  } else {
    FSTERROR() << "Unknown FAR entry type: " << s;
    return FET_LINE;  // compiler requires return
  }
}

FarTokenType StringToFarTokenType(const string &s) {
  if (s == "symbol") {
    return FTT_SYMBOL;
  } else if (s == "byte") {
    return FTT_BYTE;
  } else if (s == "utf8") {
    return FTT_UTF8;
  } else {
    FSTERROR() << "Unknown FAR entry type: " << s;
    return FTT_SYMBOL;  // compiler requires return
  }
}

void ExpandArgs(int argc, char **argv, int *argcp, char ***argvp) {
}

}  // namespace fst
