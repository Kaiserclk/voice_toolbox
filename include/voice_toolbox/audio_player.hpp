#pragma once

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <std_msgs/msg/string.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <string>

#include "voice_toolbox/action/play_audio.hpp"

namespace AudioPlayer
{
  using PlayAudio = voice_toolbox::action::PlayAudio;
  using GoalHandlePlayAudio = rclcpp_action::ServerGoalHandle<PlayAudio>;

  struct Command
  {
    std::string file;
    float volume{1.0f};
  };

  class AudioPlayer : public rclcpp::Node
  {
  public:
    explicit AudioPlayer(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
    ~AudioPlayer() override;

  private:
    // 回调函数
    void topicCallback(const std_msgs::msg::String::SharedPtr msg);
    rclcpp_action::GoalResponse handleGoal(const rclcpp_action::GoalUUID &, std::shared_ptr<const PlayAudio::Goal> goal);
    rclcpp_action::CancelResponse handleCancel(const std::shared_ptr<GoalHandlePlayAudio> goal_handle);
    void handleAccepted(const std::shared_ptr<GoalHandlePlayAudio> goal_handle);

    // 核心逻辑
    void audioLoop();
    void pushCommand(const Command &cmd);
    void finishGoal(bool success, const std::string &msg);

    // ROS相关
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr play_sub_;
    rclcpp_action::Server<PlayAudio>::SharedPtr action_server_;
    int sample_rate_;
    bool DEBUG_MODE_;

    std::thread audio_thread_;
    std::atomic_bool running_{true};
    std::atomic_bool cancel_requested_{false};
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::mutex goal_mutex_;
    std::queue<Command> command_queue_;

    // 音频相关
    Mix_Music *current_music_{nullptr};
    std::shared_ptr<GoalHandlePlayAudio> active_goal_;
  };

}