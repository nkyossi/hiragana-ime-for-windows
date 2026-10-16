# Runs the original ibus-hiragana EngineHiragana on a fake IBus (fake_gi.py)
# with generated key sequences and records the resulting document, preedit and
# candidate window after every key event. tests/engine_replay.cpp replays the
# same events with the C++ engine and compares.
#
#   python tests/golden/gen_engine_golden.py <output.txt> [scenario-count]
#
# Output format (tab separated):
#   S <name> <layout> <xkb> <surrounding 1|0> <mode> <options>
#   E <keyval> <keycode> <state>
#   R <processed> <document> <cursor> <preedit> <table visible> <candidates/...> <table cursor> <mode>

import json
import os
import random
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
UPSTREAM = os.path.join(ROOT, 'ibus-hiragana')

sys.path.insert(0, HERE)
import fake_gi  # noqa: E402,F401  (installs gi.repository)

USER_DIR = tempfile.mkdtemp(prefix='hiragana-engine-golden-')
package = types.ModuleType('package')
package.get_datadir = lambda: UPSTREAM
package.get_user_datadir = lambda: USER_DIR
package.get_name = lambda: 'ibus-hiragana'
package.get_version = lambda: 'test'
package.get_prefix = lambda: UPSTREAM
package.get_libexecdir = lambda: UPSTREAM
package.config_logging = lambda **kwargs: None
package._ = lambda text: text


def load_from_data_dirs(file):
    for d in (USER_DIR, UPSTREAM):
        path = os.path.join(d, file)
        if os.path.exists(path):
            return path
    return None


package.load_from_data_dirs = load_from_data_dirs
sys.modules['package'] = package
sys.path.insert(0, os.path.join(UPSTREAM, 'engine'))

import engine as engine_module  # noqa: E402
from gi.repository import Gio, IBus  # noqa: E402

engine_module.time = types.SimpleNamespace(sleep=lambda s: None)


class FakeLLM:
    """Deterministic stand-in for engine.llm; the same formula is implemented
    by tests/engine_replay.cpp and tests/assisted_golden_test.cpp."""

    MOD = 1000003

    @staticmethod
    def _score(prefix, word):
        h = 0
        for ch in prefix:
            h = (h * 31 + ord(ch)) % FakeLLM.MOD
        h = (h * 31) % FakeLLM.MOD  # separator
        for ch in word:
            h = (h * 31 + ord(ch)) % FakeLLM.MOD
        return h

    def _probs(self, prefix, words):
        return {i: self._score(prefix, w) / FakeLLM.MOD for i, w in enumerate(words)}

    def assist(self, prefix, yomi, words):
        return self._probs(prefix, words)

    def assist_yougen(self, prefix, yomi, words):
        return self._probs(prefix, words)

    def device_type(self):
        return 'cpu'


def _load_llm(self):
    # engine.py _load_llm() without PyTorch.
    return FakeLLM() if self._settings.get_boolean('use-llm') else None


engine_module.EngineHiragana._load_llm = _load_llm

SHIFT = IBus.ModifierType.SHIFT_MASK
CONTROL = IBus.ModifierType.CONTROL_MASK
RELEASE = IBus.ModifierType.RELEASE_MASK

# US layout: character -> (evdev keycode, shift)
US = {}
for i, (n, s) in enumerate(zip('1234567890-=', '!@#$%^&*()_+')):
    US[n] = (2 + i, False)
    US[s] = (2 + i, True)
for i, c in enumerate('qwertyuiop[]'):
    US[c] = (16 + i, False)
for i, c in enumerate('{}'):
    US[c] = (26 + i, True)
for i, c in enumerate("asdfghjkl;'`"):
    US[c] = (30 + i, False)
for i, c in enumerate(':"~'):
    US[c] = (39 + i, True)
US['\\'] = (43, False)
US['|'] = (43, True)
for i, c in enumerate('zxcvbnm,./'):
    US[c] = (44 + i, False)
for i, c in enumerate('<>?'):
    US[c] = (51 + i, True)
