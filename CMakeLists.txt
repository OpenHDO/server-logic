cmake_minimum_required(VERSION 3.24)
project(openhdo_logic VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(OPENHDO_BUILD_TESTS "Build OpenHDO Logic tests" ${BUILD_TESTING})
option(OPENHDO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(openhdo_logic STATIC
    src/flow.cpp
)

target_compile_features(openhdo_logic PUBLIC cxx_std_20)
target_include_directories(openhdo_logic
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(openhdo_logic PRIVATE /W4)
else()
    target_compile_options(openhdo_logic PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(OPENHDO_WARNINGS_AS_ERRORS)
    target_compile_options(openhdo_logic PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
    )
endif()

if(OPENHDO_BUILD_TESTS)
    add_executable(openhdo-logic-tests
        tests/flow_tests.cpp
    )
    target_link_libraries(openhdo-logic-tests PRIVATE openhdo_logic)

    if(MSVC)
        target_compile_options(openhdo-logic-tests PRIVATE /W4)
    else()
        target_compile_options(openhdo-logic-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()

    if(OPENHDO_WARNINGS_AS_ERRORS)
        target_compile_options(openhdo-logic-tests PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
        )
    endif()

    add_test(NAME openhdo-logic-tests COMMAND openhdo-logic-tests)
endif()
