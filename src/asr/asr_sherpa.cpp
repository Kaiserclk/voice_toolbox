#include "voice_toolbox/asr/asr_sherpa.hpp"
#include <asio.hpp>

namespace voice_toolbox
{
    Sherpa_onnx_ASRSerive::Sherpa_onnx_ASRSerive(const rclcpp::NodeOptions &options)
        : ASR_Base("Sherpa_onnx_ASR", "asr_service", options),
          timer_(io_work_context_)
    {
        this->declare_parameter<std::string>("config_file", "/home/kaiser/WORK_SPACE-2/voice_toolbox_ws/src/voice_toolbox/config/voice_toolbox_setting.yaml");
        this->declare_parameter<bool>("debug", false);
        this->declare_parameter<bool>("enable_stream_asr", true);
    }

    Sherpa_onnx_ASRSerive::~Sherpa_onnx_ASRSerive()
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
     * @brief Initialize the ASR engine
     */

    bool initASREngine()
    {
    }

    /**
     * @brief Initialize the websocket server
     */
    bool Sherpa_onnx_ASRSerive::initWebSocketServer()
    {
        try
        {
            //set logging settings
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
                &Sherpa_onnx_ASRSerive::handle_websocket_message, this, std::placeholders::_1, std::placeholders::_2));
            ws_server_.set_open_handler(std::bind(
                &Sherpa_onnx_ASRSerive::handle_websocket_open, this, std::placeholders::_1));
            ws_server_.set_close_handler(std::bind(
                &Sherpa_onnx_ASRSerive::handle_websocket_close, this, std::placeholders::_1));
            ws_server_.set_fail_handler(std::bind(
                &Sherpa_onnx_ASRSerive::handle_websocket_error, this, std::placeholders::_1));

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

