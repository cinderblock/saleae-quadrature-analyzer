cmake_minimum_required (VERSION 3.13)

project(QuadratureEncoderAnalyzer)

add_definitions( -DLOGIC2 )

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum supported MacOS version" FORCE)

# enable generation of compile_commands.json, helpful for IDEs to locate include files.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# custom CMake Modules are located in the cmake directory.
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(ExternalAnalyzerSDK)

# ExternalAnalyzerSDK selects C++11; the decoder is written against C++17.
set(CMAKE_CXX_STANDARD 17)

# The decoder itself has no dependency on the Analyzer SDK, so it can be unit tested on its own.
add_library(quadrature_core STATIC
src/core/EncoderWaveform.cpp
src/core/EncoderWaveform.h
src/core/MoveGrouper.cpp
src/core/MoveGrouper.h
src/core/QuadratureDecoder.cpp
src/core/QuadratureDecoder.h
)
target_include_directories(quadrature_core PUBLIC src/core)
set_target_properties(quadrature_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(SOURCES
src/QuadratureEncoderAnalyzer.cpp
src/QuadratureEncoderAnalyzer.h
src/QuadratureEncoderAnalyzerResults.cpp
src/QuadratureEncoderAnalyzerResults.h
src/QuadratureEncoderAnalyzerSettings.cpp
src/QuadratureEncoderAnalyzerSettings.h
src/QuadratureEncoderSimulationDataGenerator.cpp
src/QuadratureEncoderSimulationDataGenerator.h
)

add_analyzer_plugin(${PROJECT_NAME} SOURCES ${SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE quadrature_core)

option(QUADRATURE_BUILD_TESTS "Build the decoder unit tests and tools" ON)

if(QUADRATURE_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    True
    )
    FetchContent_GetProperties(doctest)
    if(NOT doctest_POPULATED)
        FetchContent_Populate(doctest)
    endif()

    add_executable(quadrature_core_tests tests/core_tests.cpp)
    target_include_directories(quadrature_core_tests PRIVATE ${doctest_SOURCE_DIR})
    target_link_libraries(quadrature_core_tests PRIVATE quadrature_core)
    add_test(NAME quadrature_core_tests COMMAND quadrature_core_tests)
    set_tests_properties(quadrature_core_tests PROPERTIES TIMEOUT 120)

    # Runs the simulation data generator against the real Analyzer SDK library. The Linux SDK
    # library is built against libc++, so linking an executable to it with GCC's libstdc++ fails;
    # the analyzer module is only resolved against it when Logic loads it.
    if(WIN32 OR APPLE)
        add_executable(quadrature_simulation_tests
            tests/simulation_tests.cpp
            src/QuadratureEncoderAnalyzerSettings.cpp
            src/QuadratureEncoderSimulationDataGenerator.cpp
        )
        target_include_directories(quadrature_simulation_tests PRIVATE src ${doctest_SOURCE_DIR})
        target_link_libraries(quadrature_simulation_tests PRIVATE quadrature_core Saleae::AnalyzerSDK)
        if(WIN32)
            # Multi-config generators put the executable in a per-config folder, away from the copy
            # of the SDK DLL; without it Windows shows a "DLL not found" box and the test hangs.
            add_custom_command(TARGET quadrature_simulation_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Saleae::AnalyzerSDK> $<TARGET_FILE_DIR:quadrature_simulation_tests>)
        endif()
        add_test(NAME quadrature_simulation_tests COMMAND quadrature_simulation_tests)
        set_tests_properties(quadrature_simulation_tests PROPERTIES TIMEOUT 120)
    endif()

    # Replays a Logic 2 digital CSV export through the decoder.
    add_executable(quadrature_replay tools/replay_csv.cpp)
    target_link_libraries(quadrature_replay PRIVATE quadrature_core)
endif()
