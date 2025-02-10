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

file(GLOB_RECURSE COMMON_FILE ${COMMON_SRC}/*.cpp)

# ORIGINAL SHERMAN W/ OPTS
add_library(sherman STATIC ${COMMON_FILE})
target_compile_definitions(sherman PRIVATE -DSHERMAN_LOCK -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK)
set_target_properties(sherman PROPERTIES OUTPUT_NAME "sherman")
link_libraries(sherman)

# SHERMAN_LOCK
add_library(sherman_shermanLock STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanLock PRIVATE -DSHERMAN_LOCK)
set_target_properties(sherman_shermanLock PROPERTIES OUTPUT_NAME "sherman_shermanLock")
link_libraries(sherman_shermanLock)

# LITL
add_library(sherman_litl STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litl PRIVATE -DLITL)
set_target_properties(sherman_litl PROPERTIES OUTPUT_NAME "sherman_litl")
link_libraries(sherman_litl)


file(GLOB TEST_SRC ${PROJECT_SOURCE_DIR}/test/*.cpp)
foreach (TEST ${TEST_SRC})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)
    add_executable(${TEST_NAME} ${TEST})
    target_link_libraries(${TEST_NAME} ${LINKS_FLAGS})
endforeach()