for c in 'abcdefghijklmnopqrstuvwxyz':
    US[c.upper()] = (US[c][0], True)
US[' '] = (57, False)


def load_layout(name):
    with open(os.path.join(UPSTREAM, 'layouts', name), encoding='utf-8') as f:
        return json.load(f)


def kana_to_romazi_table():
    roomazi = load_layout('roomazi.us.json')['Roomazi']
    table = {}
    for roman, kana in roomazi.items():
        if not roman.isalpha() or not roman.islower():
            continue
        if kana not in table or len(roman) < len(table[kana]):
            table[kana] = roman
    return table


KANA_TO_ROMAZI = kana_to_romazi_table()


def to_romazi(yomi):
    out = ''
    i = 0
    while i < len(yomi):
        if yomi[i] == 'ん':
            # 'n' becomes ん before a consonant; "n'" before a vowel, y, n or the end.
            nxt = to_romazi(yomi[i + 1:i + 2]) if i + 1 < len(yomi) else ''
            out += "n'" if not nxt or nxt[0] in "aiueoyn'" else 'n'
            i += 1
        elif yomi[i:i + 2] in KANA_TO_ROMAZI:
            out += KANA_TO_ROMAZI[yomi[i:i + 2]]
            i += 2
        elif yomi[i] in KANA_TO_ROMAZI:
            out += KANA_TO_ROMAZI[yomi[i]]
            i += 1
        else:
            return None
    return out


class Keyboard:
    """Turns characters and named keys into (keyval, keycode, state) events."""

    def __init__(self, layout_name, xkb):
        self.kana_keys = {}
        self.ascii_keys = {}
        if xkb == 'jp':
            layout = load_layout('jis.jp.json')
            for code, row in enumerate(layout['Key']):
                if row[0] and len(row[0]) == 1:
                    self.ascii_keys.setdefault(row[0], (code, False))
                if row[1] and len(row[1]) == 1:
                    self.ascii_keys.setdefault(row[1], (code, True))
        else:
            self.ascii_keys = dict(US)
        if layout_name in ('jis', 'new_stickney'):
            layout = load_layout(f'{layout_name}.{xkb}.json')
            for code, row in enumerate(layout['Key']):
                if code == 57:
                    continue
                if row[2]:
                    self.kana_keys.setdefault(row[2], (code, False))
                if row[3]:
                    self.kana_keys.setdefault(row[3], (code, True))
        self.layout = layout_name

    def keyval_for(self, code, shift):
        for ch, (c, s) in self.ascii_keys.items():
            if c == code and s == shift:
                return ord(ch)
        for ch, (c, s) in self.ascii_keys.items():
            if c == code:
                return ord(ch.upper() if shift else ch)
        return ord('?')

    def tap(self, code, shift, keyval=None):
        if keyval is None:
            keyval = self.keyval_for(code, shift)
        events = []
        if shift:
            events.append((IBus.Shift_L, 42, 0))
        st = SHIFT if shift else 0
        events.append((keyval, code, st))
        events.append((keyval, code, st | RELEASE))
        if shift:
            events.append((IBus.Shift_L, 42, SHIFT | RELEASE))
        return events

    def type_ascii(self, text):
        events = []
        for ch in text:
            if ch not in self.ascii_keys:
                continue
            code, shift = self.ascii_keys[ch]
            events += self.tap(code, shift, ord(ch))
        return events

    def type_yomi(self, yomi):
        if self.layout == 'roomazi':
            romazi = to_romazi(yomi)
            return self.type_ascii(romazi) if romazi else []
        events = []
        for ch in yomi:
            if ch in self.kana_keys:
                code, shift = self.kana_keys[ch]
                events += self.tap(code, shift)
            else:
                # Use dakuten/handakuten keys for voiced kana.
                base = {'が': 'か', 'ぎ': 'き', 'ぐ': 'く', 'げ': 'け', 'ご': 'こ', 'ざ': 'さ', 'じ': 'し', 'ず': 'す',
                        'ぜ': 'せ', 'ぞ': 'そ', 'だ': 'た', 'ぢ': 'ち', 'づ': 'つ', 'で': 'て', 'ど': 'と', 'ば': 'は',
                        'び': 'ひ', 'ぶ': 'ふ', 'べ': 'へ', 'ぼ': 'ほ'}.get(ch)
                mark = '゛'
                if base is None:
                    base = {'ぱ': 'は', 'ぴ': 'ひ', 'ぷ': 'ふ', 'ぺ': 'へ', 'ぽ': 'ほ'}.get(ch)
                    mark = '゜'
                if base and base in self.kana_keys and mark in self.kana_keys:
                    for c in (base, mark):
                        code, shift = self.kana_keys[c]
                        events += self.tap(code, shift)
        return events

    def key(self, name, shift=False, control=False):
        keyval = getattr(IBus, name)
        code = {'BackSpace': 14, 'Tab': 15, 'Return': 28, 'Escape': 1, 'Up': 103, 'Down': 108, 'Left': 105,
                'Right': 106, 'Page_Up': 104, 'Page_Down': 109, 'Henkan': 92, 'Muhenkan': 94,
                'Hiragana_Katakana': 93, 'Zenkaku_Hankaku': 41, 'space': 57}.get(name, 0)
        events = []
        if shift:
            events.append((IBus.Shift_L, 42, 0))
        if control:
            events.append((IBus.Control_L, 29, SHIFT if shift else 0))
        st = (SHIFT if shift else 0) | (CONTROL if control else 0)
        events.append((keyval, code, st))
        events.append((keyval, code, st | RELEASE))
        if control:
            events.append((IBus.Control_L, 29, st | RELEASE))
        if shift:
            events.append((IBus.Shift_L, 42, SHIFT | RELEASE))
        return events

    def space(self, shift=False):
        return self.key('space', shift)

    def shift_r_tap(self):
        return [(IBus.Shift_R, 54, 0), (IBus.Shift_R, 54, SHIFT | RELEASE)]

    def prefix_tap(self):
        return [(IBus.space, 57, 0), (IBus.space, 57, RELEASE)]


