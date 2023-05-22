// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Classes to provide symbol-to-integer and integer-to-symbol mappings.

#include <fst/symbol-table.h>

#include <fst/flags.h>
#include <fst/types.h>
#include <fst/log.h>

#include <fstream>
#include <fst/util.h>

DEFINE_bool(fst_compat_symbols, true,
            "Require symbol tables to match when appropriate");
DEFINE_string(fst_field_separator, "\t ",
              "Set of characters used as a separator between printed fields");

namespace fst {

SymbolTableTextOptions::SymbolTableTextOptions(bool allow_negative_labels)
    : allow_negative_labels(allow_negative_labels),
      fst_field_separator(FLAGS_fst_field_separator) {}

namespace internal {

// Maximum line length in textual symbols file.
const int kLineLen = 8096;

// Identifies stream data as a symbol table (and its endianity).
static constexpr int32 kSymbolTableMagicNumber = 2125658996;

constexpr int64 DenseSymbolMap::kEmptyBucket;

DenseSymbolMap::DenseSymbolMap()
    : str_hash_(),
      buckets_(1 << 4, kEmptyBucket),
      hash_mask_(buckets_.size() - 1) {}

std::pair<int64, bool> DenseSymbolMap::InsertOrFind(KeyType key) {
  static constexpr float kMaxOccupancyRatio = 0.75;  // Grows when 75% full.
  if (Size() >= kMaxOccupancyRatio * buckets_.size()) {
    Rehash(buckets_.size() * 2);
  }
  size_t idx = GetHash(key);
  while (buckets_[idx] != kEmptyBucket) {
    const auto stored_value = buckets_[idx];
    if (symbols_[stored_value] == key) return {stored_value, false};
    idx = (idx + 1) & hash_mask_;
  }
  const auto next = Size();
  buckets_[idx] = next;
  symbols_.emplace_back(key);
  return {next, true};
}

int64 DenseSymbolMap::Find(KeyType key) const {
  size_t idx = str_hash_(key) & hash_mask_;
  while (buckets_[idx] != kEmptyBucket) {
    const auto stored_value = buckets_[idx];
    if (symbols_[stored_value] == key) return stored_value;
    idx = (idx + 1) & hash_mask_;
  }
  return buckets_[idx];
}

void DenseSymbolMap::Rehash(size_t num_buckets) {
  buckets_.resize(num_buckets);
  hash_mask_ = buckets_.size() - 1;
  std::fill(buckets_.begin(), buckets_.end(), kEmptyBucket);
  for (size_t i = 0; i < Size(); ++i) {
    size_t idx = GetHash(symbols_[i]);
    while (buckets_[idx] != kEmptyBucket) {
      idx = (idx + 1) & hash_mask_;
    }
    buckets_[idx] = i;
  }
}

void DenseSymbolMap::RemoveSymbol(size_t idx) {
  symbols_.erase(symbols_.begin() + idx);
  Rehash(buckets_.size());
}

void DenseSymbolMap::ShrinkToFit() {
  symbols_.shrink_to_fit();
}

void MutableSymbolTableImpl::AddTable(const SymbolTable &table) {
  for (SymbolTableIterator iter(table); !iter.Done(); iter.Next()) {
    AddSymbol(iter.Symbol());
  }
}

std::unique_ptr<SymbolTableImplBase> ConstSymbolTableImpl::Copy() const {
  LOG(FATAL) << "ConstSymbolTableImpl can't be copied";
  return nullptr;
}

int64 ConstSymbolTableImpl::AddSymbol(SymbolType symbol, int64 key) {
  LOG(FATAL) << "ConstSymbolTableImpl does not support AddSymbol";
  return kNoSymbol;
}

int64 ConstSymbolTableImpl::AddSymbol(SymbolType symbol) {
  return AddSymbol(symbol, kNoSymbol);
}

void ConstSymbolTableImpl::RemoveSymbol(int64 key) {
  LOG(FATAL) << "ConstSymbolTableImpl does not support RemoveSymbol";
}

void ConstSymbolTableImpl::SetName(const std::string &new_name) {
  LOG(FATAL) << "ConstSymbolTableImpl does not support SetName";
}

void ConstSymbolTableImpl::AddTable(const SymbolTable &table) {
  LOG(FATAL) << "ConstSymbolTableImpl does not support AddTable";
}

SymbolTableImpl *SymbolTableImpl::ReadText(std::istream &strm,
                                           const std::string &source,
                                           const SymbolTableTextOptions &opts) {
  std::unique_ptr<SymbolTableImpl> impl(new SymbolTableImpl(source));
  int64 nline = 0;
  char line[kLineLen];
  while (!strm.getline(line, kLineLen).fail()) {
    ++nline;
    std::vector<char *> col;
    const auto separator = opts.fst_field_separator + "\n";
    SplitString(line, separator.c_str(), &col, true);
    if (col.empty()) continue;  // Empty line.
    if (col.size() != 2) {
      LOG(ERROR) << "SymbolTable::ReadText: Bad number of columns ("
                 << col.size() << "), "
                 << "file = " << source << ", line = " << nline << ":<" << line
                 << ">";
      return nullptr;
    }
    const char *symbol = col[0];
    const char *value = col[1];
    char *p;
    const auto key = strtoll(value, &p, 10);
    if (p < value + strlen(value) || (!opts.allow_negative_labels && key < 0) ||
        key == kNoSymbol) {
      LOG(ERROR) << "SymbolTable::ReadText: Bad non-negative integer \""
                 << value << "\", "
                 << "file = " << source << ", line = " << nline;
      return nullptr;
    }
    impl->AddSymbol(symbol, key);
  }
  impl->ShrinkToFit();
  return impl.release();
}

void SymbolTableImpl::MaybeRecomputeCheckSum() const {
  {
    ReaderMutexLock check_sum_lock(&check_sum_mutex_);
    if (check_sum_finalized_) return;
  }
  // We'll acquire an exclusive lock to recompute the checksums.
  MutexLock check_sum_lock(&check_sum_mutex_);
  if (check_sum_finalized_) {  // Another thread (coming in around the same time
    return;                    // might have done it already). So we recheck.
  }
  // Calculates the original label-agnostic checksum.
  CheckSummer check_sum;
  for (size_t i = 0; i < symbols_.Size(); ++i) {
    const auto &symbol = symbols_.GetSymbol(i);
    check_sum.Update(symbol.data(), symbol.size());
    check_sum.Update("", 1);
  }
  check_sum_string_ = check_sum.Digest();
  // Calculates the safer, label-dependent checksum.
  CheckSummer labeled_check_sum;
  for (int64 i = 0; i < dense_key_limit_; ++i) {
    std::ostringstream line;
    line << symbols_.GetSymbol(i) << '\t' << i;
    labeled_check_sum.Update(line.str().data(), line.str().size());
  }
  using citer = std::map<int64, int64>::const_iterator;
  for (citer it = key_map_.begin(); it != key_map_.end(); ++it) {
    // TODO(tombagby, 2013-11-22) This line maintains a bug that ignores
    // negative labels in the checksum that too many tests rely on.
    if (it->first < dense_key_limit_) continue;
    std::ostringstream line;
    line << symbols_.GetSymbol(it->second) << '\t' << it->first;
    labeled_check_sum.Update(line.str().data(), line.str().size());
  }
  labeled_check_sum_string_ = labeled_check_sum.Digest();
  check_sum_finalized_ = true;
}

std::string SymbolTableImpl::Find(int64 key) const {
  int64 idx = key;
  if (key < 0 || key >= dense_key_limit_) {
    const auto it = key_map_.find(key);
    if (it == key_map_.end()) return "";
    idx = it->second;
  }
  if (idx < 0 || idx >= symbols_.Size()) return "";
  return symbols_.GetSymbol(idx);
}

int64 SymbolTableImpl::AddSymbol(SymbolType symbol, int64 key) {
  if (key == kNoSymbol) return key;
  const auto insert_key = symbols_.InsertOrFind(symbol);
  if (!insert_key.second) {
    const auto key_already = GetNthKey(insert_key.first);
    if (key_already == key) return key;
    VLOG(1) << "SymbolTable::AddSymbol: symbol = " << symbol
            << " already in symbol_map_ with key = " << key_already
            << " but supplied new key = " << key << " (ignoring new key)";
    return key_already;
  }
  if (key + 1 == static_cast<int64>(symbols_.Size()) &&
      key == dense_key_limit_) {
    ++dense_key_limit_;
  } else {
    idx_key_.push_back(key);
    key_map_[key] = symbols_.Size() - 1;
  }
  if (key >= available_key_) available_key_ = key + 1;
  check_sum_finalized_ = false;
  return key;
}

// TODO(rybach): Consider a more efficient implementation which re-uses holes in
// the dense-key range or re-arranges the dense-key range from time to time.
void SymbolTableImpl::RemoveSymbol(const int64 key) {
  auto idx = key;
  if (key < 0 || key >= dense_key_limit_) {
    auto iter = key_map_.find(key);
    if (iter == key_map_.end()) return;
    idx = iter->second;
    key_map_.erase(iter);
  }
  if (idx < 0 || idx >= static_cast<int64>(symbols_.Size())) return;
  symbols_.RemoveSymbol(idx);
  // Removed one symbol, all indexes > idx are shifted by -1.
  for (auto &k : key_map_) {
    if (k.second > idx) --k.second;
  }
  if (key >= 0 && key < dense_key_limit_) {
    // Removal puts a hole in the dense key range. Adjusts range to [0, key).
    const auto new_dense_key_limit = key;
    for (int64 i = key + 1; i < dense_key_limit_; ++i) {
      key_map_[i] = i - 1;
    }
    // Moves existing values in idx_key to new place.
    idx_key_.resize(symbols_.Size() - new_dense_key_limit);
    for (int64 i = symbols_.Size(); i >= dense_key_limit_; --i) {
      idx_key_[i - new_dense_key_limit - 1] = idx_key_[i - dense_key_limit_];
    }
    // Adds indexes for previously dense keys.
    for (int64 i = new_dense_key_limit; i < dense_key_limit_ - 1; ++i) {
      idx_key_[i - new_dense_key_limit] = i + 1;
    }
    dense_key_limit_ = new_dense_key_limit;
  } else {
    // Remove entry for removed index in idx_key.
    for (size_t i = idx - dense_key_limit_; i + 1 < idx_key_.size(); ++i) {
      idx_key_[i] = idx_key_[i + 1];
    }
    idx_key_.pop_back();
  }
  if (key == available_key_ - 1) available_key_ = key;
}

SymbolTableImpl *SymbolTableImpl::Read(std::istream &strm,
                                       const SymbolTableReadOptions &) {
  int32 magic_number = 0;
  ReadType(strm, &magic_number);
  if (strm.fail()) {
    LOG(ERROR) << "SymbolTable::Read: Read failed";
    return nullptr;
  }
  std::string name;
  ReadType(strm, &name);
  std::unique_ptr<SymbolTableImpl> impl(new SymbolTableImpl(name));
  ReadType(strm, &impl->available_key_);
  int64 size;
  ReadType(strm, &size);
  if (strm.fail()) {
    LOG(ERROR) << "SymbolTable::Read: Read failed";
    return nullptr;
  }
  std::string symbol;
  int64 key;
  impl->check_sum_finalized_ = false;
  for (int64 i = 0; i < size; ++i) {
    ReadType(strm, &symbol);
    ReadType(strm, &key);
    if (strm.fail()) {
      LOG(ERROR) << "SymbolTable::Read: Read failed";
      return nullptr;
    }
    impl->AddSymbol(symbol, key);
  }
  impl->ShrinkToFit();
  return impl.release();
}

bool SymbolTableImpl::Write(std::ostream &strm) const {
  WriteType(strm, kSymbolTableMagicNumber);
  WriteType(strm, name_);
  WriteType(strm, available_key_);
  const int64 size = symbols_.Size();
  WriteType(strm, size);
  for (int64 i = 0; i < dense_key_limit_; ++i) {
    WriteType(strm, symbols_.GetSymbol(i));
    WriteType(strm, i);
  }
  for (const auto &p : key_map_) {
    WriteType(strm, symbols_.GetSymbol(p.second));
    WriteType(strm, p.first);
  }
  strm.flush();
  if (strm.fail()) {
    LOG(ERROR) << "SymbolTable::Write: Write failed";
    return false;
  }
  return true;
}

void SymbolTableImpl::ShrinkToFit() {
  symbols_.ShrinkToFit();
}

}  // namespace internal

SymbolTable *SymbolTable::ReadText(const std::string &source,
                                   const SymbolTableTextOptions &opts) {
  std::ifstream strm(source, std::ios_base::in);
  if (!strm.good()) {
    LOG(ERROR) << "SymbolTable::ReadText: Can't open file: " << source;
    return nullptr;
  }
  return ReadText(strm, source, opts);
}

bool SymbolTable::Write(const std::string &source) const {
  if (!source.empty()) {
    std::ofstream strm(source,
                             std::ios_base::out | std::ios_base::binary);
    if (!strm) {
      LOG(ERROR) << "SymbolTable::Write: Can't open file: " << source;
      return false;
    }
    if (!Write(strm)) {
      LOG(ERROR) << "SymbolTable::Write: Write failed: " << source;
      return false;
    }
    return true;
  } else {
    return Write(std::cout);
  }
}

bool SymbolTable::WriteText(std::ostream &strm,
                            const SymbolTableTextOptions &opts) const {
  if (opts.fst_field_separator.empty()) {
    LOG(ERROR) << "Missing required field separator";
    return false;
  }
  bool once_only = false;
  for (SymbolTableIterator iter(*this); !iter.Done(); iter.Next()) {
    std::ostringstream line;
    if (iter.Value() < 0 && !opts.allow_negative_labels && !once_only) {
      LOG(WARNING) << "Negative symbol table entry when not allowed";
      once_only = true;
    }
    line << iter.Symbol() << opts.fst_field_separator[0] << iter.Value()
         << '\n';
    strm.write(line.str().data(), line.str().length());
  }
  return true;
}

bool SymbolTable::WriteText(const std::string &source) const {
  if (!source.empty()) {
    std::ofstream strm(source);
    if (!strm) {
      LOG(ERROR) << "SymbolTable::WriteText: Can't open file: " << source;
      return false;
    }
    if (!WriteText(strm, SymbolTableTextOptions())) {
      LOG(ERROR) << "SymbolTable::WriteText: Write failed: " << source;
      return false;
    }
    return true;
  } else {
    return WriteText(std::cout, SymbolTableTextOptions());
  }
}

SymbolTable::const_iterator SymbolTable::begin() const {
  return SymbolTable::const_iterator(*this, 0);
}

SymbolTable::const_iterator SymbolTable::end() const {
  return SymbolTable::const_iterator(*this, this->NumSymbols());
}

SymbolTable::const_iterator SymbolTable::cbegin() const { return begin(); }

SymbolTable::const_iterator SymbolTable::cend() const { return end(); }

bool CompatSymbols(const SymbolTable *syms1, const SymbolTable *syms2,
                   bool warning) {
  // Flag can explicitly override this check.
  if (!FLAGS_fst_compat_symbols) return true;
  if (syms1 && syms2 &&
      (syms1->LabeledCheckSum() != syms2->LabeledCheckSum())) {
    if (warning) {
      LOG(WARNING) << "CompatSymbols: Symbol table checksums do not match. "
                   << "Table sizes are " << syms1->NumSymbols() << " and "
                   << syms2->NumSymbols();
    }
    return false;
  } else {
    return true;
  }
}

void SymbolTableToString(const SymbolTable *table, std::string *result) {
  std::ostringstream ostrm;
  table->Write(ostrm);
  *result = ostrm.str();
}

SymbolTable *StringToSymbolTable(const std::string &str) {
  std::istringstream istrm(str);
  return SymbolTable::Read(istrm, SymbolTableReadOptions());
}

}  // namespace fst
