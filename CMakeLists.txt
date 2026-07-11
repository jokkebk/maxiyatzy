cmake_minimum_required(VERSION 3.20)
project(maxiyatzy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(maxiyatzy
    src/dice.cpp
    src/policy.cpp
    src/scoring.cpp
    src/solver.cpp
    src/upper_states.cpp
)
target_include_directories(maxiyatzy PUBLIC include)
target_compile_options(maxiyatzy PRIVATE -Wall -Wextra -Wpedantic)

add_executable(maxiyatzy-info src/main.cpp)
target_link_libraries(maxiyatzy-info PRIVATE maxiyatzy)

add_executable(maxiyatzy-tests tests/test_main.cpp)
target_link_libraries(maxiyatzy-tests PRIVATE maxiyatzy)

add_executable(maxiyatzy-bench bench/solver_kernel.cpp)
target_link_libraries(maxiyatzy-bench PRIVATE maxiyatzy)

add_executable(maxiyatzy-solve src/solve_main.cpp)
target_link_libraries(maxiyatzy-solve PRIVATE maxiyatzy)

add_executable(maxiyatzy-inspect src/inspect_main.cpp)

add_executable(maxiyatzy-advise src/advise_main.cpp)
target_link_libraries(maxiyatzy-advise PRIVATE maxiyatzy)

# Independent table verifier; deliberately excludes solver.cpp/upper_states.cpp.
add_executable(maxiyatzy-verify verify/verify_main.cpp src/dice.cpp src/scoring.cpp)
target_include_directories(maxiyatzy-verify PRIVATE include)
find_package(Threads REQUIRED)
target_link_libraries(maxiyatzy-verify PRIVATE Threads::Threads)

enable_testing()
add_test(NAME maxiyatzy-tests COMMAND maxiyatzy-tests)
