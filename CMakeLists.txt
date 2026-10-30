cmake_minimum_required(VERSION 3.16)
project(glsl_sksl_transpiler VERSION 0.1.0 LANGUAGES CXX)
# Used through add_subdirectory (for example as a Git submodule), this file builds only the
# library glsl_sksl::glsl_sksl; the command-line tool (cli/) and the tests (tests/) are built
# when this is the top-level project, or when their options are turned on.
set(GLSL_SKSL_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(GLSL_SKSL_TOP_LEVEL ON)
endif()
option(GLSL_SKSL_BUILD_TOOLS "Build the glsl-to-sksl command-line tool" ${GLSL_SKSL_TOP_LEVEL})
option(GLSL_SKSL_BUILD_TESTS "Build the tests" ${GLSL_SKSL_TOP_LEVEL})
option(GLSL_SKSL_INSTALL "Generate the install and package export rules" ${GLSL_SKSL_TOP_LEVEL})

add_library(glsl_sksl STATIC
  src/ast.cpp src/builtins.cpp src/language.cpp src/lexer.cpp src/parser.cpp src/preprocessor.cpp src/sema.cpp
  src/emitter.cpp src/helpers.cpp src/transpiler.cpp src/types.cpp)
add_library(glsl_sksl::glsl_sksl ALIAS glsl_sksl)
target_include_directories(glsl_sksl PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(glsl_sksl PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(glsl_sksl PRIVATE /W4 /permissive-)
else()
  target_compile_options(glsl_sksl PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(GLSL_SKSL_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS glsl_sksl EXPORT glsl_sksl_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT glsl_sksl_targets NAMESPACE glsl_sksl:: FILE glsl_sksl-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/glsl_sksl)
endif()

if(GLSL_SKSL_BUILD_TOOLS)
  add_subdirectory(cli)
  # Next to the build's other outputs, where the scripts and documentation expect it.
  set_target_properties(glsl-to-sksl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
if(GLSL_SKSL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
