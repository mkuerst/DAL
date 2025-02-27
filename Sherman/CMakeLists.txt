cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)
project(Sherman)

# disable boost warning
add_definitions(-DBOOST_COROUTINES_NO_DEPRECATION_WARNING)

#Compiler options
set(CMAKE_C_FLAGS  "-Wall -Wno-class-memaccess -Wno-deprecated-declarations -Wsign-compare -O3 -g")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")
# set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")

# Link Options
set(LINKS_FLAGS "-lcityhash -lboost_system -lboost_coroutine -lpthread -libverbs -lmemcached")

set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/include)
# set(INCLUDE_MB_UTILS ${PROJECT_SOURCE_DIR}/../microbench/)
include_directories(${INCLUDE_BASE})

#Source file define
set(COMMON_SRC ${PROJECT_SOURCE_DIR}/src)


file(GLOB_RECURSE COMMON_FILE ${COMMON_SRC}/*.cpp)

#################################################################################################################
#################################################################################################################
#################################################################################################################

# SHERMAN_LOCK
add_library(sherman_shermanLock STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanLock PRIVATE -DSHERMAN_LOCK)
set_target_properties(sherman_shermanLock PROPERTIES OUTPUT_NAME "sherman_shermanLock")
target_link_libraries(sherman_shermanLock ${LINKS_FLAGS})

# SHERMAN_LOCK HO
add_library(sherman_shermanHo STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHo PRIVATE -DSHERMAN_LOCK -DHANDOVER)
set_target_properties(sherman_shermanHo PROPERTIES OUTPUT_NAME "sherman_shermanHo")
target_link_libraries(sherman_shermanHo ${LINKS_FLAGS})

# SHERMAN HOD
add_library(sherman_shermanHod STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHod PRIVATE -DSHERMAN_LOCK -DHANDOVER -DHANDOVER_DATA)
set_target_properties(sherman_shermanHod PROPERTIES OUTPUT_NAME "sherman_shermanHod")
target_link_libraries(sherman_shermanHod ${LINKS_FLAGS})

# # ORIGINAL SHERMAN W/ OPTS
# add_library(sherman STATIC ${COMMON_FILE})
# target_compile_definitions(sherman PRIVATE -DSHERMAN_LOCK -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK)
# set_target_properties(sherman PROPERTIES OUTPUT_NAME "sherman")
# target_link_libraries(sherman ${LINKS_FLAGS})

#################################################################################################################
#################################################################################################################
#################################################################################################################

# # LITL
# add_library(sherman_litl STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_litl PRIVATE -DLITL)
# set_target_properties(sherman_litl PROPERTIES OUTPUT_NAME "sherman_litl")
# target_link_libraries(sherman_litl ${LINKS_FLAGS})
# # target_link_libraries(sherman_litl)

# # LITL HO
# add_library(sherman_litlHo STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_litlHo PRIVATE -DLITL -DHANDOVER)
# set_target_properties(sherman_litlHo PROPERTIES OUTPUT_NAME "sherman_litlHo")
# target_link_libraries(sherman_litlHo ${LINKS_FLAGS})

# LITL HOD
add_library(sherman_litlHod STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litlHod PRIVATE -DLITL -DHANDOVER -DHANDOVER_DATA)
set_target_properties(sherman_litlHod PROPERTIES OUTPUT_NAME "sherman_litlHod")
target_link_libraries(sherman_litlHod ${LINKS_FLAGS})

# # LITL HO OCM BW
# add_library(sherman_litlHoOcmBw STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_litlHoOcmBw PRIVATE -DLITL -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK)
# set_target_properties(sherman_litlHoOcmBw PROPERTIES OUTPUT_NAME "sherman_litlHoOcmBw")
# target_link_libraries(sherman_litlHoOcmBw ${LINKS_FLAGS})

#################################################################################################################
#################################################################################################################
#################################################################################################################

# file(GLOB TEST_SRC ${PROJECT_SOURCE_DIR}/test/*.cpp)
# foreach (TEST ${TEST_SRC})
#     get_filename_component(TEST_NAME ${TEST} NAME_WE)
#     add_executable(${TEST_NAME} ${TEST})
#     target_link_libraries(${TEST_NAME} ${LINKS_FLAGS})
# endforeach()