    CallbackReturn Sherpa_onnx_ASRSerive::on_configure(const rclcpp_lifecycle::State &)
    {
        // get parameter
        std::string config_file = this->get_parameter("config_file").as_string();
        debug_ = this->get_parameter("debug").as_bool();
        enable_stream_asr_ = this->get_parameter("enable_stream_asr").as_bool();

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
     * @brief Initialize the online recognizer for streaming recognition
     */
    bool Sherpa_onnx_ASRSerive::initOnlineRecognizer()
    {
        // 创建在线识别配置
        sherpa_onnx::cxx::OnlineRecognizerConfig online_config;
        
        // 从配置文件中加载在线识别参数
        YAML::Node config = YAML::LoadFile(this->get_parameter("config_file").as_string());
        if (config["asr_sherpa_onnx"] && config["asr_sherpa_onnx"]["online_model"])
        {
            auto online_node = config["asr_sherpa_onnx"]["online_model"];

            // 配置Transducer模型
            if (online_node["transducer"])
            {
                auto transducer_node = online_node["transducer"];

                if (transducer_node["encoder"])
                {
                    online_config.model_config.transducer.encoder = transducer_node["encoder"].as<std::string>();
                }
                if (transducer_node["decoder"])
                {
                    online_config.model_config.transducer.decoder = transducer_node["decoder"].as<std::string>();
                }
                if (transducer_node["joiner"])
                {
                    online_config.model_config.transducer.joiner = transducer_node["joiner"].as<std::string>();
                }
            }

            // 配置Paraformer模型
            if (online_node["paraformer"])
            {
                auto paraformer_node = online_node["paraformer"];
                if (paraformer_node["encoder"])
                {
                    online_config.model_config.paraformer.encoder = paraformer_node["encoder"].as<std::string>();
                }
                if (paraformer_node["decoder"])
                {
                    online_config.model_config.paraformer.decoder = paraformer_node["decoder"].as<std::string>();
                }
            }

            // 配置Tokens和其他参数
            if (online_node["tokens"])
            {
                online_config.model_config.tokens = online_node["tokens"].as<std::string>();
            }

            if (online_node["num_threads"])
            {
                online_config.model_config.num_threads = online_node["num_threads"].as<int>();
            }

            if (online_node["decoding_method"])
            {
                online_config.decoding_method = online_node["decoding_method"].as<std::string>();
            }
            
            // 设置采样率
            if (online_node["sample_rate"])
            {
                online_config.feat_config.sampling_rate = online_node["sample_rate"].as<int>();
            }
        }
        else
        {
            // 如果没有专门的在线识别配置，尝试使用离线模型的兼容配置
            RCLCPP_WARN(this->get_logger(), "No online model configuration found in config file. Using offline model for streaming recognition.");
            
            // 尝试复制离线配置作为基础
            online_config.model_config = asr_config_.model_config;
            online_config.feat_config.sampling_rate = 16000; // 默认采样率
        }

        // 创建在线识别器
        auto temp_online_recognizer = sherpa_onnx::cxx::OnlineRecognizer::Create(online_config);
        if (!temp_online_recognizer.Get())
        {
            RCLCPP_WARN(this->get_logger(), "Failed to initialize online recognizer. Streaming recognition will not be available.");
            return false;
        }

        online_recognizer_ = std::make_unique<sherpa_onnx::cxx::OnlineRecognizer>(std::move(temp_online_recognizer));
        return true;
    }

    /**
     * @brief Activate the ASR service and create the ASR engine instance and WebSocket server
     * @param state Previous state
     * @return CallbackReturn Activation result
     */
    CallbackReturn Sherpa_onnx_ASRSerive::on_activate(const rclcpp_lifecycle::State &state)
    {
        // Create a ROS2 service by calling the base class method
        auto base_state = ASR_Base<voice_toolbox::srv::OneShot>::on_activate(state);
        if (base_state != CallbackReturn::SUCCESS)
        {
            return base_state;
        }
        auto temp_recognizer = sherpa_onnx::cxx::OfflineRecognizer::Create(asr_config_);
        if (!temp_recognizer.Get())
        {
            RCLCPP_ERROR(get_logger(), "ASR engine initialization failed. Please check the ASR configuration");
            return CallbackReturn::ERROR;
        }
        offline_recognizer_ = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(std::move(temp_recognizer));


        if(enable_stream_asr_ && !initWebSocketServer()){
            RCLCPP_ERROR(get_logger(), "Failed to initialize WebSocket server. Please check the WebSocket configuration");
            return CallbackReturn::ERROR;
        }
        // 初始化在线识别器
        initOnlineRecognizer();

        // 启动流处理
        if (enable_stream_asr_)
        {
            work_thread_ = std::thread([this]() { io_work_context_.run(); });
            StartStreamProcessing();
        }

        return CallbackReturn::SUCCESS;
    }

    /**
     * @brief Handling WebSocket connection open events
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_open(connection_hdl hdl)
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[hdl] = std::make_shared<StreamConnectionData>();
        if (debug_)
            RCLCPP_INFO(this->get_logger(), "New WebSocket connection established. Total connections: %zu", connections_.size());
    }

    /**
     * @brief Handling WebSocket connection close events
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_close(connection_hdl hdl)
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(hdl);
    }

    /**
     * @brief Handling WebSocket error events
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_error(connection_hdl hdl)
    {
        std::shared_ptr<websocketpp::connection<websocketpp::config::asio>> con = ws_server_.get_con_from_hdl(hdl);
        RCLCPP_ERROR(this->get_logger(), "WebSocket error: %s", con->get_ec().message().c_str());
    }

    /**
     * @brief Handling WebSocket messages
     */
    void Sherpa_onnx_ASRSerive::handle_websocket_message(connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            // 检查消息类型
            if (msg->get_opcode() == websocketpp::frame::opcode::text)
            {
                // 文本消息 - 处理一次性语音识别请求或控制命令
                nlohmann::json request = nlohmann::json::parse(msg->get_payload());

                // 检查是否是"Done"命令，结束流式识别
                if (request.is_string() && request.get<std::string>() == "Done")
                {
                    // 完成当前流式识别并发送结果
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    auto it = connections_.find(hdl);
                    if (it != connections_.end() && it->second->stream)
                    {
                        // 输入完成，发送尾部静音并获取最终结果
                        it->second->stream->InputFinished();
                        
                        if (online_recognizer_)
                        {
                            // 获取最终结果
                            auto result = online_recognizer_->GetResult(it->second->stream.get());

                            nlohmann::json response = {
                                {"success", true},
                                {"result_text", result.text},
                                {"is_final", true},
                                {"message", "Streaming recognition completed"}};

                            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                        }
                        else
                        {
                            nlohmann::json response = {
                                {"success", false},
                                {"message", "Online recognizer not initialized, cannot complete streaming recognition"}};
                            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                        }

                        // 清理连接数据
                        it->second->Clear();
                    }
                    else
                    {
                        nlohmann::json response = {
                            {"success", false},
                            {"message", "No active streaming session to complete"}};
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    }
                    return;
                }

                // 处理一次性语音识别请求
                std::string audio_path = request.value("audio_path", "");
                if (!std::filesystem::exists(audio_path))
                {
                    nlohmann::json response = {
                        {"success", false},
                        {"message", "audio_path does not exist"}};
                    ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    return;
                }
                sherpa_onnx::cxx::Wave wave = sherpa_onnx::cxx::ReadWave(audio_path);
                if (wave.samples.empty())
                {
                    nlohmann::json response = {
                        {"success", false},
                        {"message", "failed to read wave file: " + audio_path}};
                    ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    return;
                }

                // 加Locking保护对recognizer_的访问
                std::lock_guard<std::mutex> lock(recognizer_mutex_);
                sherpa_onnx::cxx::OfflineStream stream = offline_recognizer_->CreateStream();
                stream.AcceptWaveform(wave.sample_rate, wave.samples.data(), wave.samples.size());
                offline_recognizer_->Decode(&stream);
                sherpa_onnx::cxx::OfflineRecognizerResult result = offline_recognizer_->GetResult(&stream);

                // 发送识别结果
                nlohmann::json response = {
                    {"success", true},
                    {"result_text", result.text},
                    {"message", ""}};
                ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
            }
            else if (msg->get_opcode() == websocketpp::frame::opcode::binary)
            {
                // 二进制消息 - 处理流式语音识别
                const std::string &payload = msg->get_payload();
                
                std::lock_guard<std::mutex> lock(connections_mutex_);
                auto connection_data = connections_.find(hdl);

                if (connection_data == connections_.end())
                {
                    nlohmann::json response = {
                        {"success", false},
                        {"message", "Connection data not found"}};
                    ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    return;
                }

                // 如果还没有初始化流，先创建流
                if (!connection_data->second->stream && online_recognizer_)
                {
                    connection_data->second->stream = std::make_unique<sherpa_onnx::cxx::OnlineStream>(
                        online_recognizer_->CreateStream());
                }

                // 解析音频数据（直接处理，不再使用预定义的8字节头部格式）
                auto p = reinterpret_cast<const float*>(payload.data());
                int32_t num_samples = payload.size() / sizeof(float);
                
                // 推送音频数据到流
                if (connection_data->second->stream)
                {
                    connection_data->second->stream->AcceptWaveform(16000, p, num_samples);
                    
                    // 可选：返回中间识别结果
                    if (online_recognizer_ && online_recognizer_->IsReady(connection_data->second->stream.get()))
                    {
                        auto partial_result = online_recognizer_->GetResult(connection_data->second->stream.get());
                        
                        nlohmann::json partial_response = {
                            {"partial_result", partial_result.text},
                            {"is_final", false}};
                        ws_server_.send(hdl, partial_response.dump(), websocketpp::frame::opcode::text);
                    }
                }
            }
        }
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
        if (!std::filesystem::exists(request->audio_path))
        {
            response->result_text = "";
            response->success = false;
            response->message = "Audio file does not exist: " + request->audio_path;
            return;
        }
        sherpa_onnx::cxx::Wave wave = sherpa_onnx::cxx::ReadWave(request->audio_path);
        if (wave.samples.empty())
        {
            response->result_text = "";
            response->success = false;
            response->message = "failed to read wave file:: " + request->audio_path;
            return;
        }

        // Locking protects access to recognizer_
        std::lock_guard<std::mutex> lock(recognizer_mutex_);
        sherpa_onnx::cxx::OfflineStream stream = offline_recognizer_->CreateStream();
        stream.AcceptWaveform(wave.sample_rate, wave.samples.data(), wave.samples.size());

        offline_recognizer_->Decode(&stream);

        sherpa_onnx::cxx::OfflineRecognizerResult result = offline_recognizer_->GetResult(&stream);
        response->result_text = result.text;
        response->success = true;
        response->message = "";
    }

