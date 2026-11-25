# RViz에서 로봇 모양 확인하기 (물리 계산 없음)
# 실행: ros2 launch sumo_description display.launch.py
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # 설치된 sumo_description 패키지 폴더 위치 찾기
    pkg = get_package_share_directory('sumo_description')

    # URDF 파일 내용을 문자열로 읽기
    with open(os.path.join(pkg, 'urdf', 'sumo.urdf'), 'r', encoding='utf-8') as f:
        robot_desc = f.read()

    return LaunchDescription([
        # URDF를 읽어서 각 부품(링크)의 위치(TF)를 계산해 알려주는 노드
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            parameters=[{'robot_description': robot_desc}],
        ),
        # 바퀴 관절을 슬라이더로 돌려볼 수 있는 작은 창
        Node(
            package='joint_state_publisher_gui',
            executable='joint_state_publisher_gui',
        ),
        # 3D 화면 (설정 파일 rviz/display.rviz 사용)
        Node(
            package='rviz2',
            executable='rviz2',
            arguments=['-d', os.path.join(pkg, 'rviz', 'display.rviz')],
        ),
    ])
