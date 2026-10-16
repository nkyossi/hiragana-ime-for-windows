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

#include "OnnxBert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

#include <onnxruntime_cxx_api.h>

namespace hiragana {

namespace {

// ONNX Runtime asks that a process creates a single environment.
Ort::Env& ort_env() {
    static Ort::Env instance(ORT_LOGGING_LEVEL_WARNING, "hiragana-ime");
    return instance;
}

Ort::Session create_session(const std::filesystem::path& model_path, int threads, bool& use_cuda) {
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(threads < 1 ? 1 : threads);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (use_cuda) {
        try {
            OrtCUDAProviderOptions cuda{};
            options.AppendExecutionProvider_CUDA(cuda);
        } catch (const Ort::Exception&) {
            // The CUDA provider is not part of this ONNX Runtime; use the CPU.
            use_cuda = false;
        }
    }
    try {
        return Ort::Session(ort_env(), model_path.c_str(), options);
    } catch (const Ort::Exception& e) {
        throw std::runtime_error(std::string("could not load the ONNX model \"") +
                                 model_path.u8string() + "\": " + e.what());
    }
}

}  // namespace

struct OnnxBert::Impl {
    bool cuda;
    Ort::Session session;
    Ort::MemoryInfo memory;
    std::size_t vocab;

    Impl(const std::filesystem::path& model_path, int threads, bool use_cuda)
        : cuda(use_cuda),
          session(create_session(model_path, threads, cuda)),
          memory(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          vocab(static_cast<std::size_t>(
              session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape().back())) {}
};

OnnxBert::OnnxBert(const std::filesystem::path& model_path, int threads, bool use_cuda)
    : impl_(std::make_unique<Impl>(model_path, threads, use_cuda)) {}

OnnxBert::~OnnxBert() = default;

std::size_t OnnxBert::vocab_size() const { return impl_->vocab; }

const char* OnnxBert::device_type() const { return impl_->cuda ? "cuda" : "cpu"; }

std::vector<std::vector<float>> OnnxBert::run(const std::vector<int64_t>& input_ids) const {
    const std::array<int64_t, 2> shape{1, static_cast<int64_t>(input_ids.size())};
    auto tensor = Ort::Value::CreateTensor<int64_t>(impl_->memory,
                                                    const_cast<int64_t*>(input_ids.data()),
                                                    input_ids.size(), shape.data(), shape.size());
    const char* input_names[] = {"input_ids"};
    const char* output_names[] = {"logits"};
    auto outputs =
        impl_->session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
    const auto output_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    const std::size_t sequence = static_cast<std::size_t>(output_shape[1]);
    const std::size_t vocab = static_cast<std::size_t>(output_shape[2]);
    const float* data = outputs[0].GetTensorData<float>();
    std::vector<std::vector<float>> logits(sequence, std::vector<float>(vocab));
    for (std::size_t i = 0; i < sequence; ++i)
        std::copy(data + i * vocab, data + (i + 1) * vocab, logits[i].begin());
    return logits;
}

std::vector<double> softmax(const std::vector<float>& logits) {
    std::vector<double> probabilities(logits.size());
    if (logits.empty()) return probabilities;
    const double maximum = *std::max_element(logits.begin(), logits.end());
    double sum = 0.0;
    for (std::size_t i = 0; i < logits.size(); ++i) {
        probabilities[i] = std::exp(static_cast<double>(logits[i]) - maximum);
        sum += probabilities[i];
    }
    for (double& value : probabilities) value /= sum;
    return probabilities;
}

}  // namespace hiragana
