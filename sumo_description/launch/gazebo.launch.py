# Gazebo(Harmonic)에서 씨름로봇 물리 시뮬레이션
# 실행: ros2 launch sumo_description gazebo.launch.py
# 조종(새 터미널): ros2 run teleop_twist_keyboard teleop_twist_keyboard
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import AppendEnvironmentVariable, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node


def generate_launch_description():
    pkg = get_package_share_directory('sumo_description')

    with open(os.path.join(pkg, 'urdf', 'sumo.urdf'), 'r', encoding='utf-8') as f:
        robot_desc = f.read()

    # Gazebo가 package://sumo_description/... 경로의 STL을 찾을 수 있게
    # 패키지들이 설치된 share 폴더를 Gazebo 검색 경로에 추가
    gz_resource_path = AppendEnvironmentVariable(
        'GZ_SIM_RESOURCE_PATH', os.path.dirname(pkg))

    # 빈 월드(바닥 + 조명)로 Gazebo 켜기. -r = 켜자마자 시뮬레이션 시작
    gz_sim = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(
            get_package_share_directory('ros_gz_sim'), 'launch', 'gz_sim.launch.py')),
        launch_arguments={'gz_args': '-r empty.sdf'}.items(),
    )

    # URDF -> 부품 위치(TF) 계산. use_sim_time = Gazebo 시계 사용
    robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        parameters=[{'robot_description': robot_desc, 'use_sim_time': True}],
        output='screen',
    )

    # robot_description 토픽의 URDF로 Gazebo 안에 로봇 생성
    # 바퀴 반지름이 0.05 m라서 살짝 위(0.06 m)에서 떨어뜨림
    spawn = Node(
        package='ros_gz_sim',
        executable='create',
        arguments=['-topic', 'robot_description', '-name', 'sumo', '-z', '0.06'],
        output='screen',
    )

    # ROS 2 <-> Gazebo 메시지 다리
    #   ]  : ROS -> Gazebo (조종 명령)
    #   [  : Gazebo -> ROS (위치, 관절 각도, 시계)
    bridge = Node(
        package='ros_gz_bridge',
        executable='parameter_bridge',
        arguments=[
            '/cmd_vel@geometry_msgs/msg/Twist]gz.msgs.Twist',
            '/odom@nav_msgs/msg/Odometry[gz.msgs.Odometry',
            '/joint_states@sensor_msgs/msg/JointState[gz.msgs.Model',
            '/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock',
        ],
        output='screen',
    )

    return LaunchDescription([
        gz_resource_path,
        gz_sim,
        robot_state_publisher,
        spawn,
        bridge,
    ])