def readings(system):
    taigen, yougen = [], []
    with open(os.path.join(UPSTREAM, 'dic', system), encoding='utf-8') as f:
        for line in f:
            if line.startswith(';') or ' ' not in line:
                continue
            yomi = line.split(' ', 1)[0]
            if yomi.startswith('#') or yomi.startswith('-'):
                continue
            if yomi.endswith('―'):
                yougen.append(yomi[:-1])
            else:
                taigen.append(yomi)
    return taigen, yougen


OKURI = ['く', 'い', 'う', 'る', 'た', 'って', 'いた', 'かった', 'ない', 'ます', 'れば', 'し', 'さ', 'み', 'き']


def scenario_events(kb, rnd, taigen, yougen, steps):
    events = []
    convert = (lambda shift=False: kb.space(shift)) if kb.layout != 'new_stickney' else None
    for _ in range(steps):
        op = rnd.random()
        if op < 0.35:
            events += kb.type_yomi(rnd.choice(taigen))
            if convert:
                events += convert()
            else:
                events += kb.key('Henkan')
        elif op < 0.55:
            events += kb.type_yomi(rnd.choice(yougen))
            if convert:
                events += convert(True)
            else:
                events += kb.key('Muhenkan')
            events += kb.type_yomi(rnd.choice(OKURI))
        elif op < 0.62:
            events += kb.type_yomi(rnd.choice(taigen))
        elif op < 0.67:
            events += kb.shift_r_tap()
        elif op < 0.72:
            events += kb.key('Down')
        elif op < 0.75:
            events += kb.key('Up')
        elif op < 0.79:
            events += kb.key('Tab')
        elif op < 0.81:
            events += kb.key('Tab', shift=True)
        elif op < 0.84:
            events += kb.key('Escape')
        elif op < 0.89:
            events += kb.key('Return')
        elif op < 0.93:
            events += kb.key('BackSpace')
        elif op < 0.95:
            events += kb.type_ascii('123')
            events += kb.type_yomi('えん')
            events += convert() if convert else kb.key('Henkan')
        elif op < 0.96:
            events += kb.key('Page_Down')
        elif op < 0.97:
            events += kb.key('Muhenkan')
            events += kb.type_ascii('abc')
            events += kb.key('Henkan')
        elif op < 0.98 and kb.layout == 'new_stickney':
            events += kb.prefix_tap()
        else:
            events += kb.type_ascii(rnd.choice(['.', ',', '-', '!', '1']))
    return events


