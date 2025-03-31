cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)
project(Sherman)

# disable boost warning
add_definitions(-DBOOST_COROUTINES_NO_DEPRECATION_WARNING)

#Compiler options
# set(CMAKE_C_FLAGS  "-march=native -Wall -Wno-class-memaccess -Wno-deprecated-declarations -Wsign-compare -O3 -g")
set(CMAKE_C_FLAGS  "-march=native -Wall -Wno-class-memaccess -Wno-deprecated-declarations -Wsign-compare -O3 -g")
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
add_library(sherman_sherman STATIC ${COMMON_FILE})
target_compile_definitions(sherman_sherman PRIVATE -DSHERMAN_LOCK)
set_target_properties(sherman_sherman PROPERTIES OUTPUT_NAME "sherman_sherman")
target_link_libraries(sherman_sherman ${LINKS_FLAGS})

# # SHERMAN_LOCK HO
# add_library(sherman_shermanHo STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanHo PRIVATE -DSHERMAN_LOCK -DHANDOVER)
# set_target_properties(sherman_shermanHo PROPERTIES OUTPUT_NAME "sherman_shermanHo")
# target_link_libraries(sherman_shermanHo ${LINKS_FLAGS})

# SHERMAN HOD
add_library(sherman_shermanHod STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHod PRIVATE -DSHERMAN_LOCK -DHANDOVER -DHANDOVER_DATA)
set_target_properties(sherman_shermanHod PROPERTIES OUTPUT_NAME "sherman_shermanHod")
target_link_libraries(sherman_shermanHod ${LINKS_FLAGS})

# SHERMAN HOD + OCM
add_library(sherman_shermanHodOcm STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHodOcm PRIVATE -DSHERMAN_LOCK -DHANDOVER -DHANDOVER_DATA -DON_CHIP)
set_target_properties(sherman_shermanHodOcm PROPERTIES OUTPUT_NAME "sherman_shermanHodOcm")
target_link_libraries(sherman_shermanHodOcm ${LINKS_FLAGS})


# # ORIGINAL SHERMAN W/ OPTS
# add_library(sherman_shermanHoOcmBw STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanHoOcmBw PRIVATE -DSHERMAN_LOCK -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK)
# set_target_properties(sherman_shermanHoOcmBw PROPERTIES OUTPUT_NAME "sherman_shermanHoOcmBw")
# target_link_libraries(sherman_shermanHoOcmBw ${LINKS_FLAGS})

# ORIGINAL SHERMAN W/ OPTS + HOD
add_library(sherman_shermanHodOcmBw STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHodOcmBw PRIVATE -DSHERMAN_LOCK -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK -DHANDOVER_DATA)
set_target_properties(sherman_shermanHodOcmBw PROPERTIES OUTPUT_NAME "sherman_shermanHodOcmBw")
target_link_libraries(sherman_shermanHodOcmBw ${LINKS_FLAGS})

# # SHERMAN_LOCK CN_AWARE
# add_library(sherman_shermanCna STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanCna PRIVATE -DSHERMAN_LOCK -DCN_AWARE)
# set_target_properties(sherman_shermanCna PROPERTIES OUTPUT_NAME "sherman_shermanCna")
# target_link_libraries(sherman_shermanCna ${LINKS_FLAGS})

# # SHERMAN_LOCK CN_AWARE + BW
# add_library(sherman_shermanCnaBw STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanCnaBw PRIVATE -DSHERMAN_LOCK -DCN_AWARE -DBATCHED_WRITEBACK)
# set_target_properties(sherman_shermanCnaBw PROPERTIES OUTPUT_NAME "sherman_shermanCnaBw")
# target_link_libraries(sherman_shermanCnaBw ${LINKS_FLAGS})

# SHERMAN_LOCK RAND_FAAD
add_library(sherman_shermanRfaad STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanRfaad PRIVATE -DSHERMAN_LOCK -DHANDOVER -DHANDOVER_DATA -DRAND_FAA -DRAND_FAAD)
set_target_properties(sherman_shermanRfaad PROPERTIES OUTPUT_NAME "sherman_shermanRfaad")
target_link_libraries(sherman_shermanRfaad ${LINKS_FLAGS})

# SHERMAN_LOCK RAND_FAA
add_library(sherman_shermanRfaa STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA)
set_target_properties(sherman_shermanRfaa PROPERTIES OUTPUT_NAME "sherman_shermanRfaa")
target_link_libraries(sherman_shermanRfaa ${LINKS_FLAGS})

# # SHERMAN_LOCK RAND_FAA + BW + HOD + OCM
# add_library(sherman_shermanHodOcmBwRfaa STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanHodOcmBwRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA -DBATCHED_WRITEBACK -DON_CHIP -DHANDOVER_DATA)
# set_target_properties(sherman_shermanHodOcmBwRfaa PROPERTIES OUTPUT_NAME "sherman_shermanHodOcmBwRfaa")
# target_link_libraries(sherman_shermanHodOcmBwRfaa ${LINKS_FLAGS})

# SHERMAN_LOCK RAND_FAA + HOD 
add_library(sherman_shermanHodRfaa STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHodRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA -DHANDOVER -DHANDOVER_DATA)
set_target_properties(sherman_shermanHodRfaa PROPERTIES OUTPUT_NAME "sherman_shermanHodRfaa")
target_link_libraries(sherman_shermanHodRfaa ${LINKS_FLAGS})

