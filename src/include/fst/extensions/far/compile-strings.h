// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.

#ifndef FST_EXTENSIONS_FAR_COMPILE_STRINGS_H_
#define FST_EXTENSIONS_FAR_COMPILE_STRINGS_H_

#include <libgen.h>
#include <istream>
#include <string>
#include <vector>

#include <fstream>
#include <fst/extensions/far/far.h>
#include <fst/string.h>

namespace fst {

// Construct a reader that provides FSTs from a file (stream) either on a
// line-by-line basis or on a per-stream basis.  Note that the freshly
// constructed reader is already set to the first input.
//
// Sample Usage:
//   for (StringReader<Arc> reader(...); !reader.Done(); reader.Next()) {
//     Fst *fst = reader.GetVectorFst();
//   }
template <class A>
class StringReader {
 public:
  typedef A Arc;
  typedef typename A::Label Label;
  typedef typename A::Weight Weight;
  typedef typename StringCompiler<A>::TokenType TokenType;

  enum EntryType { LINE = 1, FILE = 2 };

  StringReader(std::istream &istrm, const string &source, EntryType entry_type,
               TokenType token_type, bool allow_negative_labels,
               const SymbolTable *syms = nullptr,
               Label unknown_label = kNoStateId)
      : nline_(0),
        strm_(istrm),
        source_(source),
        entry_type_(entry_type),
        token_type_(token_type),
        symbols_(syms),
        done_(false),
        compiler_(token_type, syms, unknown_label, allow_negative_labels) {
    Next();  // Initialize the reader to the first input.
  }

  bool Done() { return done_; }

  void Next() {
    VLOG(1) << "Processing source " << source_ << " at line " << nline_;
    if (!strm_) {  // We're done if we have no more input.
      done_ = true;
      return;
    }
    if (entry_type_ == LINE) {
      getline(strm_, content_);
      ++nline_;
    } else {
      content_.clear();
      string line;
      while (getline(strm_, line)) {
        ++nline_;
        content_.append(line);
        content_.append("\n");
      }
    }
    if (!strm_ && content_.empty())  // We're also done if we read off all the
      done_ = true;                  // whitespace at the end of a file.
  }

  VectorFst<A> *GetVectorFst(bool keep_symbols = false) {
    VectorFst<A> *fst = new VectorFst<A>;
    if (keep_symbols) {
      fst->SetInputSymbols(symbols_);
      fst->SetOutputSymbols(symbols_);
    }
    if (compiler_(content_, fst)) {
      return fst;
    } else {
      delete fst;
      return nullptr;
    }
  }

  CompactFst<A, StringCompactor<A>> *GetCompactFst(bool keep_symbols = false) {
    CompactFst<A, StringCompactor<A>> *fst;
    if (keep_symbols) {
      VectorFst<A> tmp;
      tmp.SetInputSymbols(symbols_);
      tmp.SetOutputSymbols(symbols_);
      fst = new CompactFst<A, StringCompactor<A>>(tmp);
    } else {
      fst = new CompactFst<A, StringCompactor<A>>;
    }
    if (compiler_(content_, fst)) {
      return fst;
    } else {
      delete fst;
      return nullptr;
    }
  }

 private:
  size_t nline_;
  std::istream &strm_;
  string source_;
  EntryType entry_type_;
  TokenType token_type_;
  const SymbolTable *symbols_;
  bool done_;
  StringCompiler<A> compiler_;
  string content_;  // The actual content of the input stream's next FST.

