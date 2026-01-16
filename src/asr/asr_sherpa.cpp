#include "voice_toolbox/asr/asr_sherpa.hpp"

namespace voice_toolbox
{
    Sherpa_onnx_ASRSerive::Sherpa_onnx_ASRSerive(const rclcpp::NodeOptions &options)
        : ASR_Base("Sherpa_onnx_ASR", "asr_service", options)
    {
        this->declare_parameter<std::string>("config_file", "/home/kaiser/WORK_SPACE-2/voice_toolbox_ws/src/voice_toolbox/config/voice_toolbox_setting.yaml");
    }

    Sherpa_onnx_ASRSerive::~Sherpa_onnx_ASRSerive()
    {
        // 停止WebSocket服务器
        try {
            ws_server_.stop_listening();
            ws_server_.stop();
            
            if (ws_thread_.joinable()) {
                ws_thread_.join();
            }
            
        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "Error stopping WebSocket server: %s", e.what());
        }
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
     * @brief 激活ASR服务，并创建ASR引擎实例和WebSocket服务器
     * @param state 上一个状态
     * @return CallbackReturn 激活结果
     */
    CallbackReturn Sherpa_onnx_ASRSerive::on_activate(const rclcpp_lifecycle::State &state)
    {
        // 先调用基类的方法创建ros2服务
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
        
        // 启动WebSocket服务器
        try {
            ws_server_.init_asio();
            ws_server_.set_message_handler(std::bind(
                &Sherpa_onnx_ASRSerive::handle_websocket_message, this, std::placeholders::_1, std::placeholders::_2));
            ws_server_.set_open_handler(std::bind(
                &Sherpa_onnx_ASRSerive::handle_websocket_open, this, std::placeholders::_1));
            ws_server_.set_close_handler(std::bind(
                &Sherpa_onnx_ASRSerive::handle_websocket_close, this, std::placeholders::_1));
            ws_server_.set_error_handler(std::bind(
                &Sherpa_onnx_ASRSerive::handle_websocket_error, this, std::placeholders::_1));
            
            ws_server_.listen(websocket_config_.port);
            ws_server_.start_accept();
            
            // 在单独线程中运行WebSocket服务器
            ws_thread_ = std::thread([this]() {
                ws_server_.run();
            });
            
            RCLCPP_INFO(this->get_logger(), "WebSocket server started on port %d", websocket_config_.port);
        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "Failed to start WebSocket server: %s", e.what());
            return CallbackReturn::ERROR;
        }
        
        return CallbackReturn::SUCCESS;
    }

    /**
     * @brief 处理WebSocket连接打开事件
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_open(connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.insert(hdl);
        RCLCPP_INFO(this->get_logger(), "New WebSocket connection established. Total connections: %zu", connections_.size());
    }

    /**
     * @brief 处理WebSocket连接关闭事件
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_close(connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(hdl);
        RCLCPP_INFO(this->get_logger(), "WebSocket connection closed. Remaining connections: %zu", connections_.size());
    }

    /**
     * @brief 处理WebSocket错误事件
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_error(connection_hdl hdl) {
        std::shared_ptr<websocketpp::connection<websocketpp::config::asio>> con = ws_server_.get_con_from_hdl(hdl);
        RCLCPP_ERROR(this->get_logger(), "WebSocket error: %s", con->get_ec().message().c_str());
    }

    /**
     * @brief 处理WebSocket消息
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_message(connection_hdl hdl, server::message_ptr msg) {
        try {
            // 解析JSON消息
            nlohmann::json request = nlohmann::json::parse(msg->get_payload());
            
            // 提取音频文件路径
            std::string audio_path = request.value("audio_path", "");
            if (audio_path.empty()) {
                nlohmann::json response = {
                    {"success", false},
                    {"message", "audio_path is required"}
                };
                ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                return;
            }
            
            // 执行语音识别
            sherpa_onnx::cxx::Wave wave = sherpa_onnx::cxx::ReadWave(audio_path);
            if (wave.samples.empty()) {
                nlohmann::json response = {
                    {"success", false},
                    {"message", "failed to read wave file: " + audio_path}
                };
                ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                return;
            }
            
            sherpa_onnx::cxx::OfflineStream stream = recognizer_->CreateStream();
            stream.AcceptWaveform(wave.sample_rate, wave.samples.data(), wave.samples.size());
            recognizer_->Decode(&stream);
            sherpa_onnx::cxx::OfflineRecognizerResult result = recognizer_->GetResult(&stream);
            
            // 发送识别结果
            nlohmann::json response = {
                {"success", true},
                {"result_text", result.text},
                {"message", ""}
            };
            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
            
            RCLCPP_INFO(this->get_logger(), "Speech recognition completed for %s", audio_path.c_str());
            
        } catch (const std::exception& e) {
            nlohmann::json response = {
                {"success", false},
                {"message", std::string("Error processing request: ") + e.what()}
            };
            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
        }
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