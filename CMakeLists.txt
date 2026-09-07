cmake_minimum_required(VERSION 3.16)
project(aetherion LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

add_library(aetherion STATIC
  engine/src/core/JobSystem.cpp
  engine/src/core/Streaming.cpp
  engine/src/render/VirtualGeometry.cpp
  engine/src/render/MeshMerger.cpp
  engine/src/render/GIVolume.cpp
  engine/src/render/FrameGraph.cpp
  engine/src/world/ScaleGraph.cpp
  engine/src/world/SolarSystem.cpp
  engine/src/procgen/BiomeGraph.cpp
  engine/src/procgen/Ocean.cpp
  engine/src/physics/Newtonian.cpp
  engine/src/gameplay/FirstPerson.cpp
  engine/src/ui/HolographicMap.cpp
)
target_include_directories(aetherion PUBLIC engine/include)

add_executable(aetherion_probe engine/src/kernel_probe.cpp)
target_link_libraries(aetherion_probe PRIVATE aetherion)
target_include_directories(aetherion_probe PRIVATE engine/include)

enable_testing()
add_test(NAME kernel_probe COMMAND aetherion_probe)
