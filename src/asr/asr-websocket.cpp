#include "voice_toolbox/asr/asr-websocket.hpp"
#include <iostream>

namespace websocket_asr
{
    WebsocketOfflineASR::WebsocketOfflineASR(const WebsocketConfig &config)
    {
        config_.log_level = config.log_level;
        config_.max_connections = config.max_connections;
        config_.port = config.port;
    }

    WebsocketOfflineASR::~WebsocketOfflineASR()
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

    void WebsocketOfflineASR::SetAudioProcessingCallback(AudioProcessingCallback callback)
    {
        audio_callback_ = callback;
    }


    bool WebsocketOfflineASR::Initialize()
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
                &WebsocketOfflineASR::handle_message, this, std::placeholders::_1, std::placeholders::_2));
            ws_server_.set_open_handler(std::bind(
                &WebsocketOfflineASR::handle_open, this, std::placeholders::_1));
            ws_server_.set_close_handler(std::bind(
                &WebsocketOfflineASR::handle_close, this, std::placeholders::_1));
            ws_server_.set_fail_handler(std::bind(
                &WebsocketOfflineASR::handle_error, this, std::placeholders::_1));

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

    /**
     * @brief Handling WebSocket connection open events
     */
    void WebsocketOfflineASR::handle_open(connection_hdl hdl)
    {
        std::cout << "New WebSocket connection established." << std::endl;
    }

    /**
     * @brief Handling WebSocket connection close events
     */
    void WebsocketOfflineASR::handle_close(connection_hdl hdl)
    {
        std::cout << "WebSocket connection closed." << std::endl;
    }

    /**
     * @brief Handling WebSocket error events
     */
    void WebsocketOfflineASR::handle_error(connection_hdl hdl)
    {
        std::shared_ptr<websocketpp::connection<websocketpp::config::asio>> con = ws_server_.get_con_from_hdl(hdl);
        std::cerr << "WebSocket error: " << con->get_ec().message() << std::endl;
    }

    /**
     * @brief Handling WebSocket messages
     */
    void WebsocketOfflineASR::handle_message(connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            std::string payload = msg->get_payload();
            nlohmann::json json_msg;

            try
            {
                json_msg = nlohmann::json::parse(payload);
            }
            catch (const nlohmann::json::parse_error &e)
            {
                nlohmann::json response = {
                    {"success", false},
                    {"message", std::string("Invalid JSON: ") + e.what()}};
                ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                return;
            }

            // Determine action based on message type
            if (json_msg.contains("type"))
            {
                std::string msg_type = json_msg["type"];

                if (msg_type == "recognize")
                {
                    // Perform recognition on audio data
                    if (json_msg.contains("data"))
                    {
                        // Process audio data
                        std::vector<float> audio_data = json_msg["data"];
                        
                        nlohmann::json response;
                        
                        // Check if audio_callback_ is set before using it
                        if (audio_callback_)
                        {
                            // Call the external function to process audio data
                            response = audio_callback_(audio_data);
                        }
                        else
                        {
                            // Fallback if no callback is set
                            response = {
                                {"success", false},
                                {"message", "Audio processing callback not set"}
                            };
                        }
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    }
                    else
                    {
                        nlohmann::json response = {
                            {"success", false},
                            {"message", "Missing audio data in recognize request"}
                        };
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    }
                }
                else if (msg_type == "recognize_file")
                {
                    // Handle case where client sends a file path for recognition
                    if (json_msg.contains("file_path"))
                    {
                        std::string file_path = json_msg["file_path"];
                        
                        nlohmann::json response;
                        
                        // Check if audio_file_callback_ is set before using it
                        if (audio_file_callback_)
                        {
                            // Call the external function to process audio file
                            response = audio_file_callback_(file_path);
                        }
                        else
                        {
                            // Fallback if no callback is set
                            response = {
                                {"success", false},
                                {"message", "Audio file processing callback not set"}
                            };
                        }
                        
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    }
                    else
                    {
                        nlohmann::json response = {
                            {"success", false},
                            {"message", "Missing file_path in recognize_file request"}
                        };
                        ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
                    }
                }
                else
                {
                    nlohmann::json response = {
                        {"success", false},
                        {"message", "Unknown message type, use 'recognize' or 'recognize_file'"}
                    };
                    ws_server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
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

} // namespace websocket_asr