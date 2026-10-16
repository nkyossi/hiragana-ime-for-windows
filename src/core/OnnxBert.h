// Hiragana IME for Windows
//
// Copyright (c) 2026 Hiragana IME for Windows contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace hiragana {

// A thin wrapper around an ONNX Runtime session for the masked language model
// used by the LLM-assisted conversion. The model of the original (see
// ibus-hiragana/engine/llm.py) maps input_ids (int64 [batch, sequence]) to
// logits (float [batch, sequence, vocab]).
class OnnxBert {
public:
    // threads is the number of intra-op threads; with use_cuda the CUDA
    // execution provider is tried first (the CPU is used when it is missing).
    explicit OnnxBert(const std::filesystem::path& model_path, int threads = 1, bool use_cuda = false);
    ~OnnxBert();

    OnnxBert(const OnnxBert&) = delete;
    OnnxBert& operator=(const OnnxBert&) = delete;

    // Runs one sequence (batch size 1) and returns the logits for it as
    // [sequence][vocab].
    std::vector<std::vector<float>> run(const std::vector<int64_t>& input_ids) const;

    std::size_t vocab_size() const;
    // "cuda" or "cpu"
    const char* device_type() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Numerically stable softmax over a vector of logits.
std::vector<double> softmax(const std::vector<float>& logits);

}  // namespace hiragana
