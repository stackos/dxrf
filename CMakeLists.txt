cmake_minimum_required(VERSION 3.8.2)

project(dxrf)

get_filename_component(DXRF_SRC_DIR
                       ${CMAKE_SOURCE_DIR}/src
                       ABSOLUTE)

file(GLOB DXRF_SRCS
     ${DXRF_SRC_DIR}/*.h
     ${DXRF_SRC_DIR}/*.cpp
     )

file(GLOB DXRF_SHADER_SRCS
     ${DXRF_SRC_DIR}/*.hlsl
     )

set_property(SOURCE ${DXRF_SHADER_SRCS} PROPERTY VS_SHADER_ENTRYPOINT " ")
set_property(SOURCE ${DXRF_SHADER_SRCS} PROPERTY VS_SHADER_TYPE Library)
set_property(SOURCE ${DXRF_SHADER_SRCS} PROPERTY VS_SHADER_MODEL 6.3)
set_property(SOURCE ${DXRF_SHADER_SRCS} PROPERTY VS_SHADER_VARIABLE_NAME "g_p%(Filename)")
set_property(SOURCE ${DXRF_SHADER_SRCS} PROPERTY VS_SHADER_OUTPUT_HEADER_FILE "$(IntDir)\\CompiledShaders\\%(Filename).hlsl.h")
set_property(SOURCE ${DXRF_SHADER_SRCS} PROPERTY VS_SHADER_FLAGS "/Zpr %(AdditionalOptions)")

source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${DXRF_SRCS})
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${DXRF_SHADER_SRCS})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDXRF_WINDOWS -W3 -D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_C_FLAGS}")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

add_executable(dxrf
               ${DXRF_SRCS}
               ${DXRF_SHADER_SRCS}
               )

target_include_directories(dxrf PRIVATE
                           ${DXRF_SRC_DIR}
                           ${CMAKE_BINARY_DIR}\\$(IntDir)
                           )

target_link_libraries(dxrf
                      winmm.lib
                      d3d12.lib
                      dxgi.lib
                      dxguid.lib
                      )

set_property(TARGET dxrf PROPERTY LINK_FLAGS "/SUBSYSTEM:WINDOWS")

string(REPLACE "/" "\\" BIN_DIR ${PROJECT_BINARY_DIR}/$(Configuration))
string(REPLACE "/" "\\" ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)

add_custom_command(TARGET dxrf
                   POST_BUILD
                   COMMAND xcopy ${ASSETS_DIR} ${BIN_DIR}\\assets\\ /s /d /y
                   )

set_property(TARGET dxrf PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${BIN_DIR}")
