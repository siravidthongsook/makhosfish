cmake_minimum_required(VERSION 3.20)
project(makhosfish LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MAKHOS_NATIVE   "Build with -march=native"        ON)
option(MAKHOS_SANITIZE "Build with ASan+UBSan (debug)"   OFF)
option(MAKHOS_LTO      "Enable link-time optimization"   ON)

add_compile_options(-Wall -Wextra -Wpedantic)
if(MAKHOS_NATIVE)
  add_compile_options(-march=native)
endif()
if(MAKHOS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()
if(MAKHOS_LTO AND NOT MAKHOS_SANITIZE)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()

# --- core library -------------------------------------------------------
add_library(makhosfish_core STATIC
  src/core/bitboard.cpp
  src/core/zobrist.cpp
  src/core/position.cpp
  src/core/movegen.cpp
  src/protocol/fen.cpp
  src/protocol/pdn.cpp
  src/protocol/tci.cpp
  src/eval/eval.cpp
  src/eval/pattern.cpp
  src/egdb/bitbase.cpp
  src/egdb/retrograde.cpp
  src/search/tt.cpp
  src/search/search.cpp
)
target_include_directories(makhosfish_core PUBLIC src)

find_package(Threads REQUIRED)
target_link_libraries(makhosfish_core PUBLIC Threads::Threads)

# --- executables --------------------------------------------------------
add_executable(makhosfish src/main.cpp)
target_link_libraries(makhosfish PRIVATE makhosfish_core)

add_executable(makhosfish_perft tools/perft.cpp)
target_link_libraries(makhosfish_perft PRIVATE makhosfish_core)

add_executable(makhosfish_bench tools/bench.cpp)
target_link_libraries(makhosfish_bench PRIVATE makhosfish_core)

add_executable(makhosfish_datagen tools/datagen.cpp)
target_link_libraries(makhosfish_datagen PRIVATE makhosfish_core)

add_executable(makhosfish_tune tools/tune.cpp)
target_link_libraries(makhosfish_tune PRIVATE makhosfish_core)

# --- tests --------------------------------------------------------------
enable_testing()

add_executable(makhosfish_tests
  tests/test_main.cpp
  tests/test_movegen.cpp
  tests/test_rules.cpp
  tests/test_zobrist.cpp
  tests/test_fen.cpp
  tests/test_perft_agreement.cpp
  tests/test_perft_fixtures.cpp
  tests/test_variants.cpp
  tests/test_search.cpp
  tests/test_eval.cpp
  tests/test_pattern.cpp
  tests/test_egdb.cpp
  tests/test_notation.cpp
  tests/test_pdn.cpp
  tests/test_tci.cpp
  tests/reference_movegen.cpp
)
target_include_directories(makhosfish_tests PRIVATE third_party tests)
target_link_libraries(makhosfish_tests PRIVATE makhosfish_core)
target_compile_definitions(makhosfish_tests PRIVATE
  MAKHOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME makhosfish_unit COMMAND makhosfish_tests)

add_test(NAME makhosfish_perft_smoke COMMAND makhosfish_perft startpos 6)
