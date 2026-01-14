from launch import LaunchDescription
from launch_ros.actions import Node
from launch_ros.actions import LifecycleNode
from launch.actions import (DeclareLaunchArgument, EmitEvent, LogInfo,RegisterEventHandler)
from launch_ros.events.lifecycle import ChangeState
from lifecycle_msgs.msg import Transition
from launch.events import matches_action
from launch_ros.event_handlers import OnStateTransition
import os
from ament_index_python.packages import get_package_share_directory
def generate_launch_description():
    config_file=os.path.join(get_package_share_directory('voice_toolbox'),'config','voice_toolbox_setting.yaml')
    asr_serive = LifecycleNode(
          package='voice_toolbox',
          executable='asr_sherpa',
          name='asr_sherpa',  
          output='screen',
          parameters=[{ 'config_file':config_file}],
          namespace=''
    )
    configure_event = EmitEvent(
        event=ChangeState(
            lifecycle_node_matcher=matches_action(asr_serive),
            transition_id=Transition.TRANSITION_CONFIGURE
        ),
    )
    activate_event = RegisterEventHandler(
        OnStateTransition(
            target_lifecycle_node=asr_serive,
            start_state="configuring",
            goal_state="inactive",
            entities=[
                EmitEvent(event=ChangeState(
                    lifecycle_node_matcher=matches_action(asr_serive),
                    transition_id=Transition.TRANSITION_ACTIVATE
                ))
            ]
        ),
    )
    return LaunchDescription([
        asr_serive,
        configure_event,
        activate_event
    ])