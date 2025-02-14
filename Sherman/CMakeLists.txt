cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)
project(Sherman)

# disable boost warning
add_definitions(-DBOOST_COROUTINES_NO_DEPRECATION_WARNING)
add_definitions(-DMLX4)

#Compiler options
set(CMAKE_C_FLAGS  "-Wall -Wno-class-memaccess -Wno-deprecated-declarations -Wsign-compare -O3 -g")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++17")

#For Systems group cluster
include_directories(
    "$ENV{HOME}/my_libs/libmemcached-1.0.18"
    "$ENV{HOME}/my_libs/cityhash/include"
    )
link_directories(
        "$ENV{HOME}/my_libs/libmemcached-1.0.18/libmemcached/.libs" 
        "$ENV{HOME}/my_libs/cityhash/lib"
    )

find_library(LIBMEMCACHED_LIBRARY NAMES memcached HINTS "$ENV{HOME}/my_libs/libmemcached-1.0.18/libmemcached/.libs")
if (NOT LIBMEMCACHED_LIBRARY)
    message(WARNING "libmemcached not found!")
    set(LIBMEMCACHED_LIBRARY "-lmemcached")
else()
    message(WARNING ${LIBMEMCACHED_LIBRARY})
endif()

# Link Options
set(LINKS_FLAGS "-lcityhash -lboost_system -lboost_coroutine -lpthread -libverbs ${LIBMEMCACHED_LIBRARY}")

set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_MB_UTILS ${PROJECT_SOURCE_DIR}/../microbench/)
include_directories(${INCLUDE_BASE})

#Source file define
set(COMMON_SRC ${PROJECT_SOURCE_DIR}/src)


file(GLOB_RECURSE COMMON_FILE ${COMMON_SRC}/*.cpp)

# SHERMAN_LOCK
add_library(sherman_shermanLock STATIC ${COMMON_FILE})
# target_link_libraries(sherman_shermanLock PRIVATE -L$ENV{HOME}/my_libs/libmemcached-1.0.18/libmemcached/.libs)
target_compile_definitions(sherman_shermanLock PRIVATE -DSHERMAN_LOCK)
set_target_properties(sherman_shermanLock PROPERTIES OUTPUT_NAME "sherman_shermanLock")
link_libraries(sherman_shermanLock)

# SHERMAN_LOCK HO
add_library(sherman_shermanHo STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHo PRIVATE -DSHERMAN_LOCK -DHANDOVER)
set_target_properties(sherman_shermanHo PROPERTIES OUTPUT_NAME "sherman_shermanHo")
link_libraries(sherman_shermanHo)

# ORIGINAL SHERMAN W/ OPTS
# add_library(sherman STATIC ${COMMON_FILE})
# target_compile_definitions(sherman PRIVATE -DSHERMAN_LOCK -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK)
# set_target_properties(sherman PROPERTIES OUTPUT_NAME "sherman")
# link_libraries(sherman)
# add_library(sherman STATIC ${COMMON_FILE})

# target_compile_definitions(sherman PRIVATE -DSHERMAN_LOCK -DHANDOVER -DBATCHED_WRITEBACK)
# set_target_properties(sherman PROPERTIES OUTPUT_NAME "sherman")
# link_libraries(sherman)

# LITL
add_library(sherman_litl STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litl PRIVATE -DLITL)
set_target_properties(sherman_litl PROPERTIES OUTPUT_NAME "sherman_litl")
link_libraries(sherman_litl)

# LITL HO
add_library(sherman_litlHo STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litlHo PRIVATE -DLITL -DHANDOVER)
set_target_properties(sherman_litlHo PROPERTIES OUTPUT_NAME "sherman_litlHo")
link_libraries(sherman_litlHo)

# LITL HO OCM BW
add_library(sherman_litlHoOcmBw STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litlHoOcmBw PRIVATE -DLITL -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK)
set_target_properties(sherman_litlHoOcmBw PROPERTIES OUTPUT_NAME "sherman_litlHoOcmBw")
link_libraries(sherman_litlHoOcmBw)


file(GLOB TEST_SRC ${PROJECT_SOURCE_DIR}/test/*.cpp)
foreach (TEST ${TEST_SRC})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)
    add_executable(${TEST_NAME} ${TEST})
    target_link_libraries(${TEST_NAME} ${LINKS_FLAGS})
endforeach()