    /**
     * @brief Start periodic processing of streams
     */
    void Sherpa_onnx_ASRSerive::StartStreamProcessing()
    {
        timer_.expires_after(std::chrono::milliseconds(loop_interval_ms_));
        timer_.async_wait([this](const asio::error_code &ec) { ProcessStreams(ec); });
    }

    /**
     * @brief Process streams periodically
     */
    void Sherpa_onnx_ASRSerive::ProcessStreams(const asio::error_code &ec)
    {
        if (ec)
        {
            RCLCPP_ERROR(this->get_logger(), "Stream processing timer error: %s", ec.message().c_str());
            return;
        }

        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        std::vector<connection_hdl> to_remove;
        for (auto &[hdl, conn_data] : connections_)
        {
            // 检查连接是否仍然存在
            if (!ws_server_.get_con_from_hdl(hdl))
            {
                to_remove.push_back(hdl);
                continue;
            }

            // 检查此连接是否已经在活跃列表中
            if (active_streams_.count(hdl))
            {
                continue;
            }

            // 检查是否准备好进行解码
            if (conn_data->stream && online_recognizer_ && online_recognizer_->IsReady(conn_data->stream.get()))
            {
                ready_streams_.push_back(conn_data);
                active_streams_.insert(hdl);
            }
        }

        // 移除无效连接
        for (auto &hdl : to_remove)
        {
            connections_.erase(hdl);
            active_streams_.erase(hdl);
        }

        // 如果有准备好的流，开始批处理
        if (!ready_streams_.empty())
        {
            asio::post(io_work_context_, [this]() { DecodeBatch(); });
        }

        // 继续下一轮处理
        timer_.expires_after(std::chrono::milliseconds(loop_interval_ms_));
        timer_.async_wait([this](const asio::error_code &ec) { ProcessStreams(ec); });
    }

