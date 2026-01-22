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

        bool InitializeASREngine()
        {
            auto temp_recognizer = sherpa_onnx::cxx::OfflineRecognizer::Create(asr_config_);
            if (!temp_recognizer.Get())
                return false;
            offline_recognizer_ = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(std::move(temp_recognizer));
            return true;
        }

        std::shared_ptr<sherpa_onnx::cxx::OfflineRecognizerResult> SpeechRecogize(const std::vector<uint8_t> &audio_data, int32_t sample_rate)
        {
            if (!offline_recognizer_ || audio_data.empty())
            {
                return nullptr;
            }

            // 将 uint8_t 数据转换为 float 样本
            std::vector<float> float_samples;
            if (audio_data.size() % 2 != 0)
            {
                // 如果数据长度不是偶数，添加一个零字节
                std::vector<uint8_t> padded_data = audio_data;
                padded_data.push_back(0);
                float_samples = ConvertPcmToFloat(padded_data);
            }
            else
            {
                float_samples = ConvertPcmToFloat(audio_data);
            }

            if (float_samples.empty())
            {
                return nullptr;
            }

            sherpa_onnx::cxx::Wave wave;
            wave.samples = std::move(float_samples);
            wave.sample_rate = sample_rate;

            // 使用互斥锁保护对共享资源的访问
            std::lock_guard<std::mutex> lock(recognizer_mutex_);
            sherpa_onnx::cxx::OfflineStream stream = offline_recognizer_->CreateStream();
            stream.AcceptWaveform(wave.sample_rate, wave.samples.data(), wave.samples.size());
            offline_recognizer_->Decode(&stream);
            return offline_recognizer_->GetResultPtr(&stream);
        }

        bool SetParameter(const std::string &key, const std::string &value)
        {
            // 目前返回false表示不支持，未来可以实现参数设置功能
            (void)key;
            (void)value;
            return false;
        }

        std::string GetParameter(const std::string &key)
        {
            // 目前返回空字符串表示不支持，未来可以实现参数获取功能
            (void)key;
            return "";
        }

    private:
        std::vector<float> ConvertPcmToFloat(const std::vector<uint8_t> &audio_data)
        {
            std::vector<float> float_samples;
            float_samples.reserve(audio_data.size() / 2);

            for (size_t i = 0; i < audio_data.size(); i += 2)
            {
                // 将两个字节的 PCM 数据转换为 int16_t，然后归一化为 float
                // 注意：这里假设是小端序的16位PCM数据
                int16_t sample = static_cast<int16_t>(audio_data[i] | (audio_data[i + 1] << 8));
                float normalized_sample = static_cast<float>(sample) / 32768.0f;
                float_samples.push_back(normalized_sample);
            }

            return float_samples;
        }

        sherpa_onnx::cxx::OfflineRecognizerConfig asr_config_;
        std::unique_ptr<sherpa_onnx::cxx::OfflineRecognizer> offline_recognizer_;
        std::mutex recognizer_mutex_;
    };
} // namespace asr_engine