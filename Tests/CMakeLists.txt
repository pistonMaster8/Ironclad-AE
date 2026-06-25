cmake_minimum_required(VERSION 3.26)

# ─── Physics tests ────────────────────────────────────────────────────────────
add_executable(PhysicsTests
    PhysicsTests.cpp
)
target_link_libraries(PhysicsTests PRIVATE EngineCore)
target_include_directories(PhysicsTests PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(PhysicsTests PRIVATE -std=c++20)
add_test(NAME PhysicsTests COMMAND PhysicsTests)