SCENARIOS = [
    # name, layout, xkb, surrounding, options
    ('roomazi', 'roomazi', 'us', True, {}),
    ('roomazi-legacy', 'roomazi', 'us', False, {}),
    ('roomazi-jp', 'roomazi', 'jp', True, {}),
    ('jis', 'jis', 'jp', True, {}),
    ('jis-legacy', 'jis', 'jp', False, {}),
    ('jis-us', 'jis', 'us', True, {}),
    ('stickney', 'new_stickney', 'us', True, {}),
    ('roomazi-digits', 'roomazi', 'us', True, {'use-half-width-digits': True, 'nn-as-jis-x-4063': True}),
    ('roomazi-llm', 'roomazi', 'us', True, {'use-llm': True}),
    ('jis-llm', 'jis', 'jp', True, {'use-llm': True}),
    ('stickney-llm', 'new_stickney', 'us', True, {'use-llm': True}),
]


def settings_for(layout, xkb, options):
    values = {
        'mode': 'あ', 'dictionary': 'restrained.9.dic', 'user-dictionary': '', 'use-llm': False,
        'use-cuda': False, 'use-half-width-digits': False, 'layout': layout, 'altgr': 'altgr',
        'permissible': False, 'nn-as-jis-x-4063': False, 'combining-circumflex': False,
        'combining-macron': False, 'logging-level': 'WARNING', 'xkb': xkb,
    }
    values.update(options)
    return values


def snapshot(e, result):
    table = e._lookup_table
    cands = '/'.join(table.candidates())
    return (f'R\t{int(bool(result))}\t{e.document}\t{e.cursor}\t{e.preedit}\t{int(bool(e.table_visible))}\t'
            f'{cands}\t{table.get_cursor_pos()}\t{e.get_mode()}\n')


def main():
    out = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    taigen, yougen = readings('restrained.9.dic')
    skipped = 0
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        for name, layout, xkb, surrounding, options in SCENARIOS:
            for n in range(count):
                rnd = random.Random(f'{name}-{n}')
                values = settings_for(layout, xkb, options)
                Gio.Settings.values = values
                e = engine_module.EngineHiragana(fake_gi._Anything(), '', None)
                if surrounding:
                    e._surrounding = engine_module.SURROUNDING_SUPPORTED
                else:
                    e.client_capabilities = IBus.Capabilite.PREEDIT_TEXT
                opts = ','.join(f'{k}={int(v)}' for k, v in sorted(options.items()))
                lines = [f'S\t{name}-{n}\t{layout}\t{xkb}\t{int(surrounding)}\tあ\t{opts}\n']
                kb = Keyboard(layout, xkb)
                try:
                    for keyval, keycode, state in scenario_events(kb, rnd, taigen, yougen, 25):
                        result = e.do_process_key_event(keyval, keycode, state)
                        if surrounding:
                            # The application reports the new surrounding text.
                            e._surrounding = engine_module.SURROUNDING_SUPPORTED
                            e._preedit_text = None
                        lines.append(f'E\t{keyval}\t{keycode}\t{state}\n')
                        lines.append(snapshot(e, result))
                except (AssertionError, ValueError, IndexError):
                    # The original engine aborts on some inputs the synthetic
                    # language model produces; they cannot be compared.
                    skipped += 1
                    continue
                f.writelines(lines)
    print(f'wrote {out} ({skipped} scenarios skipped)')


if __name__ == '__main__':
    main()
