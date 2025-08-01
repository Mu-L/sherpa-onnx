// sherpa-onnx/csrc/voice-activity-detector.cc
//
// Copyright (c)  2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/voice-activity-detector.h"

#include <algorithm>
#include <queue>
#include <utility>

#if __ANDROID_API__ >= 9
#include "android/asset_manager.h"
#include "android/asset_manager_jni.h"
#endif

#if __OHOS__
#include "rawfile/raw_file_manager.h"
#endif

#include "sherpa-onnx/csrc/circular-buffer.h"
#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/vad-model.h"

namespace sherpa_onnx {

class VoiceActivityDetector::Impl {
 public:
  explicit Impl(const VadModelConfig &config, float buffer_size_in_seconds = 60)
      : model_(VadModel::Create(config)),
        config_(config),
        buffer_(buffer_size_in_seconds * config.sample_rate) {
    Init();
  }

  template <typename Manager>
  Impl(Manager *mgr, const VadModelConfig &config,
       float buffer_size_in_seconds = 60)
      : model_(VadModel::Create(mgr, config)),
        config_(config),
        buffer_(buffer_size_in_seconds * config.sample_rate) {
    Init();
  }

  float Compute(const float *samples, int32_t n) {
    return model_->Compute(samples, n);
  }

  void AcceptWaveform(const float *samples, int32_t n) {
    if (buffer_.Size() > max_utterance_length_) {
      model_->SetMinSilenceDuration(new_min_silence_duration_s_);
      model_->SetThreshold(new_threshold_);
    } else {
      if (!config_.silero_vad.model.empty()) {
        model_->SetMinSilenceDuration(config_.silero_vad.min_silence_duration);
        model_->SetThreshold(config_.silero_vad.threshold);
      } else if (!config_.ten_vad.model.empty()) {
        model_->SetMinSilenceDuration(config_.ten_vad.min_silence_duration);
        model_->SetThreshold(config_.ten_vad.threshold);
      } else {
        SHERPA_ONNX_LOGE("Unknown vad model");
        SHERPA_ONNX_EXIT(-1);
      }
    }

    int32_t window_size = model_->WindowSize();
    int32_t window_shift = model_->WindowShift();

    // note n is usually window_size and there is no need to use
    // an extra buffer here
    last_.insert(last_.end(), samples, samples + n);

    if (last_.size() < window_size) {
      return;
    }

    // Note: For v4, window_shift == window_size
    int32_t k =
        (static_cast<int32_t>(last_.size()) - window_size) / window_shift + 1;
    const float *p = last_.data();
    bool is_speech = false;

    for (int32_t i = 0; i < k; ++i, p += window_shift) {
      buffer_.Push(p, window_shift);
      // NOTE(fangjun): Please don't use a very large n.
      bool this_window_is_speech = model_->IsSpeech(p, window_size);
      is_speech = is_speech || this_window_is_speech;
    }

    last_ = std::vector<float>(
        p, static_cast<const float *>(last_.data()) + last_.size());

    if (is_speech) {
      if (start_ == -1) {
        // beginning of speech
        start_ = std::max(buffer_.Tail() - 2 * model_->WindowSize() -
                              model_->MinSpeechDurationSamples(),
                          buffer_.Head());
        cur_segment_.start = start_;
      }
      int32_t num_samples = buffer_.Tail() - start_ - 1;
      cur_segment_.samples = buffer_.Get(start_, num_samples);
    } else {
      // non-speech

      cur_segment_.start = -1;
      cur_segment_.samples.clear();

      if (start_ != -1 && buffer_.Size()) {
        // end of speech, save the speech segment
        int32_t end = buffer_.Tail() - model_->MinSilenceDurationSamples();

        std::vector<float> s = buffer_.Get(start_, end - start_);
        SpeechSegment segment;

        segment.start = start_;
        segment.samples = std::move(s);

        segments_.push(std::move(segment));

        buffer_.Pop(end - buffer_.Head());
      }

      if (start_ == -1) {
        int32_t end = buffer_.Tail() - 2 * model_->WindowSize() -
                      model_->MinSpeechDurationSamples();
        int32_t n = std::max(0, end - buffer_.Head());
        if (n > 0) {
          buffer_.Pop(n);
        }
      }

      start_ = -1;
    }
  }

