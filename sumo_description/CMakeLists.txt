cmake_minimum_required(VERSION 3.8)
project(sumo_description)

find_package(ament_cmake REQUIRED)

# urdf, meshes, launch, rviz 폴더를 설치 폴더(share/sumo_description)로 복사
install(
  DIRECTORY urdf meshes launch rviz
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
