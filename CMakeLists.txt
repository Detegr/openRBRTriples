cmake_minimum_required(VERSION 3.5)
set(CMAKE_SYSTEM_VERSION 123)

project(openRBRTriples)

set(CMAKE_CXX_STANDARD 23)

set(SOURCES
    "src/API.cpp"
    "src/Dx.cpp"
    "src/Globals.cpp"
    "src/Menu.cpp"
    "src/RBR.cpp"
    "src/RenderTarget.cpp"
    "src/UI.cpp"
    "src/openRBRTriples.cpp"

    "thirdparty/imgui-1.91.5/imgui.cpp"
    "thirdparty/imgui-1.91.5/imgui_draw.cpp"
    "thirdparty/imgui-1.91.5/imgui_tables.cpp"
    "thirdparty/imgui-1.91.5/imgui_widgets.cpp"
    "thirdparty/imgui-1.91.5/backends/imgui_impl_dx9.cpp"
    "thirdparty/imgui-1.91.5/backends/imgui_impl_win32.cpp"
)

set(HEADERS
    "src/Config.hpp"
    "src/D3D.hpp"
    "src/Dx.hpp"
    "src/Font.hpp"
    "src/Globals.hpp"
    "src/Hook.hpp"
    "src/IPlugin.h"
    "src/Licenses.hpp"
    "src/Menu.hpp"
    "src/RBR.hpp"
    "src/RenderTarget.hpp"
    "src/UI.hpp"
    "src/Util.hpp"
    "src/openRBRTriples.def"
    "src/openRBRTriples.hpp"

    "thirdparty/imgui-1.91.5/imgui.h"
    "thirdparty/inicpp.h"
)

set(openRBRTriples_Major 0)
set(openRBRTriples_Minor 3)
set(openRBRTriples_Patch 2)
set(openRBRTriples_Tweak 0)
#set(openRBRTriples_TweakStr "-beta${openRBRTriples_Tweak}")

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  @ONLY)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/Version.hpp
  @ONLY
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

#set(CMAKE_C_COMPILER "C:\\Program Files\\bin\\clang.exe")
#set(CMAKE_CXX_COMPILER "C:\\Program Files\\bin\\clang++.exe")
#set_source_files_properties("src/UI.cpp" PROPERTIES COMPILE_FLAGS "/MDd")
target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX)
#target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32 _WINDOWS _USRDLL)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/thirdparty"
    "${CMAKE_SOURCE_DIR}/thirdparty/glm"
    "${CMAKE_SOURCE_DIR}/thirdparty/minhook/include"
    "${CMAKE_SOURCE_DIR}/thirdparty/imgui-1.91.5"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_SOURCE_DIR}/thirdparty/lib
  ${CMAKE_SOURCE_DIR}/thirdparty/minhook/bin
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  d3d9
  libminhook.x86
)

cmake_path(NATIVE_PATH CMAKE_INSTALL_PREFIX NATIVE_CMAKE_INSTALL_PREFIX)

set_target_properties(${PROJECT_NAME}
PROPERTIES
	VS_DEBUGGER_COMMAND "${NATIVE_CMAKE_INSTALL_PREFIX}\\RichardBurnsRally_SSE.exe"
	VS_DEBUGGER_COMMAND_ARGUMENTS "$<TARGET_FILE:${PROJECT_NAME}>"
)

# Set the output directory for Windows
set_target_properties(${PROJECT_NAME} PROPERTIES
	ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
	LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
	PDB_OUTPUT_DIRECTORY_DEBUG     "${CMAKE_BINARY_DIR}/Release"
	ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
	LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
	PDB_OUTPUT_DIRECTORY_RELEASE     "${CMAKE_BINARY_DIR}/Release"
)

add_custom_target(fmt
    COMMAND clang-format -i ${SOURCES} ${HEADERS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(build_and_copy
    COMMAND copy ${CMAKE_BUILD_TYPE}\\openRBRTriples.dll ${NATIVE_CMAKE_INSTALL_PREFIX}\\Plugins
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(build_and_copy ${PROJECT_NAME})
