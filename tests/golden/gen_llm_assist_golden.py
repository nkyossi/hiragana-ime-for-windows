# Generates golden values for ibus-hiragana's LanguageModel.assist and
# LanguageModel.assist_yougen using the REAL PyTorch model, so that the C++
# (ONNX Runtime) port can be verified against the original implementation.
#
#   python tests/golden/gen_llm_assist_golden.py <output.txt> [scenario-count]
#
# The input cases are the ones the engine actually produces: gen_engine_golden.py
# already builds a fake IBus/Gio environment and runs key events through the
# original EngineHiragana.  Here its scenario driver is reused, but
# EngineHiragana._load_llm() is monkeypatched to return a small recorder that
# remembers every assist()/assist_yougen() call and returns a trivial dict.
# The distinct recorded argument tuples are then evaluated with the real model.
#
# Output format (tab separated, LF, UTF-8).  Tabs inside a field are escaped as
# \t, newlines as \n and backslash (first) as \\.  List elements are joined with
# U+001F.  Floats use repr(float).  The index=value pairs are sorted by index.
#
#   # llm assist golden
#   A<TAB>prefix<TAB>yomi<TAB>word1<US>word2<US>...<TAB>i1=v1 i2=v2 ...
#   Y<TAB>prefix<TAB>yomi<TAB>stem1<US>stem2<US>...<TAB>i1=v1 i2=v2 ...

import builtins
import os
import random
import shutil
import sys
import tempfile
import types

# On Windows the default text encoding is the ANSI code page (cp932 here), but
# the upstream code opens the dictionary/data files without an explicit
# encoding and they are UTF-8.  Default text-mode opens to UTF-8 unless the
# interpreter already runs in UTF-8 mode (PYTHONUTF8=1 / -X utf8).
if sys.flags.utf8_mode == 0:
    _open = builtins.open

    def _utf8_open(file, mode='r', *args, **kwargs):
        if 'b' not in mode and 'encoding' not in kwargs and len(args) < 2:
            kwargs['encoding'] = 'utf-8'
        return _open(file, mode, *args, **kwargs)

    builtins.open = _utf8_open

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
UPSTREAM = os.path.join(ROOT, 'ibus-hiragana')

sys.path.insert(0, HERE)

# gen_engine_golden installs the fake gi.repository, a stand-in 'package'
# module and imports the original engine (which imports llm and dictionary).
import gen_engine_golden as geg  # noqa: E402

import llm  # noqa: E402  (already imported by engine)

# Use the model that is actually present in the local Hugging Face cache.  The
# upstream constant points at the 'cl-tohoku/...' org, whose cache entry differs.
llm.MODEL_NAME = 'tohoku-nlp/bert-base-japanese-v3'

# llm.py reads os.path.join(package.get_datadir(), 'dic', <data file>), i.e. it
# expects the installed pkgdatadir layout (<datadir>/dic/...).  In the source
# tree the data files live directly in ibus-hiragana/data/ and the system
# dictionaries in ibus-hiragana/dic/.  Build a staging datadir with the three
# data files under dic/ and give llm its own package shim so the engine's
# package (which still points at UPSTREAM for the dictionaries/layouts) is
# untouched.
DATA_DIR = os.path.join(UPSTREAM, 'data')
STAGING = os.path.join(tempfile.gettempdir(), 'hiragana-llm-data')
STAGING_DIC = os.path.join(STAGING, 'dic')
os.makedirs(STAGING_DIC, exist_ok=True)
for _name in ('yougen_token.dic', 'katuyou_token.dic', 'kango_token.json'):
    shutil.copyfile(os.path.join(DATA_DIR, _name), os.path.join(STAGING_DIC, _name))

llm.package = types.SimpleNamespace(get_datadir=lambda: STAGING)

MAX_ASSIST = 300
MAX_YOUGEN = 200
US = '\x1f'


class Recorder:
    """Stand-in language model that records the calls the engine makes."""

    def __init__(self):
        self.assist_calls = []
        self.yougen_calls = []

    def assist(self, prefix, yomi, words):
        self.assist_calls.append((prefix, yomi, tuple(words)))
        return {i: 1.0 for i in range(len(words))}

    def assist_yougen(self, prefix, yomi, stem_list):
        self.yougen_calls.append((prefix, yomi, tuple(stem_list)))
        return {i: 1.0 for i in range(len(stem_list))}

    def device_type(self):
        return 'cpu'


