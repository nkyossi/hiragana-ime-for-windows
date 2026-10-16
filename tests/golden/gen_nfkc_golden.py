# Generates the golden data for tests/nfkc_test.cpp: it records
# unicodedata.normalize('NFKC', text) for a corpus of Japanese and
# compatibility characters.
#
#   python tests/golden/gen_nfkc_golden.py <output.txt>
#
# Each line is "<input code points>\t<output code points>" in hexadecimal so
# that control characters and tabs can be represented.

import sys
import unicodedata

CORPUS = [
    '',
    '東北大学で研究をしています。',
    'アンケートにかいとう',
    'テスト',
    'ひらがな',
    'カタカナ',
    '問題のかいとう',
    '生きがい論',
    '日本語の文章を入力します。',
    'きょうはいいてんきですね。',
    # Half-width katakana (with and without voiced marks).
    'ﾃｽﾄ',
    'ｶﾞｷﾞｸﾞｹﾞｺﾞ',
    'ﾊﾟﾋﾟﾌﾟﾍﾟﾎﾟ',
    'ﾞ',
    'ﾟ',
    # Full-width ASCII and digits.
    'ＡＢＣ',
    'ａｂｃ',
    '１２３',
    '！＃％＆',
    '　',
    'ＡＢＣ　ＤＥＦ',
    # Compatibility characters.
    '①',
    '㍿',
    'Ⅻ',
    'ﬀ',
    '㌔',
    '㍍',
    '㌫',
    '㍑',
    '㎡',
    '№',
    '℡',
    '㌀',
    # Symbols and punctuation.
    '。、・ー〜',
    '「こんにちは」',
    '〜',
    '～',
    '゛',
    '゜',
    # Spaces and controls.
    ' ',
    '\t',
    '\n',
    '\r',
    'a\tb\nc',
    '\u00a0',
    '\u3000',
    '\u2003',
    '\u2028',
    # Mixed.
    'ﾃｽﾄ ＡＢＣ漢字',
    'abcあいうABC漢字',
    'Windowsで日本語',
    'メーカー型番XYZ-123',
    '３丁目',
    '50%',
    '（１）「２」',
    'ﾞﾟ',
    'ｳﾞｧｲｵﾘﾝ',
    'テスト。です',
]


def hexes(s):
    return ' '.join(f'{ord(c):X}' for c in s)


def main():
    out = sys.argv[1]
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# nfkc golden\n')
        for text in CORPUS:
            f.write(hexes(text) + '\t' + hexes(unicodedata.normalize('NFKC', text)) + '\n')
    print(f'wrote {out}')


if __name__ == '__main__':
    main()
