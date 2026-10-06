cmake_minimum_required(VERSION 3.16)
project(ImguiDrawTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(DRAWTOOL_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(DRAWTOOL_TOP_LEVEL ON)
endif()

option(DRAWTOOL_BUILD_DEMO  "Build the GLFW + OpenGL3 demo"  ${DRAWTOOL_TOP_LEVEL})
option(DRAWTOOL_BUILD_BENCH "Build the headless benchmark"  ${DRAWTOOL_TOP_LEVEL})
option(DRAWTOOL_BUILD_TESTS "Build unit tests"              ${DRAWTOOL_TOP_LEVEL})
set(DRAWTOOL_IMGUI_DIR "" CACHE PATH "Existing Dear ImGui source dir (empty = download)")

# ---------------------------------------------------------------------------
# Dear ImGui. If the parent project already defines an `imgui` target we use
# it as-is; otherwise build one from DRAWTOOL_IMGUI_DIR or a fetched copy.
# ---------------------------------------------------------------------------
if(NOT TARGET imgui)
  if(DRAWTOOL_IMGUI_DIR)
    set(IMGUI_SRC ${DRAWTOOL_IMGUI_DIR})
  else()
    include(FetchContent)
    FetchContent_Declare(imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG        v1.92.9
      GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_SRC ${imgui_SOURCE_DIR})
  endif()

  add_library(imgui STATIC
    ${IMGUI_SRC}/imgui.cpp
    ${IMGUI_SRC}/imgui_draw.cpp
    ${IMGUI_SRC}/imgui_tables.cpp
    ${IMGUI_SRC}/imgui_widgets.cpp
    ${IMGUI_SRC}/imgui_demo.cpp)
  target_include_directories(imgui PUBLIC ${IMGUI_SRC})
  set(DRAWTOOL_IMGUI_SRC ${IMGUI_SRC} CACHE INTERNAL "")
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(drawtool STATIC
  src/BoxStore.cpp
  src/BoxRenderer.cpp
  src/DrawTool.cpp
  src/DebugUI.cpp)
add_library(drawtool::drawtool ALIAS drawtool)
target_include_directories(drawtool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(drawtool PUBLIC imgui)

if(MSVC)
  target_compile_options(drawtool PRIVATE /W4 $<$<CONFIG:Release>:/O2 /fp:fast>)
else()
  target_compile_options(drawtool PRIVATE -Wall -Wextra $<$<CONFIG:Release>:-O3>)
endif()

if(DRAWTOOL_BUILD_BENCH)
  add_executable(drawtool_bench bench/bench_headless.cpp)
  target_link_libraries(drawtool_bench PRIVATE drawtool)
endif()

if(DRAWTOOL_BUILD_TESTS)
  enable_testing()
  add_executable(drawtool_tests tests/tests.cpp)
  target_link_libraries(drawtool_tests PRIVATE drawtool)
  add_test(NAME drawtool_tests COMMAND drawtool_tests)
endif()

if(DRAWTOOL_BUILD_DEMO)
  add_subdirectory(examples)
endif()