# # SHERMAN_LOCK RAND_FAA + BW
# add_library(sherman_shermanBwRfaa STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanBwRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA -DBATCHED_WRITEBACK)
# set_target_properties(sherman_shermanBwRfaa PROPERTIES OUTPUT_NAME "sherman_shermanBwRfaa")
# target_link_libraries(sherman_shermanBwRfaa ${LINKS_FLAGS})

# # SHERMAN_LOCK RAND_FAA + HOD + BW
# add_library(sherman_shermanHodBwRfaa STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanHodBwRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA -DHANDOVER_DATA -DBATCHED_WRITEBACK)
# set_target_properties(sherman_shermanHodBwRfaa PROPERTIES OUTPUT_NAME "sherman_shermanHodBwRfaa")
# target_link_libraries(sherman_shermanHodBwRfaa ${LINKS_FLAGS})

# # SHERMAN_LOCK RAND_FAA + OCM
# add_library(sherman_shermanOcmRfaa STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanOcmRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA -DON_CHIP)
# set_target_properties(sherman_shermanOcmRfaa PROPERTIES OUTPUT_NAME "sherman_shermanOcmRfaa")
# target_link_libraries(sherman_shermanOcmRfaa ${LINKS_FLAGS})

# SHERMAN_LOCK RAND_FAA + OCM + HOD
add_library(sherman_shermanHodOcmRfaa STATIC ${COMMON_FILE})
target_compile_definitions(sherman_shermanHodOcmRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA -DHANDOVER -DHANDOVER_DATA -DON_CHIP)
set_target_properties(sherman_shermanHodOcmRfaa PROPERTIES OUTPUT_NAME "sherman_shermanHodOcmRfaa")
target_link_libraries(sherman_shermanHodOcmRfaa ${LINKS_FLAGS})

# # SHERMAN_LOCK RAND_FAA + OCM + BW
# add_library(sherman_shermanOcmBwRfaa STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanOcmBwRfaa PRIVATE -DSHERMAN_LOCK -DRAND_FAA -DON_CHIP -DBATCHED_WRITEBACK)
# set_target_properties(sherman_shermanOcmBwRfaa PROPERTIES OUTPUT_NAME "sherman_shermanOcmBwRfaa")
# target_link_libraries(sherman_shermanOcmBwRfaa ${LINKS_FLAGS})

# # SHERMAN_LOCK RAND_MN_FAA
# add_library(sherman_shermanRMfaa STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_shermanRMfaa PRIVATE -DSHERMAN_LOCK -DRAND_MN_FAA)
# set_target_properties(sherman_shermanRMfaa PROPERTIES OUTPUT_NAME "sherman_shermanRMfaa")
# target_link_libraries(sherman_shermanRMfaa ${LINKS_FLAGS})

#################################################################################################################
#################################################################################################################
#################################################################################################################

# LITL
add_library(sherman_litl STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litl PRIVATE -DLITL)
set_target_properties(sherman_litl PROPERTIES OUTPUT_NAME "sherman_litl")
target_link_libraries(sherman_litl ${LINKS_FLAGS})

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

# LITL HOD OCM
add_library(sherman_litlHodOcm STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litlHodOcm PRIVATE -DLITL -DHANDOVER -DHANDOVER_DATA -DON_CHIP)
set_target_properties(sherman_litlHodOcm PROPERTIES OUTPUT_NAME "sherman_litlHodOcm")
target_link_libraries(sherman_litlHodOcm ${LINKS_FLAGS})

# LITL HOD OCM RAND_FAA
add_library(sherman_litlHodOcmRfaa STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litlHodOcmRfaa PRIVATE -DLITL -DHANDOVER -DHANDOVER_DATA -DON_CHIP -DRAND_FAA)
set_target_properties(sherman_litlHodOcmRfaa PROPERTIES OUTPUT_NAME "sherman_litlHodOcmRfaa")
target_link_libraries(sherman_litlHodOcmRfaa ${LINKS_FLAGS})

# LITL RAND_FAA
add_library(sherman_litlRfaa STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litlRfaa PRIVATE -DLITL -DRAND_FAA)
set_target_properties(sherman_litlRfaa PROPERTIES OUTPUT_NAME "sherman_litlRfaa")
target_link_libraries(sherman_litlRfaa ${LINKS_FLAGS})

# LITL RAND_FAA + HOD
add_library(sherman_litlHodRfaa STATIC ${COMMON_FILE})
target_compile_definitions(sherman_litlHodRfaa PRIVATE -DLITL -DRAND_FAA -DHANDOVER -DHANDOVER_DATA)
set_target_properties(sherman_litlHodRfaa PROPERTIES OUTPUT_NAME "sherman_litlHodRfaa")
target_link_libraries(sherman_litlHodRfaa ${LINKS_FLAGS})

# # LITL HOD OCM BW
# add_library(sherman_litlHodOcmBw STATIC ${COMMON_FILE})
# target_compile_definitions(sherman_litlHodOcmBw PRIVATE -DLITL -DHANDOVER -DON_CHIP -DBATCHED_WRITEBACK -DHANDOVER_DATA)
# set_target_properties(sherman_litlHodOcmBw PROPERTIES OUTPUT_NAME "sherman_litlHodOcmBw")
# target_link_libraries(sherman_litlHodOcmBw ${LINKS_FLAGS})

#################################################################################################################
#################################################################################################################
#################################################################################################################

# file(GLOB TEST_SRC ${PROJECT_SOURCE_DIR}/test/*.cpp)
# foreach (TEST ${TEST_SRC})
#     get_filename_component(TEST_NAME ${TEST} NAME_WE)
#     add_executable(${TEST_NAME} ${TEST})
#     target_link_libraries(${TEST_NAME} ${LINKS_FLAGS})
# endforeach()

