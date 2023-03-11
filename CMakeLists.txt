cmake_minimum_required(VERSION 3.1)
project(cppready_trader_go)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall)
endif()

find_package(Boost 1.74 COMPONENTS date_time log system thread
        OPTIONAL_COMPONENTS container graph math_c99 math_c99f math_tr1
        math_tr1f random regex timer unit_test_framework)
if(NOT ${Boost_FOUND})
    message(FATAL_ERROR
            "Ready Trader Go requires the free Boost C++ libraries version "
            "1.74 or above. See https://www.boost.org/.")
endif()

add_compile_definitions(BOOST_LOG_DYN_LINK=1)

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

add_subdirectory(libs)
include_directories(${PROJECT_SOURCE_DIR}/libs)

add_executable(autotrader main.cc autotrader.cc autotrader.h)
target_link_libraries(autotrader PRIVATE ready_trader_go_lib ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

add_executable(autotrader1 main.cc autotrader1.cc autotrader1.h)
target_link_libraries(autotrader1 PRIVATE ready_trader_go_lib ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

add_executable(autotrader2 main.cc autotrader2.cc autotrader2.h)
target_link_libraries(autotrader2 PRIVATE ready_trader_go_lib ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

add_executable(autotrader3 main.cc autotrader3.cc autotrader3.h)
target_link_libraries(autotrader3 PRIVATE ready_trader_go_lib ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

if(${Boost_UNIT_TEST_FRAMEWORK_FOUND})
    if(IS_DIRECTORY ${PROJECT_SOURCE_DIR}/unit_tests)
        enable_testing()
        add_subdirectory(unit_tests)
    endif()
endif()
