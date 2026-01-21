#include "voice_toolbox/asr/simple_asr.hpp"
#include <asio.hpp>

namespace voice_toolbox
{
    Simple_ASRService::Simple_ASRService(const rclcpp::NodeOptions &options)
        : ASREngineImpl("Sherpa_onnx_ASR", "asr_service", options)
    {
        this->declare_parameter<std::string>("config_file", "/home/kaiser/WORK_SPACE-2/voice_toolbox_ws/src/voice_toolbox/config/voice_toolbox_setting.yaml");
        this->declare_parameter<bool>("debug", false);
    }

    Simple_ASRService::~Simple_ASRService()
    {
        // release resource
        try
        {
            ws_server_.stop_listening();
            ws_server_.stop();

            if (ws_thread_.joinable())
            {
                ws_thread_.join();
            }

            if (work_thread_.joinable())
            {
                io_work_context_.stop();
                work_thread_.join();
            }
        }
        catch (const std::exception &e)
        {
            RCLCPP_ERROR(this->get_logger(), "Error stopping WebSocket server: %s", e.what());
        }
    }

    /**
     * @brief Initialize the websocket server
     */
    bool Simple_ASRService::initWebSocketServer()
    {
        try
        {
            // set logging settings
            ws_server_.set_access_channels(websocketpp::log::alevel::connect |
                                           websocketpp::log::alevel::disconnect |
                                           websocketpp::log::alevel::app);
            ws_server_.clear_access_channels(websocketpp::log::alevel::frame_payload |
                                             websocketpp::log::alevel::frame_header);
            ws_server_.set_error_channels(websocketpp::log::elevel::warn |
                                          websocketpp::log::elevel::rerror |
                                          websocketpp::log::elevel::fatal);

            ws_server_.init_asio();
            ws_server_.set_message_handler(std::bind(
                &Simple_ASRService::handle_websocket_message, this, std::placeholders::_1, std::placeholders::_2));
            ws_server_.set_open_handler(std::bind(
                &Simple_ASRService::handle_websocket_open, this, std::placeholders::_1));
            ws_server_.set_close_handler(std::bind(
                &Simple_ASRService::handle_websocket_close, this, std::placeholders::_1));
            ws_server_.set_fail_handler(std::bind(
                &Simple_ASRService::handle_websocket_error, this, std::placeholders::_1));

            ws_server_.listen(websocket_config_.port);
            ws_server_.start_accept();
            ws_thread_ = std::thread([this]()
                                     { ws_server_.run(); });

            RCLCPP_INFO(this->get_logger(), "WebSocket server started on port %d", websocket_config_.port);
        }
        catch (const std::exception &e)
        {
            RCLCPP_ERROR(this->get_logger(), "Failed to start WebSocket server: %s", e.what());
            return false;
        }
        return true;
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

        asr_config_.model_config.debug = asr_node["debug"] ? asr_node["debug"].as<bool>() : false;

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

        sensevoice_engine_ = SenseVoiceOffline(asr_config_);
        // initialize ASR engine
        if (!sensevoice_engine_.InitializeASREngine())
        {
            RCLCPP_ERROR(get_logger(), "SenseVoice ASR engine initialization failed. Please check the configuration");
            return CallbackReturn::ERROR;
        }
        return CallbackReturn::SUCCESS;
    }

    /**
     * @brief Handling WebSocket connection open events
     */
    void Simple_ASRService::handle_websocket_open(connection_hdl hdl)
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[hdl] = std::make_shared<StreamConnectionData>();
        if (debug_)
            RCLCPP_INFO(this->get_logger(), "New WebSocket connection established. Total connections: %zu", connections_.size());
    }

    /**
     * @brief Handling WebSocket connection close events
     */
    void Simple_ASRService::handle_websocket_close(connection_hdl hdl)
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(hdl);
    }

    /**
     * @brief Handling WebSocket error events
     */
    void Simple_ASRService::handle_websocket_error(connection_hdl hdl)
    {
        std::shared_ptr<websocketpp::connection<websocketpp::config::asio>> con = ws_server_.get_con_from_hdl(hdl);
        RCLCPP_ERROR(this->get_logger(), "WebSocket error: %s", con->get_ec().message().c_str());
    }

    /**
     * @brief Handling WebSocket messages
     */
    void Simple_ASRService::handle_websocket_message(connection_hdl hdl, server::message_ptr msg)
    {

        catch (const std::exception &e)
        {
            nlohmann::json response = {
                {"success", false},
                {"message", std::string("Error processing request: ") + e.what()}};
            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
        }
    }

    /**
     * @brief Handles local file speech recognition requests by ros service
     * @param request Service request object
     * @param response Service response object
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

RCLCPP_COMPONENTS_REGISTER_NODE(voice_toolbox::Simple_ASRService);