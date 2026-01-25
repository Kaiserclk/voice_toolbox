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
        int max_connections = 5;        // 最大连接数
    };

    using AudioProcessingCallback = std::function<std::string(const std::vector<int16_t>& audio_data, uint32_t sample_rate)>;

    // 自定义哈希函数用于connection_hdl
    struct ConnectionHash {
        std::size_t operator()(const connection_hdl& hdl) const {
            // 将weak_ptr锁定并获取其地址作为哈希值
            auto ptr = hdl.lock();
            return std::hash<void*>{}(ptr.get());
        }
    };
    
    // 自定义比较函数用于connection_hdl
    struct ConnectionEqual {
        bool operator()(const connection_hdl& lhs, const connection_hdl& rhs) const {
            return lhs.lock() == rhs.lock();
        }
    };

    class InlineWebsocketASR
    {
    public:
        InlineWebsocketASR(const WebsocketConfig &config);
        ~InlineWebsocketASR();
        bool Initialize();
        void SetAudioProcessingCallback(AudioProcessingCallback callback);
    private:
        server ws_server_;
        std::thread ws_thread_;
        WebsocketConfig config_;
        
        AudioProcessingCallback audio_callback_;
        std::unordered_map<connection_hdl, std::vector<int16_t>, ConnectionHash, ConnectionEqual> audio_buffers_;
        std::unordered_map<connection_hdl, uint32_t, ConnectionHash, ConnectionEqual> expected_sizes_;
        std::mutex buffer_mutex_;

        // WebSocket事件处理方法
        void handle_open(connection_hdl hdl);
        void handle_close(connection_hdl hdl);
        void handle_error(connection_hdl hdl);
        void handle_message(connection_hdl hdl, server::message_ptr msg);
    };


} // namespace websocket_asr