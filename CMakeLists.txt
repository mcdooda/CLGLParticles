cmake_minimum_required(VERSION 3.6)

project(CLGLParticles)

include_directories(
    include
)

link_directories(
    ${CMAKE_SOURCE_DIR}/lib
)

file(
    GLOB_RECURSE
    src
    src/*
)

#black magic, ask Tom
foreach(_source IN ITEMS ${src})
    if (IS_ABSOLUTE "${_source}")
        file(RELATIVE_PATH _source_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_source}")
    else()
        set(source_rel "${_source}")
    endif()
    get_filename_component(_source_path "${_source_rel}" PATH)
    string(REPLACE "/" "\\" _source_path_msvc "${_source_path}")
    source_group("${_source_path_msvc}" FILES "${_source}")
endforeach()

add_executable(
    CLGLParticles
    ${src}
)

target_link_libraries(
    CLGLParticles
    SDL2main
    SDL2
    SDL2_image
    OpenCL
    opengl32
    glew32
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT CLGLParticles)
set_property(TARGET CLGLParticles PROPERTY CXX_STANDARD 17)

