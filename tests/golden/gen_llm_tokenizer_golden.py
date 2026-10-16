# Generates the golden data for the C++ BERT tokenizer of the LLM backend
# (MecabTokenizer + WordPiece of BertJapaneseTokenizer).
#
#   python tests/golden/gen_llm_tokenizer_golden.py <output.txt>
#
# For each text it records the subword tokens and the input ids with the
# special tokens, as produced by tohoku-nlp/bert-base-japanese-v3.

import sys

from transformers import AutoTokenizer

MODEL_NAME = 'tohoku-nlp/bert-base-japanese-v3'

CORPUS = [
    # Ordinary sentences.
    '東北大学で研究をしています。',
    '問題のかいとう',
    'アンケートにかいとう',
    '生きがい論',
    'テスト',
    'ひらがな',
    'カタカナ',
    '漢字',
    '日本語の文章を入力します。',
    'きょうはいいてんきですね。',
    'これはふりがなパッドです',
    'きんきょうほうこく',
    # Half-width and full-width forms (NFKC).
    'ﾃｽﾄ ＡＢＣ',
    'ｶﾞｷﾞｸﾞｹﾞｺﾞ',
    'ＡＢＣＤＥＦＧ',
    '１２３４５６７８９０',
    '３丁目',
    '50%',
    # Compatibility characters.
    '①㍿Ⅻﬀ',
    '㌔㍍',
    '（１）「２」',
    # Mixed scripts.
    'abcあいうABC漢字',
    'Windowsで日本語',
    'メーカー型番XYZ-123',
    # Punctuation and symbols.
    '。、・ー〜',
    '「こんにちは」',
    'テスト。です',
    '、',
    '。',
    '  ',
    'スペース 入り',
    # Conjugated forms (used by assist_yougen).
    '―',
    'する―',
    'ない―',
    '読ま―',
    'し―',
    'き―',
    'て―',
    'た―',
    'まし―',
    'ませ―',
    'れ―',
    # Repeated and long tokens.
    'あ' * 40,
    '漢' * 40,
    'あ' * 120,
    'テスト' * 30,
    # Empty and whitespace only.
    '',
    ' ',
    '\t',
    # Added (special) tokens: PreTrainedTokenizer.tokenize() keeps these whole.
    '[UNK]',
    '言[UNK]',
    'あ[UNK]い',
    'テスト[UNK]',
    'ﾃｽﾄ[UNK]ＡＢＣ',
    '[CLS]テスト[SEP]',
    'テスト[MASK]です',
    '[UNK][UNK]',
    'a[UNK]b[UNK]c',
    '[PAD]',
]


def main():
    out = sys.argv[1]
    tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME, clean_up_tokenization_spaces=True)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# llm tokenizer golden\n')
        for text in CORPUS:
            # Escape so that a line always has exactly three tab separated fields.
            escaped = text.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n')
            tokens = tokenizer.tokenize(text)
            ids = tokenizer(text)['input_ids']
            f.write('T\t' + escaped + '\t' + '\u001f'.join(tokens) + '\t' +
                    ' '.join(str(i) for i in ids) + '\n')

        # padding=True of a batch (add_special_tokens=True).
        batches = [
            ['アンケートにかいとう', '問題のかいとう', 'テスト'],
            ['東北大学で研究をしています。', 'ひらがな'],
            ['', 'テスト', '漢字', 'あ'],
        ]
        for batch in batches:
            encoded = tokenizer(batch, padding=True)
            rows = []
            for ids in encoded.input_ids:
                rows.append(' '.join(str(i) for i in ids))
            f.write('B\t' + '\t'.join(batch) + '\t' + '\u001f'.join(rows) + '\n')
    print(f'wrote {out}')


if __name__ == '__main__':
    main()
