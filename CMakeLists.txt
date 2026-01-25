cmake_minimum_required(VERSION 4.1)
project(voice_toolbox)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)

find_package(rosidl_default_generators REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)

find_package(yaml-cpp REQUIRED)
find_package(websocketpp REQUIRED)

# 为sherpa-onnx设置路径
set(SHERPA_ONNX_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sherpa_lib/x86)
set(SHERPA_ONNX_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/sherpa-onnx)

# 生成消息接口
rosidl_generate_interfaces(${PROJECT_NAME}
  "action/PlayAudio.action"
  "interfaces/srv/OneShot.srv"
)

# 设置需要依赖生成action的库
set(dependencies
  rclcpp
  rclcpp_action
  std_msgs
  rclcpp_components
)

set(asr_dependencies
  rclcpp
  std_msgs
  rclcpp_components
  rclcpp_lifecycle
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


#音频驱动
add_library(audio_player_component SHARED src/audio_player.cpp)
#ASR服务
add_library(offline_asr_component SHARED src/asr/offline_asr.cpp src/asr/asr-websocket.cpp)

# 设置库为POSITION_INDEPENDENT_CODE以支持链接静态库
set_property(TARGET offline_asr_component PROPERTY POSITION_INDEPENDENT_CODE ON)

# Link SDL2 libraries
target_link_libraries(audio_player_component 
  ${SDL2_LIBRARIES}
  ${SDL2_MIXER_LIBRARIES}
)


# Include directories
target_include_directories(audio_player_component PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_MIXER_INCLUDE_DIRS}
  include  
)

# Include directories for asr library - adding sherpa_lib include paths
target_include_directories(offline_asr_component PRIVATE
  include
  ${SHERPA_ONNX_INCLUDE_DIR}  # 添加sherpa-onnx的头文件路径
  ${yaml-cpp_INCLUDE_DIRS}
)

# 添加RPATH设置，确保运行时能找到动态库
set_target_properties(offline_asr_component PROPERTIES
  BUILD_WITH_INSTALL_RPATH TRUE
  INSTALL_RPATH_USE_LINK_PATH TRUE
  INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib:${SHERPA_ONNX_LIB_DIR}"
)

ament_target_dependencies(audio_player_component ${dependencies})

ament_target_dependencies(offline_asr_component ${asr_dependencies})
target_link_libraries(offline_asr_component
  websocketpp::websocketpp
  ${YAML_CPP_LIBRARIES}
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-c-api.so      # sherpa-onnx C API库
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-cxx-api.so    # sherpa-onnx C++ API库
  ${SHERPA_ONNX_LIB_DIR}/libonnxruntime.so            # onnxruntime库
)

rosidl_get_typesupport_target(cpp_typesupport_target_audio "${PROJECT_NAME}" "rosidl_typesupport_cpp")
target_link_libraries(audio_player_component "${cpp_typesupport_target_audio}")

rosidl_get_typesupport_target(cpp_typesupport_target_asr "${PROJECT_NAME}" "rosidl_typesupport_cpp")
target_link_libraries(offline_asr_component "${cpp_typesupport_target_asr}")

# 注册节点
rclcpp_components_register_node(
    audio_player_component
    PLUGIN "AudioPlayer::AudioPlayer"
    EXECUTABLE AudioPlayer
)
rclcpp_components_register_node(
    offline_asr_component
    PLUGIN "voice_toolbox::Offline_ASR"
    EXECUTABLE offline_asr
)


#
ament_export_targets(export_audio_player_component)
install(TARGETS audio_player_component
        EXPORT export_audio_player_component
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)
ament_export_targets(export_offline_asr_component)
install(TARGETS offline_asr_component
        EXPORT export_offline_asr_component
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)


# 安装配置文件
install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()