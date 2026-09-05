cmake_minimum_required(VERSION 3.25)
project(NvidiaDriverMac VERSION 0.0.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rtxmac_core STATIC
    src/core/pci_identity.cpp
    src/core/nvidia_boot.cpp
    src/core/nvidia_probe.cpp
    src/core/nvidia_snapshot.cpp
    src/core/mmio_trace.cpp
    src/core/dma.cpp
    src/core/dma_plan.cpp
    src/core/gsp_rpc.cpp
)
target_include_directories(rtxmac_core PUBLIC include)

if(MSVC)
  target_compile_options(rtxmac_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(rtxmac_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rtxmac-offline tools/rtxmac_offline.cpp)
target_link_libraries(rtxmac-offline PRIVATE rtxmac_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(rtxmac-pci-tests tests/test_pci_identity.cpp)
  target_link_libraries(rtxmac-pci-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-pci-identity COMMAND rtxmac-pci-tests)

  add_executable(rtxmac-nvidia-tests tests/test_nvidia_boot.cpp)
  target_link_libraries(rtxmac-nvidia-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-nvidia-boot COMMAND rtxmac-nvidia-tests)

  add_executable(rtxmac-probe-tests tests/test_nvidia_probe.cpp)
  target_link_libraries(rtxmac-probe-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-nvidia-probe COMMAND rtxmac-probe-tests)

  add_executable(rtxmac-snapshot-tests tests/test_nvidia_snapshot.cpp)
  target_link_libraries(rtxmac-snapshot-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-nvidia-snapshot COMMAND rtxmac-snapshot-tests)

  add_executable(rtxmac-trace-tests tests/test_mmio_trace.cpp)
  target_link_libraries(rtxmac-trace-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-mmio-trace COMMAND rtxmac-trace-tests)

  add_executable(rtxmac-dma-tests tests/test_dma.cpp)
  target_link_libraries(rtxmac-dma-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-dma-validation COMMAND rtxmac-dma-tests)

  add_executable(rtxmac-dma-plan-tests tests/test_dma_plan.cpp)
  target_link_libraries(rtxmac-dma-plan-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-dma-plan COMMAND rtxmac-dma-plan-tests)

  add_executable(rtxmac-write-policy-tests tests/test_write_policy.cpp)
  target_link_libraries(rtxmac-write-policy-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-write-policy COMMAND rtxmac-write-policy-tests)

  add_executable(rtxmac-gsp-rpc-tests tests/test_gsp_rpc.cpp)
  target_link_libraries(rtxmac-gsp-rpc-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-gsp-rpc COMMAND rtxmac-gsp-rpc-tests)
endif()
