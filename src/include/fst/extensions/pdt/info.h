// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Prints information about a PDT.

#ifndef FST_EXTENSIONS_PDT_INFO_H__
#define FST_EXTENSIONS_PDT_INFO_H__

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fst/extensions/pdt/pdt.h>
#include <fst/fst.h>

namespace fst {

// Compute various information about PDTs, helper class for pdtinfo.cc.
template <class A>
class PdtInfo {
 public:
  typedef A Arc;
  typedef typename A::StateId StateId;
  typedef typename A::Label Label;
  typedef typename A::Weight Weight;

  PdtInfo(const Fst<A> &fst,
          const std::vector<std::pair<typename A::Label, typename A::Label>>
              &parens);

  const string &FstType() const { return fst_type_; }
  const string &ArcType() const { return A::Type(); }

  int64 NumStates() const { return nstates_; }
  int64 NumArcs() const { return narcs_; }
  int64 NumOpenParens() const { return nopen_parens_; }
  int64 NumCloseParens() const { return nclose_parens_; }
  int64 NumUniqueOpenParens() const { return nuniq_open_parens_; }
  int64 NumUniqueCloseParens() const { return nuniq_close_parens_; }
  int64 NumOpenParenStates() const { return nopen_paren_states_; }
  int64 NumCloseParenStates() const { return nclose_paren_states_; }

 private:
  string fst_type_;
  int64 nstates_;
  int64 narcs_;
  int64 nopen_parens_;
  int64 nclose_parens_;
  int64 nuniq_open_parens_;
  int64 nuniq_close_parens_;
  int64 nopen_paren_states_;
  int64 nclose_paren_states_;

  DISALLOW_COPY_AND_ASSIGN(PdtInfo);
};

template <class A>
PdtInfo<A>::PdtInfo(
    const Fst<A> &fst,
    const std::vector<std::pair<typename A::Label, typename A::Label>> &parens)
    : fst_type_(fst.Type()),
      nstates_(0),
      narcs_(0),
      nopen_parens_(0),
      nclose_parens_(0),
      nuniq_open_parens_(0),
      nuniq_close_parens_(0),
      nopen_paren_states_(0),
      nclose_paren_states_(0) {
  std::unordered_map<Label, size_t> paren_map;
  std::unordered_set<Label> paren_set;
  std::unordered_set<StateId> open_paren_state_set;
  std::unordered_set<StateId> close_paren_state_set;

  for (size_t i = 0; i < parens.size(); ++i) {
    const std::pair<Label, Label> &p = parens[i];
    paren_map[p.first] = i;
    paren_map[p.second] = i;
  }

  for (StateIterator<Fst<A>> siter(fst); !siter.Done(); siter.Next()) {
    ++nstates_;
    StateId s = siter.Value();
    for (ArcIterator<Fst<A>> aiter(fst, s); !aiter.Done(); aiter.Next()) {
      const A &arc = aiter.Value();
      ++narcs_;
      typename std::unordered_map<Label, size_t>::const_iterator pit =
          paren_map.find(arc.ilabel);
      if (pit != paren_map.end()) {
        Label open_paren = parens[pit->second].first;
        Label close_paren = parens[pit->second].second;
        if (arc.ilabel == open_paren) {
          ++nopen_parens_;
          if (!paren_set.count(open_paren)) {
            ++nuniq_open_parens_;
            paren_set.insert(open_paren);
          }
          if (!open_paren_state_set.count(arc.nextstate)) {
            ++nopen_paren_states_;
            open_paren_state_set.insert(arc.nextstate);
          }
        } else {
          ++nclose_parens_;
          if (!paren_set.count(close_paren)) {
            ++nuniq_close_parens_;
            paren_set.insert(close_paren);
          }
          if (!close_paren_state_set.count(s)) {
            ++nclose_paren_states_;
            close_paren_state_set.insert(s);
          }
        }
      }
    }
  }
}

template <class A>
void PrintPdtInfo(const PdtInfo<A> &pdtinfo) {
  ios_base::fmtflags old = std::cout.setf(ios::left);
  std::cout.width(50);
  std::cout << "fst type" << pdtinfo.FstType().c_str() << std::endl;
  std::cout.width(50);
  std::cout << "arc type" << pdtinfo.ArcType().c_str() << std::endl;
  std::cout.width(50);
  std::cout << "# of states" << pdtinfo.NumStates() << std::endl;
  std::cout.width(50);
  std::cout << "# of arcs" << pdtinfo.NumArcs() << std::endl;
  std::cout.width(50);
  std::cout << "# of open parentheses" << pdtinfo.NumOpenParens() << std::endl;
  std::cout.width(50);
  std::cout << "# of close parentheses" << pdtinfo.NumCloseParens()
            << std::endl;
  std::cout.width(50);
  std::cout << "# of unique open parentheses" << pdtinfo.NumUniqueOpenParens()
            << std::endl;
  std::cout.width(50);
  std::cout << "# of unique close parentheses" << pdtinfo.NumUniqueCloseParens()
            << std::endl;
  std::cout.width(50);
  std::cout << "# of open parenthesis dest. states"
            << pdtinfo.NumOpenParenStates() << std::endl;
  std::cout.width(50);
  std::cout << "# of close parenthesis source states"
            << pdtinfo.NumCloseParenStates() << std::endl;
  std::cout.setf(old);
}

}  // namespace fst

#endif  // FST_EXTENSIONS_PDT_INFO_H__
