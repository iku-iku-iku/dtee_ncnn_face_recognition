
# Copyright (c) Huawei Technologies Co., Ltd. 2020. All rights reserved.
# secGear is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# PURPOSE.
# See the Mulan PSL v2 for more details.
include(./function.cmake)
set(CLIENT_SOURCE_FILES
# NOTE: you can add your insecure source files here
  client.cpp file.cpp
)

set(TEST_SOURCE_FILES
# NOTE: you can add your insecure source files here
  test.cpp file.cpp
)

set(COMPUTE_NODE_FILES
# NOTE: you can add your insecure source files here
  compute_node.cpp file.cpp
)

if(CMAKE_CXX_COMPILER MATCHES "riscv64-linux-gnu-g\\+\\+" OR ENV{CXX} MATCHES "riscv64-linux-gnu-g\\+\\+" OR CMAKE_SYSTEM_PROCESSOR MATCHES "riscv")
  add_subdirectory(ncnn_retinanet.rv)
  include_directories(ncnn_retinanet.rv)
else()
  add_subdirectory(ncnn_retinanet.x64)
  include_directories(ncnn_retinanet.x64)
endif()
#if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64")
#  add_subdirectory(ncnn_retinanet.x64)
#  include_directories(ncnn_retinanet.x64)
#elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "riscv")
#  add_subdirectory(ncnn_retinanet.rv)
#  include_directories(ncnn_retinanet.rv)
#endif()

add_executable(client ${CLIENT_SOURCE_FILES})
add_executable(test ${TEST_SOURCE_FILES})
add_executable(compute_node ${COMPUTE_NODE_FILES})

find_package(foonathan_memory REQUIRED)
target_link_libraries(client retinanet secure distributed_tee fastrtps fastcdr foonathan_memory rt)
target_link_libraries(test retinanet secure distributed_tee fastrtps fastcdr foonathan_memory rt)
target_link_libraries(compute_node secure distributed_tee fastrtps fastcdr foonathan_memory rt)

find_package(foonathan_memory REQUIRED)
foreach(EXE IN LISTS ${TEE_EXECUTABLE_TARGETS})
    target_link_libraries(${EXE} distributed_tee fastrtps fastcdr foonathan_memory rt)
endforeach()
