#pragma once
// extern pkg
#include "voice_toolbox/asr/asr-engine-impl.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"
#include <mutex>

namespace voice_toolbox
{

    class SenseVoiceOffline : public ASREngineImpl<sherpa_onnx::cxx::Wave, std::shared_ptr<sherpa_onnx::cxx::OfflineRecognizerResult> >
    {
    public:
        SenseVoiceOffline(const sherpa_onnx::cxx::OfflineRecognizerConfig &config)
        {
            asr_config_ = config;
        }
        ~SenseVoiceOffline();
        bool InitializeASREngine() override
        {
            auto temp_recognizer = sherpa_onnx::cxx::OfflineRecognizer::Create(asr_config_);
            if (!temp_recognizer.Get())
                return false;
            offline_recognizer_ = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(std::move(temp_recognizer));
            return true;
        }
        std::shared_ptr<sherpa_onnx::cxx::OfflineRecognizerResult>  SpeechRecogize(const sherpa_onnx::cxx::Wave &wave) override
        {
            std::lock_guard<std::mutex> lock(recognizer_mutex_);
            sherpa_onnx::cxx::OfflineStream stream = offline_recognizer_->CreateStream();
            stream.AcceptWaveform(wave.sample_rate, wave.samples.data(), wave.samples.size());
            offline_recognizer_->Decode(&stream);
            return offline_recognizer_->GetResultPtr(&stream);
        }

        bool SetParameter(const std::string &key, const std::string &value) override;
        std::string GetParameter(const std::string &key) override;

    private:
        sherpa_onnx::cxx::OfflineRecognizerConfig asr_config_;
        std::unique_ptr<sherpa_onnx::cxx::OfflineRecognizer> offline_recognizer_;
        std::mutex recognizer_mutex_; 
    };

} // namespace voice_toolbox
