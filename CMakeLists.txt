cmake_minimum_required(VERSION 3.14)
project(ImGui_GLFW_OpenGL3_Example)

# Enable export commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# Fetch GL3W
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
)
FetchContent_MakeAvailable(glad)

# Fetch ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.4
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SRC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Download and setup litehtml
FetchContent_Declare(
  litehtml
  GIT_REPOSITORY https://github.com/litehtml/litehtml.git
  GIT_TAG d4453f5d4e03cd4d902b867ca553d0ad81b09939
)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "Skip building tests" FORCE)
FetchContent_MakeAvailable(litehtml)

if(TARGET litehtml)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(litehtml PRIVATE
      -Wno-error
      -Wno-reorder-ctor
      -Wno-switch
    )
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(litehtml PRIVATE -Wno-error -Wno-reorder -Wno-switch)
  endif()
endif()

add_executable(imhtml ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/imhtml.cpp ${IMGUI_SRC})

# Include directories
target_include_directories(imhtml PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${glfw_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${glad_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(imhtml PRIVATE glfw OpenGL::GL litehtml glad)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Define IMHTML_DEBUG_PRINTF
target_compile_definitions(imhtml PRIVATE IMHTML_DEBUG_PRINTF)

# On macOS, link frameworks
if(APPLE)
    target_link_libraries(imhtml PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()
