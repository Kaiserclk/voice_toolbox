#pragma once
// extern pkg

#include "voice_toolbox/asr/asr-websocket.hpp"

#include <string>
#include <memory>

#include "voice_toolbox/asr/asr-engine.hpp"
#include "voice_toolbox/asr/asr-server-impl.hpp"

#include "voice_toolbox/srv/one_shot.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"

#include <rclcpp_components/register_node_macro.hpp>
#include <asio.hpp>

namespace voice_toolbox
{

    class Simple_ASRService : public ASR_Service<voice_toolbox::srv::OneShot>
    {
    public:
        Simple_ASRService(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
        ~Simple_ASRService();

        CallbackReturn on_configure(const rclcpp_lifecycle::State &previous_state) override;
        CallbackReturn on_activate(const rclcpp_lifecycle::State &previous_state) override;

        void handle_service_request(const std::shared_ptr<typename voice_toolbox::srv::OneShot::Request> request,
                                    std::shared_ptr<typename voice_toolbox::srv::OneShot::Response> response) override;

    protected:
    private:
        sherpa_onnx::cxx::OfflineRecognizerConfig asr_config_;
        std::unique_ptr<asr_engine::SenseVoiceOffline> sensevoice_;
        websocket_asr::WebsocketConfig websocket_config_;
        std::unique_ptr<websocket_asr::WebsocketOfflineASR> websocket_ptr_;
        bool debug_ = false;
    };

} // namespace voice_toolbox