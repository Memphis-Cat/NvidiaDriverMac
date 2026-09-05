cmake_minimum_required(VERSION 3.25)
project(NvidiaDriverMac VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rtxmac_core STATIC
    src/core/pci_identity.cpp
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
  add_executable(rtxmac-tests tests/test_pci_identity.cpp)
  target_link_libraries(rtxmac-tests PRIVATE rtxmac_core)
  add_test(NAME rtxmac-pci-identity COMMAND rtxmac-tests)
endif()