RECORDER = Recorder()


def _load_llm(self):
    return RECORDER if self._settings.get_boolean('use-llm') else None


geg.engine_module.EngineHiragana._load_llm = _load_llm


def collect(count):
    """Replay the LLM scenarios and record every assist/assist_yougen call."""
    taigen, yougen = geg.readings('restrained.9.dic')
    for name, layout, xkb, surrounding, options in geg.SCENARIOS:
        if not options.get('use-llm'):
            continue
        for n in range(count):
            rnd = random.Random(f'{name}-{n}')
            geg.Gio.Settings.values = geg.settings_for(layout, xkb, options)
            e = geg.engine_module.EngineHiragana(geg.fake_gi._Anything(), '', None)
            if surrounding:
                e._surrounding = geg.engine_module.SURROUNDING_SUPPORTED
            else:
                e.client_capabilities = geg.IBus.Capabilite.PREEDIT_TEXT
            kb = geg.Keyboard(layout, xkb)
            try:
                for keyval, keycode, state in geg.scenario_events(kb, rnd, taigen, yougen, 25):
                    e.do_process_key_event(keyval, keycode, state)
                    if surrounding:
                        # The application reports the new surrounding text.
                        e._surrounding = geg.engine_module.SURROUNDING_SUPPORTED
                        e._preedit_text = None
            except (AssertionError, ValueError, IndexError):
                # The engine aborts on some synthetic inputs; keep what was
                # recorded so far.
                pass


def dedup(calls, limit):
    seen = set()
    out = []
    for call in calls:
        if call in seen:
            continue
        seen.add(call)
        out.append(call)
        if len(out) >= limit:
            break
    return out


def escape(text):
    return text.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n')


def pairs(values):
    return ' '.join(f'{index}={float(value)!r}' for index, value in sorted(values.items()))


def main():
    if len(sys.argv) < 2:
        sys.exit(f'usage: {sys.argv[0]} <output.txt> [scenario-count]')
    out = os.path.abspath(sys.argv[1])
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    os.makedirs(os.path.dirname(out), exist_ok=True)

    print(f'collecting cases ({count} scenarios per LLM layout) ...', flush=True)
    collect(count)
    assist_cases = dedup(RECORDER.assist_calls, MAX_ASSIST)
    yougen_cases = dedup(RECORDER.yougen_calls, MAX_YOUGEN)
    print(f'recorded {len(assist_cases)} distinct assist and '
          f'{len(yougen_cases)} distinct assist_yougen cases', flush=True)
    print(f'staging datadir: {STAGING}', flush=True)

    print('loading the real model ...', flush=True)
    model = llm.LanguageModel('cpu')

    errors = 0
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# llm assist golden\n')
        for i, (prefix, yomi, words) in enumerate(assist_cases):
            try:
                result = model.assist(prefix, yomi, list(words))
            except Exception as e:  # pragma: no cover - diagnostics only
                errors += 1
                print(f'assist case {i} failed: {e!r}: {prefix!r} {yomi!r} {words!r}',
                      file=sys.stderr, flush=True)
                continue
            f.write('A\t' + escape(prefix) + '\t' + escape(yomi) + '\t' +
                    US.join(escape(word) for word in words) + '\t' + pairs(result) + '\n')
            if (i + 1) % 25 == 0:
                print(f'  assist {i + 1}/{len(assist_cases)}', flush=True)
        for i, (prefix, yomi, stems) in enumerate(yougen_cases):
            try:
                result = model.assist_yougen(prefix, yomi, list(stems))
            except Exception as e:  # pragma: no cover - diagnostics only
                errors += 1
                print(f'assist_yougen case {i} failed: {e!r}: {prefix!r} {yomi!r} {stems!r}',
                      file=sys.stderr, flush=True)
                continue
            f.write('Y\t' + escape(prefix) + '\t' + escape(yomi) + '\t' +
                    US.join(escape(stem) for stem in stems) + '\t' + pairs(result) + '\n')
            if (i + 1) % 25 == 0:
                print(f'  assist_yougen {i + 1}/{len(yougen_cases)}', flush=True)

    print(f'wrote {out}: {len(assist_cases)} A and {len(yougen_cases)} Y cases '
          f'({errors} errors)', flush=True)


if __name__ == '__main__':
    main()
