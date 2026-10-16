# Exports cl-tohoku/bert-base-japanese-v3 (AutoModelForMaskedLM) to ONNX for the
# LLM-assisted conversion of the Windows port, and saves the tokenizer files.
#
#   python tools/export-llm-model.py <output-dir>
#
# Requires: torch, transformers (and fugashi + unidic-lite for the tokenizer).
# The model itself is downloaded from the Hugging Face Hub on the first run.
#
# Writes to <output-dir>:
#   model.onnx        the masked language model (dynamic batch/sequence)
#   vocab.txt         the WordPiece vocabulary
#   tokenizer.json    the tokenizer configuration (for reference)
#   special_tokens_map.json

import os
import sys

import numpy as np
import torch
from transformers import AutoModelForMaskedLM, AutoTokenizer

MODEL_NAME = 'tohoku-nlp/bert-base-japanese-v3'


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    onnx_path = os.path.join(out_dir, 'model.onnx')

    print(f'loading {MODEL_NAME} ...')
    tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME, clean_up_tokenization_spaces=True)
    model = AutoModelForMaskedLM.from_pretrained(MODEL_NAME)
    model.eval()

    ids = tokenizer('テスト', return_tensors='pt')['input_ids']
    with torch.no_grad():
        logits = model(ids).logits.numpy()
    print('torch logits', logits.shape)

    print(f'exporting {onnx_path} ...')
    torch.onnx.export(
        model, (ids,), onnx_path,
        input_names=['input_ids'], output_names=['logits'],
        dynamic_axes={'input_ids': {0: 'batch', 1: 'sequence'},
                      'logits': {0: 'batch', 1: 'sequence'}},
        opset_version=17, do_constant_folding=True)
    print('size', os.path.getsize(onnx_path))

    import onnxruntime as ort
    session = ort.InferenceSession(onnx_path, providers=['CPUExecutionProvider'])
    got = session.run(None, {'input_ids': ids.numpy()})[0]
    print('onnx logits', got.shape)
    print('max abs diff', float(np.max(np.abs(got - logits))))

    tokenizer.save_pretrained(out_dir)
    print('saved tokenizer files to', out_dir)


if __name__ == '__main__':
    main()
