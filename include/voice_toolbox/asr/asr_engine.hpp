#pragma once

#include <rclcpp/rclcpp.hpp>
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp"
#include "rclcpp_lifecycle/lifecycle_publisher.hpp"

#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <string>
#include <memory>

namespace voice_toolbox
{
    using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

    template <typename ServiceT>
    class ASR_Base : public rclcpp_lifecycle::LifecycleNode
    {
    public:
        /**
         * @brief 构造函数
         * @param node_name 节点名称
         * @param options ROS2节点选项
         */
        explicit ASR_Base(
            const std::string &node_name,
            const std::string &asr_service_name,
            const rclcpp::NodeOptions &options = rclcpp::NodeOptions())
            : rclcpp_lifecycle::LifecycleNode(node_name, options),
              service_name_(asr_service_name)
        {
        }

        virtual ~ASR_Base() = default;

        /**
         * @brief 通用服务回调函数（适配任意符合约束的ServiceT）
         * @param request 模板化的服务请求（ServiceT::Request）
         * @param response 模板化的服务响应（ServiceT::Response）
         */
        virtual void handle_service_request(const std::shared_ptr<typename ServiceT::Request> request, std::shared_ptr<typename ServiceT::Response> response) = 0;

    protected:
        CallbackReturn on_configure(const rclcpp_lifecycle::State & /*state*/) override
        {
            return CallbackReturn::SUCCESS;
        }

        CallbackReturn on_activate(const rclcpp_lifecycle::State & state) override
        {
            recognize_service_ = this->create_service<ServiceT>(
                service_name_,
                std::bind(&ASR_Base<ServiceT>::handle_service_request, this, std::placeholders::_1, std::placeholders::_2));
            return rclcpp_lifecycle::LifecycleNode::on_activate(state);
        }

        CallbackReturn on_deactivate(const rclcpp_lifecycle::State & state) override
        {
            if (recognize_service_)
            {
                recognize_service_.reset();
            }
            return rclcpp_lifecycle::LifecycleNode::on_deactivate(state);
        }

        CallbackReturn on_cleanup(const rclcpp_lifecycle::State & /*state*/) override
        {
            recognize_service_.reset();
            return CallbackReturn::SUCCESS;
        }

        CallbackReturn on_shutdown(const rclcpp_lifecycle::State & /*state*/) override
        {
            recognize_service_.reset();
            return CallbackReturn::SUCCESS;
        }

    private:
        std::shared_ptr<rclcpp::Service<ServiceT>> recognize_service_;
        std::string service_name_;
    };
}
