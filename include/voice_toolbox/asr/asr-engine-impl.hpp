#pragma once

#include <string>
#include <memory>
#include "sherpa-onnx/c-api/cxx-api.h"
#include "sherpa-onnx/c-api/c-api.h"
namespace voice_toolbox
{

    class ASREngineImpl
    {
    public:
        using Ptr = std::shared_ptr<ASREngineImpl>;
        using ConstPtr = std::shared_ptr<const ASREngineImpl>;

        virtual ~ASREngineImpl() = default;

        /**
         * @brief The main method for performing speech recognition
         * @param audio The input audio data, which can be a file path or an audio array
         * @return The string representing the recognition result
         */
        virtual sherpa_onnx::cxx::OfflineRecognizerResult SpeechRecogize(const sherpa_onnx::cxx::Wave &wave) = 0;

        /**
         * @brief 初始化识别引擎
         * @return 成功返回true，否则返回false
         */
        virtual bool InitializeASREngine() = 0;

        /**
         * @brief 设置识别参数
         * @param key 参数键名
         * @param value 参数值
         * @return 成功返回true，否则返回false
         */
        virtual bool SetParameter(const std::string &key, const std::string &value){}

        /**
         * @brief 获取识别参数
         * @param key 参数键名
         * @return 参数值，如果不存在则返回空字符串
         */
        virtual std::string GetParameter(const std::string &key){}

        /**
         * @brief 开始流式识别
         * @return 成功返回true，否则返回false
         */
        virtual bool StartStreaming() { return false; }


        /**
         * @brief 结束流式识别并获取结果
         * @return 识别结果
         */
        virtual std::string FinishStream() { return ""; }

        /**
         * @brief 获取引擎名称
         * @return 引擎名称
         */
        virtual std::string GetModelName() { return model_name_; }

    protected:
        ASREngineImpl() = default;

        std::string model_name_;
    };

} // namespace voice_toolbox