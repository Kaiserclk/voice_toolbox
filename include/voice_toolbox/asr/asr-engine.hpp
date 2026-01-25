#pragma once

#include "sherpa-onnx/c-api/cxx-api.h"
#include <mutex>
#include <vector>
#include <memory>

namespace asr_engine
{
    class SenseVoiceOffline
    {
    public:
        SenseVoiceOffline(const sherpa_onnx::cxx::OfflineRecognizerConfig &config)
        {
            asr_config_ = config;
        }


        /**
         * @brief Initialize the ASR engine
         * @return true if initialization is successful, false otherwise
         */
        bool InitializeASREngine()
        {
            auto temp_recognizer = sherpa_onnx::cxx::OfflineRecognizer::Create(asr_config_);
            if (!temp_recognizer.Get())
                return false;
            offline_recognizer_ = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(std::move(temp_recognizer));
            return true;
        }


        /**
         * @brief Generic A speech recognition method that returns multiple recognition labels
         * @param audio_data   audio data
         * @param sample_rate  sample rate
         * @return             recognition result
         */
        std::shared_ptr<sherpa_onnx::cxx::OfflineRecognizerResult> SpeechRecogize(const std::vector<int16_t> &audio_data, int32_t sample_rate)
        {
            if (!offline_recognizer_ || audio_data.empty())
            {
                return nullptr;
            }

            std::vector<float> float_samples;
            float_samples.reserve(audio_data.size());

            for (size_t i = 0; i < audio_data.size(); ++i)
            {
                float normalized_sample = static_cast<float>(audio_data[i]) / 32768.0f;
                float_samples.push_back(normalized_sample);
            }

            sherpa_onnx::cxx::Wave wave;
            wave.samples = std::move(float_samples);
            wave.sample_rate = sample_rate;

            std::lock_guard<std::mutex> lock(recognizer_mutex_);
            sherpa_onnx::cxx::OfflineStream stream = offline_recognizer_->CreateStream();
            stream.AcceptWaveform(wave.sample_rate, wave.samples.data(), wave.samples.size());
            offline_recognizer_->Decode(&stream);
            return offline_recognizer_->GetResultPtr(&stream);
        }
    private:
        sherpa_onnx::cxx::OfflineRecognizerConfig asr_config_;
        std::unique_ptr<sherpa_onnx::cxx::OfflineRecognizer> offline_recognizer_;
        std::mutex recognizer_mutex_;
    };



    
} // namespace asr_engine