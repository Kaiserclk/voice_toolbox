cmake_minimum_required(VERSION 4.1)
project(voice_toolbox)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
# uncomment the following section in order to fill in
# further dependencies manually.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)

find_package(rosidl_default_generators REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_msgs REQUIRED)

# 生成action接口
rosidl_generate_interfaces(${PROJECT_NAME}
  "action/PlayAudio.action"
)

# 设置需要依赖生成action的库
set(dependencies
  rclcpp
  rclcpp_action
  std_msgs
  rclcpp_components
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

add_library(audio_player_component SHARED src/audio_player.cpp)

# Link SDL2 libraries
target_link_libraries(audio_player_component 
  ${SDL2_LIBRARIES}
  ${SDL2_MIXER_LIBRARIES}
)

# Include directories
target_include_directories(audio_player_component PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_MIXER_INCLUDE_DIRS}
  include  # 添加头文件目录
)

ament_target_dependencies(audio_player_component ${dependencies})

# 添加对生成接口的依赖
rosidl_target_interfaces(audio_player_component
  ${PROJECT_NAME} 
  "rosidl_typesupport_cpp")

rclcpp_components_register_node(
    audio_player_component
    PLUGIN "AudioPlayer::AudioPlayer"
    EXECUTABLE AudioPlayer
)

ament_export_targets(export_audio_player_component_component)
install(TARGETS audio_player_component
        EXPORT export_audio_player_component_component
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)

install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()