  bool Empty() const { return segments_.empty(); }

  void Pop() { segments_.pop(); }

  void Clear() { std::queue<SpeechSegment>().swap(segments_); }

  const SpeechSegment &Front() const { return segments_.front(); }

  void Reset() {
    std::queue<SpeechSegment>().swap(segments_);

    model_->Reset();
    buffer_.Reset();
    last_.clear();

    start_ = -1;

    cur_segment_.start = -1;
    cur_segment_.samples.clear();
  }

  void Flush() {
    if (start_ == -1 || buffer_.Size() == 0) {
      return;
    }

    int32_t end = buffer_.Tail();
    if (end <= start_) {
      return;
    }

    std::vector<float> s = buffer_.Get(start_, end - start_);

    SpeechSegment segment;

    segment.start = start_;
    segment.samples = std::move(s);

    segments_.push(std::move(segment));

    buffer_.Pop(end - buffer_.Head());
    start_ = -1;

    cur_segment_.start = -1;
    cur_segment_.samples.clear();
  }

  bool IsSpeechDetected() const { return start_ != -1; }

  SpeechSegment CurrentSpeechSegment() const { return cur_segment_; }

  const VadModelConfig &GetConfig() const { return config_; }

 private:
  void Init() {
    if (!config_.silero_vad.model.empty()) {
      max_utterance_length_ =
          config_.sample_rate * config_.silero_vad.max_speech_duration;
    } else if (!config_.ten_vad.model.empty()) {
      max_utterance_length_ =
          config_.sample_rate * config_.ten_vad.max_speech_duration;
    } else {
      SHERPA_ONNX_LOGE("Unsupported VAD model");
      SHERPA_ONNX_EXIT(-1);
    }
  }

 private:
  std::queue<SpeechSegment> segments_;

  // it is empty if no speech is detected
  SpeechSegment cur_segment_;

  std::unique_ptr<VadModel> model_;
  VadModelConfig config_;
  CircularBuffer buffer_;
  std::vector<float> last_;

  int max_utterance_length_ = -1;  // in samples
  float new_min_silence_duration_s_ = 0.1;
  float new_threshold_ = 0.90;

  int32_t start_ = -1;
};

VoiceActivityDetector::VoiceActivityDetector(
    const VadModelConfig &config, float buffer_size_in_seconds /*= 60*/)
    : impl_(std::make_unique<Impl>(config, buffer_size_in_seconds)) {}

template <typename Manager>
VoiceActivityDetector::VoiceActivityDetector(
    Manager *mgr, const VadModelConfig &config,
    float buffer_size_in_seconds /*= 60*/)
    : impl_(std::make_unique<Impl>(mgr, config, buffer_size_in_seconds)) {}

VoiceActivityDetector::~VoiceActivityDetector() = default;

void VoiceActivityDetector::AcceptWaveform(const float *samples, int32_t n) {
  impl_->AcceptWaveform(samples, n);
}

bool VoiceActivityDetector::Empty() const { return impl_->Empty(); }

void VoiceActivityDetector::Pop() { impl_->Pop(); }

void VoiceActivityDetector::Clear() { impl_->Clear(); }

const SpeechSegment &VoiceActivityDetector::Front() const {
  return impl_->Front();
}

void VoiceActivityDetector::Reset() const { impl_->Reset(); }

void VoiceActivityDetector::Flush() const { impl_->Flush(); }

bool VoiceActivityDetector::IsSpeechDetected() const {
  return impl_->IsSpeechDetected();
}

SpeechSegment VoiceActivityDetector::CurrentSpeechSegment() const {
  return impl_->CurrentSpeechSegment();
}

const VadModelConfig &VoiceActivityDetector::GetConfig() const {
  return impl_->GetConfig();
}

float VoiceActivityDetector::Compute(const float *samples, int32_t n) {
  return impl_->Compute(samples, n);
}

#if __ANDROID_API__ >= 9
template VoiceActivityDetector::VoiceActivityDetector(
    AAssetManager *mgr, const VadModelConfig &config,
    float buffer_size_in_seconds = 60);
#endif

#if __OHOS__
template VoiceActivityDetector::VoiceActivityDetector(
    NativeResourceManager *mgr, const VadModelConfig &config,
    float buffer_size_in_seconds = 60);
#endif

}  // namespace sherpa_onnx
