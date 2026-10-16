# A stand-in for the parts of PyGObject (IBus, Gio, Gdk, ...) that
# ibus-hiragana/engine/engine.py uses, so that the original engine can run
# without IBus. The fake IBus.Engine edits a plain string document.

import sys
import types


def _module(name):
    m = types.ModuleType(name)
    sys.modules[name] = m
    return m


gi = _module('gi')
gi.require_version = lambda *args: None
repository = _module('gi.repository')
gi.repository = repository

IBus = _module('gi.repository.IBus')
repository.IBus = IBus

KEYSYMS = {
    'VoidSymbol': 0xffffff, 'space': 0x20, 'exclam': 0x21, 'backslash': 0x5c, 'asciitilde': 0x7e,
    'yen': 0xa5, 'periodcentered': 0xb7,
    'ISO_Level3_Shift': 0xfe03, 'ISO_Left_Tab': 0xfe20,
    'BackSpace': 0xff08, 'Tab': 0xff09, 'Return': 0xff0d, 'Escape': 0xff1b, 'Delete': 0xffff,
    'Muhenkan': 0xff22, 'Henkan': 0xff23, 'Hiragana_Katakana': 0xff27, 'Zenkaku_Hankaku': 0xff2a,
    'Eisu_toggle': 0xff30, 'Hangul': 0xff31, 'Hangul_Hanja': 0xff34,
    'Home': 0xff50, 'Left': 0xff51, 'Up': 0xff52, 'Right': 0xff53, 'Down': 0xff54,
    'Page_Up': 0xff55, 'Page_Down': 0xff56, 'End': 0xff57, 'KP_Page_Up': 0xff9a, 'KP_Page_Down': 0xff9b,
    'Shift_L': 0xffe1, 'Shift_R': 0xffe2, 'Control_L': 0xffe3, 'Control_R': 0xffe4, 'Caps_Lock': 0xffe5,
    'Meta_L': 0xffe7, 'Meta_R': 0xffe8, 'Alt_L': 0xffe9, 'Alt_R': 0xffea, 'Super_L': 0xffeb,
}
ASCII_NAMES = {
    'quotedbl': 0x22, 'numbersign': 0x23, 'dollar': 0x24, 'percent': 0x25, 'ampersand': 0x26,
    'apostrophe': 0x27, 'parenleft': 0x28, 'parenright': 0x29, 'asterisk': 0x2a, 'plus': 0x2b,
    'comma': 0x2c, 'minus': 0x2d, 'period': 0x2e, 'slash': 0x2f, 'colon': 0x3a, 'semicolon': 0x3b,
    'less': 0x3c, 'equal': 0x3d, 'greater': 0x3e, 'question': 0x3f, 'at': 0x40, 'bracketleft': 0x5b,
    'bracketright': 0x5d, 'asciicircum': 0x5e, 'underscore': 0x5f, 'grave': 0x60, 'braceleft': 0x7b,
    'bar': 0x7c, 'braceright': 0x7d,
}
for _name, _value in {**KEYSYMS, **ASCII_NAMES}.items():
    setattr(IBus, _name, _value)


def keyval_from_name(name):
    if name in KEYSYMS:
        return KEYSYMS[name]
    if name in ASCII_NAMES:
        return ASCII_NAMES[name]
    if len(name) == 1:
        return ord(name)
    return KEYSYMS['VoidSymbol']


IBus.keyval_from_name = keyval_from_name
IBus.keyval_name = lambda keyval: hex(keyval)


class ModifierType:
    SHIFT_MASK = 1 << 0
    LOCK_MASK = 1 << 1
    CONTROL_MASK = 1 << 2
    MOD1_MASK = 1 << 3
    MOD4_MASK = 1 << 6
    MOD5_MASK = 1 << 7
    RELEASE_MASK = 1 << 30


IBus.ModifierType = ModifierType


class Capabilite:
    PREEDIT_TEXT = 1
    SURROUNDING_TEXT = 1 << 5


IBus.Capabilite = Capabilite


class _Anything:
    def __init__(self, *args, **kwargs):
        pass

    def __getattr__(self, name):
        return _Anything()

    def __call__(self, *args, **kwargs):
        return _Anything()


class Text:
    def __init__(self, text=''):
        self._text = text

    @staticmethod
    def new_from_string(text):
        return Text(text)

    def get_text(self):
        return self._text

    def set_attributes(self, attrs):
        pass


IBus.Text = Text
IBus.AttrList = _Anything
IBus.Attribute = _Anything()
IBus.AttrType = _Anything()
IBus.AttrUnderline = _Anything()
IBus.Orientation = _Anything()
IBus.PropType = _Anything()
IBus.PropState = _Anything()
IBus.Property = _Anything
IBus.PropList = _Anything
IBus.Bus = _Anything


