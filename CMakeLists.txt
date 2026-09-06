cmake_minimum_required(VERSION 3.25)
project(NvidiaDriverMac VERSION 0.0.18 LANGUAGES CXX)

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
    src/core/pramin.cpp
    src/core/gsp_rpc.cpp
    src/core/gsp_boot.cpp
    src/core/gsp_bootstrap.cpp
    src/core/gsp_metadata.cpp
    src/core/gsp_radix3.cpp
    src/core/gsp_manifest.cpp
    src/core/framebuffer_stage.cpp
    src/core/elf.cpp
    src/core/nvfw.cpp
    src/core/vbios.cpp
    src/core/frts.cpp
    src/core/falcon_plan.cpp
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

  add_executable(rtxmac-pramin-tests tests/test_pramin.cpp)
  target_link_libraries(rtxmac-pramin-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-ga102-pramin-staging COMMAND rtxmac-pramin-tests)

  add_executable(rtxmac-framebuffer-stage-tests tests/test_framebuffer_stage.cpp)
  target_link_libraries(rtxmac-framebuffer-stage-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-framebuffer-stage-artifact COMMAND rtxmac-framebuffer-stage-tests)

  add_executable(rtxmac-write-policy-tests tests/test_write_policy.cpp)
  target_link_libraries(rtxmac-write-policy-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-write-policy COMMAND rtxmac-write-policy-tests)

  add_executable(rtxmac-gsp-rpc-tests tests/test_gsp_rpc.cpp)
  target_link_libraries(rtxmac-gsp-rpc-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-gsp-rpc COMMAND rtxmac-gsp-rpc-tests)

  add_executable(rtxmac-gsp-boot-tests tests/test_gsp_boot.cpp)
  target_link_libraries(rtxmac-gsp-boot-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-gsp-boot-memory COMMAND rtxmac-gsp-boot-tests)

  add_executable(rtxmac-gsp-metadata-tests tests/test_gsp_metadata.cpp)
  target_link_libraries(rtxmac-gsp-metadata-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-gsp-wpr-libos-metadata COMMAND rtxmac-gsp-metadata-tests)

  add_executable(rtxmac-gsp-radix3-tests tests/test_gsp_radix3.cpp)
  target_link_libraries(rtxmac-gsp-radix3-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-gsp-radix3 COMMAND rtxmac-gsp-radix3-tests)

  add_executable(rtxmac-gsp-bootstrap-tests tests/test_gsp_bootstrap.cpp)
  target_link_libraries(rtxmac-gsp-bootstrap-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-gsp-bootstrap COMMAND rtxmac-gsp-bootstrap-tests)

  add_executable(rtxmac-gsp-manifest-tests tests/test_gsp_manifest.cpp)
  target_link_libraries(rtxmac-gsp-manifest-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-gsp-boot-manifest COMMAND rtxmac-gsp-manifest-tests)

  add_executable(rtxmac-elf-tests tests/test_elf.cpp)
  target_link_libraries(rtxmac-elf-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-elf-sections COMMAND rtxmac-elf-tests)

  add_executable(rtxmac-nvfw-tests tests/test_nvfw.cpp)
  target_link_libraries(rtxmac-nvfw-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-nvidia-firmware-parser COMMAND rtxmac-nvfw-tests)

  add_executable(rtxmac-vbios-tests tests/test_vbios.cpp)
  target_link_libraries(rtxmac-vbios-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-vbios-fwsec-parser COMMAND rtxmac-vbios-tests)

  add_executable(rtxmac-frts-tests tests/test_frts.cpp)
  target_link_libraries(rtxmac-frts-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-frts-offline-planning COMMAND rtxmac-frts-tests)

  add_executable(rtxmac-falcon-plan-tests tests/test_falcon_plan.cpp)
  target_link_libraries(rtxmac-falcon-plan-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-falcon-dry-run-plan COMMAND rtxmac-falcon-plan-tests)
endif()