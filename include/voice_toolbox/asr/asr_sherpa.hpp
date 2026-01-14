#pragma once
// extern pkg
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <yaml-cpp/yaml.h>

#include <string>
#include <memory>
#include <atomic>
#include <filesystem>

#include "voice_toolbox/asr/asr_engine.hpp"
#include "voice_toolbox/srv/one_shot.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"

#include <rclcpp_components/register_node_macro.hpp>
namespace voice_toolbox
{
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;

    class Sherpa_onnx_ASRSerive : public ASR_Base<voice_toolbox::srv::OneShot>
    {
    public:
        struct WebsocketConfig
        {
            int port = 8000;                      // 服务器端口
            std::string models_root = "./assets"; // 模型根目录
            std::string log_level = "INFO";       // 日志级别
            int max_connections = 100;            // 最大连接数
            int connection_timeout = 300;         // 连接超时时间(秒)
        };

        Sherpa_onnx_ASRSerive(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
        ~Sherpa_onnx_ASRSerive() {};

        CallbackReturn on_configure(const rclcpp_lifecycle::State &previous_state) override;
        CallbackReturn on_activate(const rclcpp_lifecycle::State &previous_state) override;

        void handle_service_request(const std::shared_ptr<typename voice_toolbox::srv::OneShot::Request> request, 
            std::shared_ptr<typename voice_toolbox::srv::OneShot::Response> response) override;

    protected:

    private:
        sherpa_onnx::cxx::OfflineRecognizerConfig asr_config_;
        std::unique_ptr<sherpa_onnx::cxx::OfflineRecognizer> recognizer_;
        WebsocketConfig websocket_config_;
    };

} // namespace voice_toolbox