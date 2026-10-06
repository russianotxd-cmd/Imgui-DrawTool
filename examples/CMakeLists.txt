if(NOT DRAWTOOL_IMGUI_SRC)
  message(WARNING "drawtool demo needs ImGui backend sources; set DRAWTOOL_IMGUI_DIR or let drawtool fetch ImGui. Skipping demo.")
  return()
endif()

find_package(OpenGL REQUIRED)

find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(glfw)
endif()

add_executable(drawtool_demo
  demo_glfw.cpp
  ${DRAWTOOL_IMGUI_SRC}/backends/imgui_impl_glfw.cpp
  ${DRAWTOOL_IMGUI_SRC}/backends/imgui_impl_opengl3.cpp)
target_include_directories(drawtool_demo PRIVATE ${DRAWTOOL_IMGUI_SRC}/backends)
target_link_libraries(drawtool_demo PRIVATE drawtool glfw OpenGL::GL ${CMAKE_DL_LIBS})
