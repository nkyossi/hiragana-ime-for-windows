# Generates golden assisted_lookup results with the original ibus-hiragana
# Python Dictionary and a deterministic stand-in for the language model, so
# that the C++ port can be checked against it.
#
#   python tests/golden/gen_llm_golden.py <output-dir> [system.dic ...]
#
# Writes <output-dir>/assisted.<system>.<romazi|kana>.tsv with the columns
#   text, pos, anchor, reading, candidates, suggested, is_complete
#
# The stand-in model returns a probability derived from a hash of the preceding
# text and the candidate, which exercises every branch of assisted_lookup()
# (candidate reordering and reading shrinking) without needing PyTorch.  The
# same hash is implemented by tests/assisted_golden_test.cpp.

import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# gen_golden.py provides the 'package' stand-in and imports Dictionary.
import gen_golden  # noqa: E402
from gen_golden import Dictionary, cases  # noqa: E402

MOD = 1000003


class FakeModel:
    @staticmethod
    def _score(prefix, word):
        h = 0
        for ch in prefix:
            h = (h * 31 + ord(ch)) % MOD
        h = (h * 31) % MOD  # separator
        for ch in word:
            h = (h * 31 + ord(ch)) % MOD
        return h

    def _probs(self, prefix, words):
        return {i: self._score(prefix, word) / MOD for i, word in enumerate(words)}

    def assist(self, prefix, yomi, words):
        return self._probs(prefix, words)

    def assist_yougen(self, prefix, yomi, words):
        return self._probs(prefix, words)

    def device_type(self):
        return 'cpu'


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    systems = sys.argv[2:] or ['restrained.9.dic', 'restrained.1.dic']
    for system in systems:
        for romazi in (True, False):
            rnd = random.Random(1)
            d = Dictionary(system, '')
            d.use_romazi(romazi)
            model = FakeModel()
            name = f'assisted.{system[:-4]}.{"romazi" if romazi else "kana"}.tsv'
            path = os.path.join(out_dir, name)
            n = 0
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                for text, arg in cases(d, rnd):
                    if arg is None:
                        pos, anchor = len(text), 0
                    elif text.endswith('x'):
                        pos, anchor = arg, 0
                    else:
                        pos, anchor = len(text), arg
                    current, suggested = d.assisted_lookup(model, text, pos, anchor)
                    cand = '/'.join(d.cand()) if current else ''
                    f.write(f'{text}\t{pos}\t{anchor}\t{d.reading() if current else ""}\t{cand}\t'
                            f'{suggested}\t{int(d.is_complete())}\n')
                    n += 1
            print(f'{name}: {n} cases')


if __name__ == '__main__':
    main()
