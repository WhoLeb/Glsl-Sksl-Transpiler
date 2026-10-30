# Tests of the library; added by the top-level project when GLSL_SKSL_BUILD_TESTS is on.
add_executable(transpiler-tests transpiler_tests.cpp)
target_link_libraries(transpiler-tests PRIVATE glsl_sksl::glsl_sksl)
add_test(NAME transpiler COMMAND transpiler-tests)

# Its own copy of the library: under MSVC's iterator debugging the standard containers' noexcept
# constructors allocate and terminate on failure, which would end the allocation sweep inside
# the standard library rather than test the library's own handling.
get_target_property(GLSL_SKSL_SOURCES glsl_sksl SOURCES)
get_target_property(GLSL_SKSL_SOURCE_DIR glsl_sksl SOURCE_DIR)
list(TRANSFORM GLSL_SKSL_SOURCES PREPEND "${GLSL_SKSL_SOURCE_DIR}/")
add_executable(resilience-tests resilience_tests.cpp ${GLSL_SKSL_SOURCES})
target_include_directories(resilience-tests PRIVATE ${GLSL_SKSL_SOURCE_DIR}/include)
target_compile_features(resilience-tests PRIVATE cxx_std_17)
if(MSVC)
  target_compile_definitions(resilience-tests PRIVATE _ITERATOR_DEBUG_LEVEL=0)
endif()
add_test(NAME resilience COMMAND resilience-tests)

# Front-end tests use the internal headers in src/.
add_executable(frontend-tests frontend_tests.cpp)
target_include_directories(frontend-tests PRIVATE ${GLSL_SKSL_SOURCE_DIR}/src)
target_link_libraries(frontend-tests PRIVATE glsl_sksl::glsl_sksl)
add_test(NAME frontend COMMAND frontend-tests)
add_executable(sema-tests sema_tests.cpp)
target_include_directories(sema-tests PRIVATE ${GLSL_SKSL_SOURCE_DIR}/src)
target_link_libraries(sema-tests PRIVATE glsl_sksl::glsl_sksl)
add_test(NAME sema COMMAND sema-tests)

# The corpus runs the command-line tool.
find_package(Python3 COMPONENTS Interpreter)
option(GLSL_SKSL_COMPILER_TESTS "Enable strict Skia compiler-backed corpus" OFF)
if(TARGET glsl-to-sksl AND Python3_Interpreter_FOUND)
  add_test(NAME fragment-conversions COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/run_corpus.py --transpiler $<TARGET_FILE:glsl-to-sksl>
    --allow-missing-skia --artifacts ${CMAKE_BINARY_DIR}/conversion-results)
endif()
if(Python3_Interpreter_FOUND)
  add_test(NAME validation-harness COMMAND ${Python3_EXECUTABLE} -m unittest discover
    -s ${CMAKE_CURRENT_SOURCE_DIR} -p test_harness.py)
endif()
if(GLSL_SKSL_COMPILER_TESTS)
  if(NOT Python3_Interpreter_FOUND OR NOT TARGET glsl-to-sksl)
    message(FATAL_ERROR "The compiler corpus requires Python3 and the glsl-to-sksl tool (GLSL_SKSL_BUILD_TOOLS)")
  endif()
  add_test(NAME fragment-corpus COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/run_corpus.py --transpiler $<TARGET_FILE:glsl-to-sksl>
    --artifacts ${CMAKE_BINARY_DIR}/corpus-results)
endif()
