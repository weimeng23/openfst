// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Generates a random FST according to a class-specific transition model.

#ifndef FST_EXTENSIONS_COMPRESS_RANDMOD_H_
#define FST_EXTENSIONS_COMPRESS_RANDMOD_H_

#include <vector>

#include <fst/compat.h>
#include <fst/mutable-fst.h>

namespace fst {

template <class Arc, class G>
class RandMod {
 public:
  typedef typename Arc::StateId StateId;
  typedef typename Arc::Label Label;
  typedef typename Arc::Weight Weight;

  // Generate random FST with 'nstates' with 'nclasses' in
  // the probability generation model, and 'nlabels' in the alphabet.
  // If 'trans' = true, then generate a transducer.
  // If 'weight_gen' is non-NULL, output a weighted FST.
  RandMod(StateId nstates, StateId nclasses, Label nlabels, bool trans,
          const G *weight_gen)
      : nstates_(nstates),
        nclasses_(nclasses),
        nlabels_(nlabels),
        trans_(trans),
        weight_gen_(weight_gen) {
    for (StateId s = 0; s < nstates; ++s) {
      classes_.push_back(rand() % nclasses);  // NOLINT
    }
  }

  // Generates a random FST according to a class-specific transition model
  void Generate(StdMutableFst *fst) {
    StateId start = rand() % nstates_;  // NOLINT
    fst->DeleteStates();
    for (StateId s = 0; s < nstates_; ++s) {
      fst->AddState();
      if (s == start) fst->SetStart(start);
      for (StateId n = 0; n <= nstates_; ++n) {
        Arc arc;
        StateId d = n == nstates_ ? kNoStateId : n;
        if (!RandArc(s, d, &arc)) continue;
        if (d == kNoStateId) {  // a super-final transition?
          fst->SetFinal(s, arc.weight);
        } else {
          fst->AddArc(s, arc);
        }
      }
    }
  }

 private:
  // Generates a transition from s to d.
  // If d == kNoStateId, generate a super-final transition.
  // Returns false if no transition generated.
  bool RandArc(StateId s, StateId d, Arc *arc) {
    StateId sclass = classes_[s];
    StateId dclass = d != kNoStateId ? classes_[d] : 0;

    int r = sclass + dclass + 2;
    if ((rand() % r) != 0)  // NOLINT
      return false;

    arc->nextstate = d;

    Label ilabel = kNoLabel;
    Label olabel = kNoLabel;
    if (d != kNoStateId) {
      ilabel = (dclass % nlabels_) + 1;
      if (trans_)
        olabel = (sclass % nlabels_) + 1;
      else
        olabel = ilabel;
    }

    Weight weight = Weight::One();
    if (weight_gen_) weight = (*weight_gen_)();

    arc->ilabel = ilabel;
    arc->olabel = olabel;
    arc->weight = weight;
    return true;
  }

  StateId nstates_;
  StateId nclasses_;
  Label nlabels_;
  bool trans_;
  const G *weight_gen_;
  std::vector<StateId> classes_;
};

}  // namespace fst

#endif  // FST_EXTENSIONS_COMPRESS_RANDMOD_H_
