#pragma once
// extern pkg
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

#include <string>
#include <memory>
#include <atomic>
#include <filesystem>
#include <thread>
#include <set>
#include <mutex>
#include <map>
#include <deque>

#include "voice_toolbox/asr/asr_engine.hpp"
#include "voice_toolbox/srv/one_shot.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"

#include <rclcpp_components/register_node_macro.hpp>
#include <asio.hpp>

namespace voice_toolbox
{
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;


    class Simple_ASRService : public ASR_Base<voice_toolbox::srv::OneShot>
    {
    public:
        struct WebsocketConfig
        {
            int port = 8000;                      // 服务器端口
            std::string log_level = "INFO";       // 日志级别
            int max_connections = 5;              // 最大连接数
            int connection_timeout = 300;         // 连接超时时间(秒)
        };

        Simple_ASRService(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
        ~Simple_ASRService();

        CallbackReturn on_configure(const rclcpp_lifecycle::State &previous_state) override;
        CallbackReturn on_activate(const rclcpp_lifecycle::State &previous_state) override;

        void handle_service_request(const std::shared_ptr<typename voice_toolbox::srv::OneShot::Request> request,
                                    std::shared_ptr<typename voice_toolbox::srv::OneShot::Response> response) override;


    protected:
    private:
        sherpa_onnx::cxx::OfflineRecognizerConfig asr_config_;
        std::unique_ptr<sherpa_onnx::cxx::OfflineRecognizer> offline_recognizer_;  // 离线识别器
        WebsocketConfig websocket_config_;

        // WebSocket服务器相关成员
        server ws_server_;
        std::thread ws_thread_;
        std::map<connection_hdl, StreamConnectionDataPtr, std::owner_less<connection_hdl>> connections_;
        std::mutex connections_mutex_;

        bool debug_ = false;
        bool enable_stream_asr_ = false;

        // 用于流式识别的定时器和工作上下文
        asio::io_context io_work_context_;
        asio::steady_timer timer_;
        std::thread work_thread_;
        
        // 用于存储活跃连接和待处理连接
        std::set<connection_hdl, std::owner_less<connection_hdl>> active_streams_;
        std::deque<StreamConnectionDataPtr> ready_streams_;
        

        
        // WebSocket事件处理方法
        void handle_websocket_open(connection_hdl hdl);
        void handle_websocket_close(connection_hdl hdl);
        void handle_websocket_error(connection_hdl hdl);
        void handle_websocket_message(connection_hdl hdl, server::message_ptr msg);

    };

} // namespace voice_toolbox