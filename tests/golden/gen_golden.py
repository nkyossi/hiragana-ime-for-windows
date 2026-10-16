# Generates golden lookup results with the original ibus-hiragana Python
# Dictionary so that the C++ port can be checked against it.
#
#   python tests/golden/gen_golden.py <output-dir> [system.dic ...]
#
# Writes <output-dir>/lookup.<system>.<romazi|kana>.tsv with the columns
#   text, pos, anchor, reading, candidates joined with '/', is_complete

import os
import random
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
UPSTREAM = os.path.join(ROOT, 'ibus-hiragana')

# dictionary.py imports 'package' for the data directories; provide a stand-in
# so that the module can be used without GLib.
USER_DIR = tempfile.mkdtemp(prefix='hiragana-golden-')
package = types.ModuleType('package')
package.get_datadir = lambda: UPSTREAM
package.get_user_datadir = lambda: USER_DIR
sys.modules['package'] = package
sys.path.insert(0, os.path.join(UPSTREAM, 'engine'))

from dictionary import Dictionary, HIRAGANA, KATUYOU  # noqa: E402

ENDINGS = sorted({k for v in KATUYOU.values() for k in v if k} | {'', 'な', 'ない', 'ます', 'ました', 'られる'})


def cases(d: Dictionary, rnd: random.Random):
    keys = sorted(d._dict.keys())
    for yomi in keys:
        if yomi.startswith('#'):
            yield '3' + yomi[1:], None
            yield '１２' + yomi[1:], None
        elif yomi.endswith('―'):
            for ending in ENDINGS:
                yield yomi + ending, None
            yield 'は' + yomi + 'い', None
            # Partially typed okurigana
            yield yomi + rnd.choice(HIRAGANA[:46]), None
        else:
            yield yomi, None
            yield 'を' + yomi, None
            yield 'あ' + yomi + 'x', len(yomi) + 1
            if 2 <= len(yomi):
                yield yomi, 1   # anchor


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    systems = sys.argv[2:] or ['restrained.9.dic', 'restrained.1.dic']
    for system in systems:
        for romazi in (True, False):
            rnd = random.Random(1)
            d = Dictionary(system, '')
            d.use_romazi(romazi)
            name = f'lookup.{system[:-4]}.{"romazi" if romazi else "kana"}.tsv'
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
                    current = d.lookup(text, pos, anchor)
                    cand = '/'.join(d.cand()) if current else ''
                    f.write(f'{text}\t{pos}\t{anchor}\t{d.reading() if current else ""}\t{cand}\t'
                            f'{int(d.is_complete())}\n')
                    n += 1
            print(f'{name}: {n} cases')


if __name__ == '__main__':
    main()
