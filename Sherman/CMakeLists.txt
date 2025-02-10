cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)
project(Sherman)

# disable boost warning
add_definitions(-DBOOST_COROUTINES_NO_DEPRECATION_WARNING)

#Compiler options
set(CMAKE_C_FLAGS "-Wall -Wno-deprecated-declarations -Wsign-compare -O0 -g")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++17")

# Link Options
set(LINKS_FLAGS "-lcityhash -lboost_system -lboost_coroutine -lpthread -libverbs -lmemcached")

set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_MB_UTILS ${PROJECT_SOURCE_DIR}/../microbench/)
include_directories(${INCLUDE_BASE})

#Source file define
set(COMMON_SRC ${PROJECT_SOURCE_DIR}/src)

#Optimizations
set(OPTS
    original ORIGINAL
    spinlock  SPINLOCK
    litl   LITL
    handover HANDOVER
    on_chip ON_CHIP
    batched_writeback   BATCHED_WRITEBACK
)

#Used by both server and clients
list(LENGTH OPTS OPTS_LEN)
math(EXPR OPTS_LEN "${OPTS_LEN} - 1")

foreach(index RANGE 0 ${OPTS_LEN} 2)
    list(GET OPTS ${index} name)
    math(EXPR next_index "${index} + 1")
    list(GET OPTS ${next_index} define)

    file(GLOB_RECURSE COMMON_FILE ${COMMON_SRC}/*.cpp)

    add_library(sherman_${name} STATIC ${COMMON_FILE})
    target_compile_definitions(sherman_${name} PRIVATE -D${define})
    set_target_properties(sherman_${name} PROPERTIES OUTPUT_NAME "sherman_${name}")

    link_libraries(sherman_${name})  # Fix linking issue (see next section)
endforeach()

file(GLOB TEST_SRC ${PROJECT_SOURCE_DIR}/test/*.cpp)
foreach (TEST ${TEST_SRC})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)
    add_executable(${TEST_NAME} ${TEST})
    target_link_libraries(${TEST_NAME} ${LINKS_FLAGS})
endforeach()

