#pragma once
#include <string>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>

namespace websocket_asr
{
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;
    
    struct WebsocketConfig
    {
        int port = 8000;                // 服务器端口
        std::string log_level = "INFO"; // 日志级别
        int max_connections = 5;        // 最大连接数
    };

    using AudioProcessingCallback = std::function<nlohmann::json(const std::vector<float>& audio_data)>;
    using AudioFileProcessingCallback = std::function<nlohmann::json(const std::string& audio_file_path)>;

    class WebsocketOfflineASR
    {
    public:
        WebsocketOfflineASR(const WebsocketConfig &config);
        ~WebsocketOfflineASR();
        bool Initialize();
        void SetAudioProcessingCallback(AudioProcessingCallback callback);
        void SetAudioFileProcessingCallback(AudioFileProcessingCallback callback);

    private:
        server ws_server_;
        std::thread ws_thread_;
        WebsocketConfig config_;
        
        AudioProcessingCallback audio_callback_;
        AudioFileProcessingCallback audio_file_callback_;

        // WebSocket事件处理方法
        void handle_open(connection_hdl hdl);
        void handle_close(connection_hdl hdl);
        void handle_error(connection_hdl hdl);
        void handle_message(connection_hdl hdl, server::message_ptr msg);
    };

} // namespace websocket_asr