  DISALLOW_COPY_AND_ASSIGN(StringReader);
};

// Compute the minimal length required to encode each line number as a decimal
// number.
int KeySize(const char *filename);

template <class Arc>
void FarCompileStrings(const std::vector<string> &in_fnames,
                       const string &out_fname, const string &fst_type,
                       const FarType &far_type, int32 generate_keys,
                       FarEntryType fet, FarTokenType tt,
                       const string &symbols_fname,
                       const string &unknown_symbol, bool keep_symbols,
                       bool initial_symbols, bool allow_negative_labels,
                       const string &key_prefix, const string &key_suffix) {
  typename StringReader<Arc>::EntryType entry_type;
  if (fet == FET_LINE) {
    entry_type = StringReader<Arc>::LINE;
  } else if (fet == FET_FILE) {
    entry_type = StringReader<Arc>::FILE;
  } else {
    FSTERROR() << "FarCompileStrings: Unknown entry type";
    return;
  }

  typename StringCompiler<Arc>::TokenType token_type;
  if (tt == FTT_SYMBOL) {
    token_type = StringCompiler<Arc>::SYMBOL;
  } else if (tt == FTT_BYTE) {
    token_type = StringCompiler<Arc>::BYTE;
  } else if (tt == FTT_UTF8) {
    token_type = StringCompiler<Arc>::UTF8;
  } else {
    FSTERROR() << "FarCompileStrings: Unknown token type";
    return;
  }

  bool compact;
  if (fst_type.empty() || (fst_type == "vector")) {
    compact = false;
  } else if (fst_type == "compact") {
    compact = true;
  } else {
    FSTERROR() << "FarCompileStrings: Unknown Fst type: " << fst_type;
    return;
  }

  const SymbolTable *syms = 0;
  typename Arc::Label unknown_label = kNoLabel;
  if (!symbols_fname.empty()) {
    SymbolTableTextOptions opts;
    opts.allow_negative = allow_negative_labels;
    syms = SymbolTable::ReadText(symbols_fname, opts);
    if (!syms) {
      LOG(ERROR) << "FarCompileStrings: Error reading symbol table: "
                 << symbols_fname;
      return;
    }
    if (!unknown_symbol.empty()) {
      unknown_label = syms->Find(unknown_symbol);
      if (unknown_label == kNoLabel) {
        FSTERROR() << "FarCompileStrings: Label \"" << unknown_label
                   << "\" missing from symbol table: " << symbols_fname;
        return;
      }
    }
  }

  FarWriter<Arc> *far_writer = FarWriter<Arc>::Create(out_fname, far_type);
  if (!far_writer) return;

  for (int i = 0, n = 0; i < in_fnames.size(); ++i) {
    if (generate_keys == 0 && in_fnames[i].empty()) {
      FSTERROR() << "FarCompileStrings: Read from a file instead of stdin or"
                 << " set the --generate_keys flags.";
      delete far_writer;
      delete syms;
      return;
    }
    int key_size =
        generate_keys ? generate_keys : (entry_type == StringReader<Arc>::FILE
                                             ? 1
                                             : KeySize(in_fnames[i].c_str()));
    std::istream *istrm =
        in_fnames[i].empty() ? &std::cin : new std::ifstream(in_fnames[i]);

    bool keep_syms = keep_symbols;
    for (StringReader<Arc> reader(
             *istrm, in_fnames[i].empty() ? "stdin" : in_fnames[i], entry_type,
             token_type, allow_negative_labels, syms, unknown_label);
         !reader.Done(); reader.Next()) {
      ++n;
      const Fst<Arc> *fst;
      if (compact)
        fst = reader.GetCompactFst(keep_syms);
      else
        fst = reader.GetVectorFst(keep_syms);
      if (initial_symbols) keep_syms = false;
      if (!fst) {
        FSTERROR() << "FarCompileStrings: Compiling string number " << n
                   << " in file " << in_fnames[i]
                   << " failed with token_type = "
                   << (tt == FTT_BYTE
                           ? "byte"
                           : (tt == FTT_UTF8
                                  ? "utf8"
                                  : (tt == FTT_SYMBOL ? "symbol" : "unknown")))
                   << " and entry_type = "
                   << (fet == FET_LINE
                           ? "line"
                           : (fet == FET_FILE ? "file" : "unknown"));
        delete far_writer;
        delete syms;
        if (!in_fnames[i].empty()) delete istrm;
        return;
      }
      std::ostringstream keybuf;
      keybuf.width(key_size);
      keybuf.fill('0');
      keybuf << n;
      string key;
      if (generate_keys > 0) {
        key = keybuf.str();
      } else {
        char *filename = new char[in_fnames[i].size() + 1];
        strcpy(filename, in_fnames[i].c_str());
        key = basename(filename);
        if (entry_type != StringReader<Arc>::FILE) {
          key += "-";
          key += keybuf.str();
        }
        delete[] filename;
      }
      far_writer->Add(key_prefix + key + key_suffix, *fst);
      delete fst;
    }
    if (generate_keys == 0) n = 0;
    if (!in_fnames[i].empty()) delete istrm;
  }

  delete far_writer;
}

}  // namespace fst

#endif  // FST_EXTENSIONS_FAR_COMPILE_STRINGS_H_
