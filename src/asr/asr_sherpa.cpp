#include "voice_toolbox/asr/asr_sherpa.hpp"

namespace voice_toolbox
{

    Sherpa_onnx_ASRSerive::Sherpa_onnx_ASRSerive(const rclcpp::NodeOptions &options)
        : ASR_Base("Sherpa_onnx_ASR", "asr_service", options)
    {
        this->declare_parameter<std::string>("config_file", "/home/kaiser/WORK_SPACE-2/voice_toolbox_ws/src/voice_toolbox/config/voice_toolbox_setting.yaml");
    }

    CallbackReturn Sherpa_onnx_ASRSerive::on_configure(const rclcpp_lifecycle::State &)
    {
        std::string config_file = this->get_parameter("config_file").as_string();
        if (!std::filesystem::exists(config_file))
        {
            RCLCPP_ERROR(this->get_logger(), "Config file %s does not exist", config_file.c_str());
            return CallbackReturn::ERROR;
        }
        YAML::Node config = YAML::LoadFile(config_file);

        // 检查asr_sherpa_onnx部分是否存在
        if (!config["asr_sherpa_onnx"])
        {
            RCLCPP_ERROR(this->get_logger(), "Config file does not contain 'asr_sherpa_onnx' section");
            return CallbackReturn::ERROR;
        }

        auto asr_node = config["asr_sherpa_onnx"];
        asr_config_.model_config.sense_voice.model = asr_node["model_path"] ? asr_node["model_path"].as<std::string>() : "default_model_path";

        asr_config_.model_config.sense_voice.use_itn = asr_node["use_itn"] ? asr_node["use_itn"].as<bool>() : true;

        asr_config_.model_config.sense_voice.language = asr_node["language"] ? asr_node["language"].as<std::string>() : "auto";

        asr_config_.model_config.tokens = asr_node["tokens"] ? asr_node["tokens"].as<std::string>() : "default_tokens_path";

        asr_config_.model_config.num_threads = asr_node["num_threads"] ? asr_node["num_threads"].as<int>() : 1;

        asr_config_.model_config.debug = asr_node["debug"] ? asr_node["debug"].as<bool>() : false;

        // 检查websocket部分是否存在
        if (!config["websocket"])
        {
            RCLCPP_WARN(this->get_logger(), "Config file does not contain 'websocket' section, using default values");
        }
        else
        {
            auto ws_node = config["websocket"];
            websocket_config_.port = ws_node["port"] ? ws_node["port"].as<int>() : 8000;

            websocket_config_.log_level = ws_node["log_level"] ? ws_node["log_level"].as<std::string>() : "INFO";

            websocket_config_.max_connections = ws_node["max_connections"] ? ws_node["max_connections"].as<int>() : 100;

            websocket_config_.connection_timeout = ws_node["connection_timeout"] ? ws_node["connection_timeout"].as<int>() : 300;
        }

        return CallbackReturn::SUCCESS;
    }

    /**
     * @brief 激活ASR服务，并创建ASR引擎实例
     * @param previous_state 上一个状态
     * @return CallbackReturn 激活结果
     */
    CallbackReturn Sherpa_onnx_ASRSerive::on_activate(const rclcpp_lifecycle::State &state)
    {
        // 先调用基类的on_activate方法来创建服务
        auto base_state = ASR_Base<voice_toolbox::srv::OneShot>::on_activate(state);
        if (base_state != CallbackReturn::SUCCESS) {
            return base_state;
        }

        auto temp_recognizer = sherpa_onnx::cxx::OfflineRecognizer::Create(asr_config_);
        if (!temp_recognizer.Get())
        {
            RCLCPP_ERROR(get_logger(), "ASR engine initialization failed. Please check the ASR configuration");
            return CallbackReturn::ERROR;
        }

        recognizer_ = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(std::move(temp_recognizer));
        return CallbackReturn::SUCCESS;
    }

    /**
     * @brief 处理识别请求的完整流程，包括日志记录
     * @param request 服务请求对象
     * @param response 服务响应对象
     */
    void Sherpa_onnx_ASRSerive::handle_service_request(const std::shared_ptr<typename voice_toolbox::srv::OneShot::Request> request,
                                                   std::shared_ptr<typename voice_toolbox::srv::OneShot::Response> response)
    {
        sherpa_onnx::cxx::Wave wave = sherpa_onnx::cxx::ReadWave(request->audio_path);
        if (wave.samples.empty())
        {
            RCLCPP_ERROR(this->get_logger(), "failed to read wave file: %s", request->audio_path.c_str());
            response->result_text = "";
            response->success = false;
            response->message = "failed to read wave file:: " + request->audio_path;
            return;
        }
        
        sherpa_onnx::cxx::OfflineStream stream = recognizer_->CreateStream();
        stream.AcceptWaveform(wave.sample_rate, wave.samples.data(), wave.samples.size());

        recognizer_->Decode(&stream);

        sherpa_onnx::cxx::OfflineRecognizerResult result = recognizer_->GetResult(&stream);
        response->result_text = result.text;
        response->success = true;
        response->message = "";
    }

} // namespace voice_toolbox

RCLCPP_COMPONENTS_REGISTER_NODE(voice_toolbox::Sherpa_onnx_ASRSerive);