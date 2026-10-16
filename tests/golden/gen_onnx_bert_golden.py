# Generates the golden data for tests/onnx_bert_test.cpp: it runs the exported
# ONNX model with the reference onnxruntime (Python) and records the softmax at
# the last position of a few input sequences.
#
#   python tests/golden/gen_onnx_bert_golden.py <output.txt> <model.onnx>
#
# The C++ test loads the same model through the ONNX Runtime C++ API and must
# reproduce the same values.

import sys

import numpy as np
import onnxruntime as ort

# Sequences produced by the tokenizer of tohoku-nlp/bert-base-japanese-v3
# (see gen_llm_tokenizer_golden.py), [CLS] and [SEP] included.
CASES = [
    [2, 13968, 12533, 457, 12605, 500, 441, 456, 422, 12995, 385, 3],
    [2, 14985, 3],
    [2, 19778, 461, 17801, 14694, 3],
    [2, 12829, 464, 17801, 14694, 3],
    [2, 13630, 18536, 5654, 3],
]


def main():
    out = sys.argv[1]
    model = sys.argv[2]
    session = ort.InferenceSession(model, providers=['CPUExecutionProvider'])
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# onnx bert golden: input_ids -> softmax at the last position\n')
        for ids in CASES:
            logits = session.run(None, {'input_ids': np.array([ids], dtype=np.int64)})[0][0]
            last = logits[-1].astype(np.float64)
            e = np.exp(last - last.max())
            p = e / e.sum()
            f.write('CASE\t' + ' '.join(str(i) for i in ids) + '\n')
            f.write('SOFTMAX\t' + ' '.join(repr(float(v)) for v in p) + '\n')
    print(f'wrote {out}')


if __name__ == '__main__':
    main()
