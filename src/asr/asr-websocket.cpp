#include "voice_toolbox/asr/asr-websocket.hpp"
#include <iostream>
#include <cstring>

namespace websocket_asr
{

    InlineWebsocketASR::InlineWebsocketASR(const WebsocketConfig &config)
    {
        config_.max_connections = config.max_connections;
        config_.port = config.port;
    }

    InlineWebsocketASR::~InlineWebsocketASR()
    {
        try
        {
            ws_server_.stop_listening();
            ws_server_.stop();

            if (ws_thread_.joinable())
            {
                ws_thread_.join();
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error stopping WebSocket server: " << e.what() << std::endl;
        }
    }

    bool InlineWebsocketASR::Initialize()
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
                &InlineWebsocketASR::handle_message, this, std::placeholders::_1, std::placeholders::_2));
            ws_server_.set_open_handler(std::bind(
                &InlineWebsocketASR::handle_open, this, std::placeholders::_1));
            ws_server_.set_close_handler(std::bind(
                &InlineWebsocketASR::handle_close, this, std::placeholders::_1));
            ws_server_.set_fail_handler(std::bind(
                &InlineWebsocketASR::handle_error, this, std::placeholders::_1));
            ws_server_.listen(config_.port);
            ws_server_.start_accept();

            ws_thread_ = std::thread([this]()
                                     { ws_server_.run(); });

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to initialize WebSocket server: " << e.what() << std::endl;
            return false;
        }
    }

    void InlineWebsocketASR::SetAudioProcessingCallback(AudioProcessingCallback callback)
    {
        audio_callback_ = callback;
    }

    /**
     * @brief Handling WebSocket connection open events
     * @param hdl Connection handle
     */
    void InlineWebsocketASR::handle_open(connection_hdl hdl)
    {
        std::cout << "New WebSocket connection established." << std::endl;
        
        // 初始化此连接的音频缓冲区
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        audio_buffers_[hdl] = std::vector<int16_t>();
        expected_sizes_[hdl] = 0;
    }

    /**
     * @brief Handling WebSocket connection close events
     * @param hdl Connection handle
     */
    void InlineWebsocketASR::handle_close(connection_hdl hdl)
    {
        std::cout << "WebSocket connection closed." << std::endl;
        
        // 清理此连接的音频缓冲区
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        audio_buffers_.erase(hdl);
        expected_sizes_.erase(hdl);
    }

    /**
     * @brief Handling WebSocket error events
     */
    void InlineWebsocketASR::handle_error(connection_hdl hdl)
    {
        std::shared_ptr<websocketpp::connection<websocketpp::config::asio>> con = ws_server_.get_con_from_hdl(hdl);
        std::cerr << "WebSocket error: " << con->get_ec().message() << std::endl;
    }

    /**
     * @brief Handling WebSocket messages
     */
    void InlineWebsocketASR::handle_message(connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            // 检查消息类型，如果是二进制消息则处理音频数据
            if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
                // 获取二进制数据
                std::string payload = msg->get_payload();
                
                // 解析前8个字节，前4个字节为采样率，后4个字节为音频数据大小
                if (payload.size() >= 8) {
                    uint32_t sample_rate, data_size;
                    
                    // 从前4个字节提取采样率
                    std::memcpy(&sample_rate, payload.data(), sizeof(uint32_t));
                    
                    // 从第5-8个字节提取数据大小
                    std::memcpy(&data_size, payload.data() + sizeof(uint32_t), sizeof(uint32_t));
                    
                    // 验证采样率是否有效
                    if (sample_rate == 0) {
                        std::cerr << "Warning: Invalid sample rate received: 0, using default 16000" << std::endl;
                        sample_rate = 16000;
                    }
                    
                    // 存储采样率信息以便后续处理
                    expected_sizes_[hdl] = sample_rate;
                    
                    // 获取实际音频数据（跳过前8个字节）
                    const char* audio_data_start = payload.data() + 8;
                    size_t audio_data_length = payload.size() - 8;
                    
                    // 将音频数据转换为int16_t并追加到缓冲区
                    std::lock_guard<std::mutex> lock(buffer_mutex_);
                    std::vector<int16_t>& buffer = audio_buffers_[hdl];
                    
                    // 计算有多少个int16_t样本
                    size_t num_samples = audio_data_length / sizeof(int16_t);
                    
                    for (size_t i = 0; i < num_samples; ++i) {
                        int16_t sample_value;
                        std::memcpy(&sample_value, audio_data_start + i * sizeof(int16_t), sizeof(int16_t));
                        buffer.push_back(sample_value);
                    }
                    
                    // 发送确认消息，告知客户端已接收部分数据
                    nlohmann::json response = {
                        {"success", true},
                        {"message", "Audio chunk received"},
                        {"received_bytes", audio_data_length}
                    };
                    ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                } else {
                    nlohmann::json response = {
                        {"success", false},
                        {"message", "Invalid binary message format: header too short"}
                    };
                    ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                }
            } 
            // 如果是文本消息，则检查是否为"Done"命令
            else {
                std::string payload = msg->get_payload();
                
                // 直接检查是否为"Done"命令，而不是尝试解析为JSON
                if (payload == "Done" || payload == "\"Done\"") {  // 接受纯文本"Done"或JSON格式的"Done"
                    // 获取当前连接的音频数据
                    std::lock_guard<std::mutex> lock(buffer_mutex_);
                    auto buffer_it = audio_buffers_.find(hdl);
                    if (buffer_it != audio_buffers_.end()) {
                        std::vector<int16_t>& audio_data = buffer_it->second;
                        
                        // 从expected_sizes_获取对应的采样率（如果存储的话，否则使用默认值）
                        auto size_it = expected_sizes_.find(hdl);
                        uint32_t sample_rate = 16000; // 默认采样率
                        if(size_it != expected_sizes_.end()) {
                           sample_rate = size_it->second; 
                        }
                        
                        // 调用外部audio_callback_方法进行语音识别，直接传递int16数据
                        if (audio_callback_) {
                            std::string result = audio_callback_(audio_data, sample_rate);
                            
                            // 将识别结果发送回客户端
                            ws_server_.send(hdl, result, websocketpp::frame::opcode::text);
                        } else {
                            nlohmann::json response = {
                                {"success", false},
                                {"message", "Audio processing callback is not set"}
                            };
                            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                        }
                        
                        // 清空当前连接的音频缓冲区
                        audio_data.clear();
                    } else {
                        nlohmann::json response = {
                            {"success", false},
                            {"message", "No audio data found for this connection"}
                        };
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    }
                } else {
                    // 对于其他文本消息，尝试解析为JSON
                    try {
                        nlohmann::json json_msg = nlohmann::json::parse(payload);
                        nlohmann::json response = {
                            {"success", false},
                            {"message", "Unknown text command, send 'Done' to trigger recognition"}
                        };
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    } catch (const nlohmann::json::exception &e) {
                        nlohmann::json response = {
                            {"success", false},
                            {"message", std::string("JSON parsing error: ") + e.what()}
                        };
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    }
                }
            }
        }
        catch (const nlohmann::json::exception &e)
        {
            nlohmann::json response = {
                {"success", false},
                {"message", std::string("JSON parsing error: ") + e.what()}
            };
            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
        }
        catch (const std::exception &e)
        {
            nlohmann::json response = {
                {"success", false},
                {"message", std::string("Error processing request: ") + e.what()}
            };
            ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
        }
    }

} // namespace websocket_asr