#include "voice_toolbox/audio_player.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <filesystem>
#include <rclcpp_components/register_node_macro.hpp>

namespace AudioPlayer
{

  AudioPlayer::AudioPlayer(const rclcpp::NodeOptions &options) : Node("audio_player_node", options)
  {
    declare_parameter<int>("sample_rate", 44100);
    declare_parameter<bool>("DEBUG_MODE_", false);
    get_parameter("sample_rate", sample_rate_);
    get_parameter("DEBUG_MODE_", DEBUG_MODE_);

    // 初始化SDL音频
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
      throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));

    if (Mix_OpenAudio(sample_rate_, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
    {
      SDL_Quit();
      throw std::runtime_error("Mix_OpenAudio failed: " + std::string(Mix_GetError()));
    }

    play_sub_ = create_subscription<std_msgs::msg::String>(
        "/voice_toolbox/play_audio", 5,
        std::bind(&AudioPlayer::topicCallback, this, std::placeholders::_1));
        
    action_server_ = rclcpp_action::create_server<PlayAudio>(
        this,
        "/voice_toolbox/play_action",
        std::bind(&AudioPlayer::handleGoal, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&AudioPlayer::handleCancel, this, std::placeholders::_1),
        std::bind(&AudioPlayer::handleAccepted, this, std::placeholders::_1));

    audio_thread_ = std::thread(&AudioPlayer::audioLoop, this);
    RCLCPP_INFO(get_logger(), "AudioPlayer  started successfully");
  }

  AudioPlayer::~AudioPlayer()
  {
    running_ = false;
    queue_cv_.notify_all();
    if (audio_thread_.joinable())
      audio_thread_.join();

    if (current_music_)
    {
      Mix_HaltMusic();
      Mix_FreeMusic(current_music_);
      current_music_ = nullptr;
    }
    Mix_CloseAudio();
    SDL_Quit();
  }

  void AudioPlayer::topicCallback(const std_msgs::msg::String::SharedPtr msg)
  {
    if (!std::filesystem::exists(msg->data))
    {
      RCLCPP_ERROR(get_logger(), "Reject goal: File %s does not exist", msg->data.c_str());
      return;
    }
    pushCommand({msg->data, 1.0f});
  }

  rclcpp_action::GoalResponse AudioPlayer::handleGoal(const rclcpp_action::GoalUUID & /*uuid*/, std::shared_ptr<const PlayAudio::Goal> goal)
  {
    // 检查文件是否存在
    if (!std::filesystem::exists(goal->file_path))
    {
      RCLCPP_ERROR(get_logger(), "Reject goal: File %s does not exist", goal->file_path.c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }

    RCLCPP_DEBUG(get_logger(), "Accept goal for file: %s", goal->file_path.c_str());
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse
  AudioPlayer::handleCancel(const std::shared_ptr<GoalHandlePlayAudio> /*goal_handle*/)
  {
    if (DEBUG_MODE_) RCLCPP_INFO(get_logger(), "Received cancel request for audio playback");
    cancel_requested_ = true;
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void AudioPlayer::handleAccepted(const std::shared_ptr<GoalHandlePlayAudio> goal_handle)
  {
    // 保存活跃的goal并重置取消标志
    {
      std::lock_guard<std::mutex> lock(goal_mutex_);
      active_goal_ = goal_handle;
      cancel_requested_ = false;
    }

    auto goal = goal_handle->get_goal();
    pushCommand({goal->file_path, goal->volume});
  }

  void AudioPlayer::pushCommand(const Command &cmd)
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      command_queue_.push(cmd);
    }
    queue_cv_.notify_one();
  }

  void AudioPlayer::audioLoop()
  {
    while (running_)
    {
      Command cmd;

      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [&]
                       { return !command_queue_.empty() || !running_; });
        if (!running_)
          break;
        cmd = command_queue_.front();
        command_queue_.pop();
      }
      cancel_requested_ = false;
      if (current_music_)
      {
        Mix_HaltMusic();
        Mix_FreeMusic(current_music_);
        current_music_ = nullptr;
      }

      current_music_ = Mix_LoadMUS(cmd.file.c_str());
      if (!current_music_)
      {
        RCLCPP_ERROR(get_logger(), "Failed to load audio file: %s (SDL error: %s)",cmd.file.c_str(), Mix_GetError());
        finishGoal(false, "Failed to load audio file: " + cmd.file);
        continue;
      }

      // 设置音量并播放
      Mix_VolumeMusic(static_cast<int>(cmd.volume * MIX_MAX_VOLUME));
      if (Mix_PlayMusic(current_music_, 1) == -1)
      {
        RCLCPP_ERROR(get_logger(), "Failed to play audio file: %s (SDL error: %s)",cmd.file.c_str(), Mix_GetError());
        Mix_FreeMusic(current_music_);
        current_music_ = nullptr;
        finishGoal(false, "Failed to play audio file: " + cmd.file);
        continue;
      }

      if (DEBUG_MODE_) RCLCPP_INFO(get_logger(), "Playing audio: %s (volume: %.2f)",cmd.file.c_str(), cmd.volume);

      while (Mix_PlayingMusic())
      {
        if (!running_ || cancel_requested_)
          break;
        SDL_Delay(100); 
      }

      Mix_HaltMusic();
      Mix_FreeMusic(current_music_);
      current_music_ = nullptr;

      if (cancel_requested_)
      {
        finishGoal(false, "Playback canceled by user");
      }
      else
      {
        finishGoal(true, "Playback completed successfully");
      }
    }
  }

  void AudioPlayer::finishGoal(bool success, const std::string &msg)
  {
    std::shared_ptr<GoalHandlePlayAudio> goal;

    {
      std::lock_guard<std::mutex> lock(goal_mutex_);
      goal = active_goal_;
      active_goal_.reset();
    }
    // 如果没有活跃的goal（比如是通过topic触发的播放），直接返回
    if (!goal)
      return;

    // 构建结果并响应
    auto result = std::make_shared<PlayAudio::Result>();
    result->success = success;
    result->message = msg;

    if (success)
    {
      goal->succeed(result);
    }
    else
    {
      // 如果是取消请求，标记为canceled，否则标记为abort
      if (cancel_requested_)
        goal->canceled(result);
      else
        goal->abort(result);
    }
  }

} // namespace AudioPlayer

RCLCPP_COMPONENTS_REGISTER_NODE(AudioPlayer::AudioPlayer)