    /**
     * @brief Decode a batch of streams
     */
    void Sherpa_onnx_ASRSerive::DecodeBatch()
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        if (ready_streams_.empty())
        {
            return;
        }

        // 准备要处理的批次
        std::vector<StreamConnectionDataPtr> batch_streams;
        std::vector<sherpa_onnx::cxx::OnlineStream*> batch_ptrs;
        
        while (!ready_streams_.empty() && static_cast<int32_t>(batch_streams.size()) < max_batch_size_)
        {
            auto conn_data = ready_streams_.front();
            ready_streams_.pop_front();

            batch_streams.push_back(conn_data);
            batch_ptrs.push_back(conn_data->stream.get());
        }

        if (!ready_streams_.empty())
        {
            // 如果还有更多准备好的流，调度另一个处理任务
            asio::post(io_work_context_, [this]() { DecodeBatch(); });
        }

        // 解码这一批流
        if (online_recognizer_)
        {
            online_recognizer_->DecodeStreams(batch_ptrs.data(), batch_ptrs.size());
        }

        // 处理每个流的结果
        for (auto &conn_data : batch_streams)
        {
            auto result = online_recognizer_ ? online_recognizer_->GetResult(conn_data->stream.get()) : sherpa_onnx::cxx::OnlineRecognizerResult{};
            
            // 查找对应的连接句柄
            connection_hdl target_hdl;
            bool found_target = false;
            
            for (const auto &[hdl, data] : connections_)
            {
                if (data == conn_data)
                {
                    target_hdl = hdl;
                    found_target = true;
                    break;
                }
            }
            
            if (found_target && ws_server_.get_con_from_hdl(target_hdl))
            {
                nlohmann::json response_json = {
                    {"partial_result", result.text},
                    {"is_final", false}
                };
                
                ws_server_.send(target_hdl, response_json.dump(), websocketpp::frame::opcode::text);
            }
        }

        // 从活跃列表中移除已完成的流
        for (const auto &[hdl, data] : connections_)
        {
            bool found = false;
            for (const auto &batch_data : batch_streams)
            {
                if (data == batch_data)
                {
                    active_streams_.erase(hdl);
                    found = true;
                    break;
                }
            }
        }
    }

} // namespace voice_toolbox

RCLCPP_COMPONENTS_REGISTER_NODE(voice_toolbox::Sherpa_onnx_ASRSerive);