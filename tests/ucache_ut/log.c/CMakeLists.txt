# Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
# Description: log test
# Create: 2024-02-27

cmake_minimum_required(VERSION 3.13)

project(log_tests VERSION 1.0 LANGUAGES C CXX)

SET(CMAKE_CXX_FLAGS "-g -O0 -fprofile-arcs -ftest-coverage")
SET(CMAKE_C_FLAGS "-g -O0 -fprofile-arcs -ftest-coverage")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -lgcov")

# 添加测试代码
aux_source_directory(./ TEST_SRC)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")

# 添加头文件目录
include_directories(../../../lava_ocf_adaptor/include)
include_directories(../../../lava_ocf_adaptor/src)
include_directories(../../../env/posix)
execute_process(COMMAND mkdir inc WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
execute_process(COMMAND mkdir ocf WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/inc)
execute_process(COMMAND sudo cp -r ${PROJECT_SOURCE_DIR}/../../../inc/. inc/ocf/ WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
execute_process(COMMAND cp ${PROJECT_SOURCE_DIR}/../../../env/posix/ocf_env.h ${PROJECT_SOURCE_DIR}/inc WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
execute_process(COMMAND cp ${PROJECT_SOURCE_DIR}/../../../env/posix/ocf_env_headers.h ${PROJECT_SOURCE_DIR}/inc/ WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
execute_process(COMMAND cp ${PROJECT_SOURCE_DIR}/../../../env/posix/ocf_env_list.h ${PROJECT_SOURCE_DIR}/inc/ WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
execute_process(COMMAND cp ${PROJECT_SOURCE_DIR}/../../../env/posix/utils_mpool.h ${PROJECT_SOURCE_DIR}/inc/ WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

include_directories(inc)

# 生成目标
add_executable(log_tests ${TEST_SRC} ../../../lava_ocf_adaptor/src/log.c)

add_test(log_tests ${CMAKE_CURRENT_BINARY_DIR}/log_tests)

# 链接库，在此补充UT打桩函数
target_link_libraries(log_tests -lpthread -lm -lcmocka -lz)

# 打印构建选项
get_target_property(COMPILE_FLAGS log_tests COMPILE_OPTIONS)
message(STATUS "Compiler id: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Compiler flags: ${COMPILE_FLAGS}")
message(STATUS "Link flags: ${LINK_FLAGS}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