class LookupTable:
    """ibus/src/ibuslookuptable.c with round=False"""

    def __init__(self, page_size, cursor_pos, cursor_visible, round):
        self._page_size = page_size
        self._cursor_pos = cursor_pos
        self._candidates = []

    @staticmethod
    def new(page_size, cursor_pos, cursor_visible, round):
        return LookupTable(page_size, cursor_pos, cursor_visible, round)

    def set_orientation(self, o):
        pass

    def clear(self):
        self._candidates = []
        self._cursor_pos = 0

    def append_candidate(self, text):
        self._candidates.append(text.get_text())

    def set_label(self, i, text):
        pass

    def get_number_of_candidates(self):
        return len(self._candidates)

    def candidates(self):
        return self._candidates

    def get_cursor_pos(self):
        return self._cursor_pos

    def set_cursor_pos(self, pos):
        self._cursor_pos = pos if pos < len(self._candidates) else 0

    def get_page_size(self):
        return self._page_size

    def cursor_up(self):
        if self._cursor_pos == 0:
            return False
        self._cursor_pos -= 1
        return True

    def cursor_down(self):
        if not self._candidates or self._cursor_pos + 1 >= len(self._candidates):
            return False
        self._cursor_pos += 1
        return True

    def page_up(self):
        if self._cursor_pos < self._page_size:
            return False
        self._cursor_pos -= self._page_size
        return True

    def page_down(self):
        if not self._candidates:
            return False
        page = self._cursor_pos // self._page_size
        pages = (len(self._candidates) + self._page_size - 1) // self._page_size
        if page + 1 >= pages:
            return False
        self._cursor_pos += self._page_size
        if self._cursor_pos >= len(self._candidates):
            self._cursor_pos = len(self._candidates) - 1
        return True


IBus.LookupTable = LookupTable


class Engine:
    """Fake IBus.Engine that edits self.document at self.cursor."""

    class props:
        pass

    def __init__(self, **kwargs):
        self.document = ''
        self.cursor = 0
        self.preedit = ''
        self.table_visible = False
        self.table = None
        self.client_capabilities = Capabilite.PREEDIT_TEXT | Capabilite.SURROUNDING_TEXT

    def get_surrounding_text(self):
        return Text(self.document), self.cursor, self.cursor

    def delete_surrounding_text(self, offset, nchars):
        start = self.cursor + offset
        self.document = self.document[:start] + self.document[start + nchars:]
        self.cursor = start

    def commit_text(self, text):
        t = text.get_text()
        self.document = self.document[:self.cursor] + t + self.document[self.cursor:]
        self.cursor += len(t)

    def update_preedit_text(self, text, cursor, visible):
        self.preedit = text.get_text() if visible else ''

    def update_lookup_table(self, table, visible):
        self.table = table
        self.table_visible = visible

    def forward_key_event(self, *args):
        raise AssertionError('forward_key_event is not expected')

    def register_properties(self, props):
        pass

    def update_property(self, prop):
        pass

    def do_set_surrounding_text(self, text, cursor_pos, anchor_pos):
        pass

    def do_set_cursor_location(self, *args):
        pass


IBus.Engine = Engine


class _Settings:
    values = {}

    def __init__(self, schema):
        self.schema = schema

    @staticmethod
    def new(schema):
        return _Settings(schema)

    def get_string(self, key):
        return _Settings.values[key]

    def get_boolean(self, key):
        return _Settings.values[key]

    def set_string(self, key, value):
        _Settings.values[key] = value

    def reset(self, key):
        pass

    def connect(self, *args):
        return 1

    def disconnect(self, handler):
        pass

    def get_value(self, key):
        xkb = _Settings.values.get('xkb', 'us')
        return _Variant([_Variant([_Variant('xkb'), _Variant(xkb)])])


class _Variant:
    def __init__(self, value):
        self.value = value

    def n_children(self):
        return len(self.value)

    def get_child_value(self, i):
        return self.value[i]

    def get_string(self):
        return self.value


Gio = _module('gi.repository.Gio')
repository.Gio = Gio
Gio.Settings = _Settings

for _name in ('Gdk', 'Gtk', 'GnomeDesktop', 'Notify', 'GLib'):
    _m = _module('gi.repository.' + _name)
    setattr(repository, _name, _m)

repository.Gdk.Display = _Anything()
repository.Gdk.Keymap = _Anything()
repository.Notify.Notification = _Anything()
