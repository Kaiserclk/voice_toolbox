#include "voice_toolbox/asr/simple_asr.hpp"
#include <asio.hpp>

namespace voice_toolbox
{
    Simple_ASRService::Simple_ASRService(const rclcpp::NodeOptions &options)
        : ASR_Service("OfflineASR", "asr_service", options)
    {
        this->declare_parameter<std::string>("config_file", "/home/kaiser/WORK_SPACE-2/voice_toolbox_ws/src/voice_toolbox/config/voice_toolbox_setting.yaml");
        this->declare_parameter<bool>("debug", true);
    }

    Simple_ASRService::~Simple_ASRService()
    {
    }

    CallbackReturn Simple_ASRService::on_configure(const rclcpp_lifecycle::State &)
    {
        // get parameter
        std::string config_file = this->get_parameter("config_file").as_string();
        debug_ = this->get_parameter("debug").as_bool();

        if (!std::filesystem::exists(config_file))
        {
            RCLCPP_ERROR(this->get_logger(), "Config file %s does not exist", config_file.c_str());
            return CallbackReturn::ERROR;
        }
        YAML::Node config = YAML::LoadFile(config_file);

        if (!config["simple_asr"])
        {
            RCLCPP_ERROR(this->get_logger(), "Config file does not contain 'simple_asr' section");
            return CallbackReturn::ERROR;
        }

        auto asr_node = config["simple_asr"];
        asr_config_.model_config.sense_voice.model = asr_node["model_path"] ? asr_node["model_path"].as<std::string>() : "default_model_path";

        asr_config_.model_config.sense_voice.use_itn = asr_node["use_itn"] ? asr_node["use_itn"].as<bool>() : true;

        asr_config_.model_config.sense_voice.language = asr_node["language"] ? asr_node["language"].as<std::string>() : "auto";

        asr_config_.model_config.tokens = asr_node["tokens"] ? asr_node["tokens"].as<std::string>() : "default_tokens_path";

        asr_config_.model_config.num_threads = asr_node["num_threads"] ? asr_node["num_threads"].as<int>() : 1;

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
        }
        return CallbackReturn::SUCCESS;
    }

    /**
     * @brief Activate the ASR service and create the ASR engine instance and WebSocket server
     * @param state Previous state
     * @return CallbackReturn Activation result
     */
    CallbackReturn Simple_ASRService::on_activate(const rclcpp_lifecycle::State &state)
    {
        // initialize ROS2 service
        auto base_state = ASR_Service<voice_toolbox::srv::OneShot>::on_activate(state);
        if (base_state != CallbackReturn::SUCCESS)
        {
            return base_state;
        }
        // initialize ASR engine
        sensevoice_ = std::make_unique<asr_engine::SenseVoiceOffline>(asr_config_);
        if (!sensevoice_->InitializeASREngine())
        {
            RCLCPP_ERROR(get_logger(), "SenseVoice ASR engine initialization failed. Please check the configuration");
            return CallbackReturn::ERROR;
        }

        // initialize WebSocket server
        websocket_ptr_ = std::make_unique<websocket_asr::WebsocketOfflineASR>(websocket_config_);
        if (!websocket_ptr_->Initialize())
        {
            RCLCPP_ERROR(get_logger(), "Websocket initialization failed. Please check the configuration");
            return CallbackReturn::ERROR;
        }

        return CallbackReturn::SUCCESS;
    }

    /**
     * @brief Handles local file speech recognition requests by ros service
     * @param request Service request object
     * @param response Service response object
     */
    void Simple_ASRService::handle_service_request(const std::shared_ptr<typename voice_toolbox::srv::OneShot::Request> request,
                                                   std::shared_ptr<typename voice_toolbox::srv::OneShot::Response> response)
    {
        // 添加计时功能
        auto start_time = std::chrono::high_resolution_clock::now();

        std::shared_ptr<sherpa_onnx::cxx::OfflineRecognizerResult> result_ptr = sensevoice_->SpeechRecogize(request->audio_data, request->sample_rate);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (debug_) {
            RCLCPP_INFO(get_logger(), "Speech recognition took %ld ms", duration.count());
        }

        if (result_ptr && !result_ptr->text.empty())
        {
            response->result_text = result_ptr->text;
            response->success = true;
            response->message = "Recognition successful";
        }
        else
        {
            response->result_text = "";
            response->success = false;
            response->message = "Recognition failed or returned empty result";
        }
    }

} // namespace voice_toolbox

RCLCPP_COMPONENTS_REGISTER_NODE(voice_toolbox::Simple_ASRService);