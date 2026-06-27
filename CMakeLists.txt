cmake_minimum_required(VERSION 3.26)

project(Ironclad
    VERSION 0.1.0
    LANGUAGES CXX C
)

# Objective-C++ and Swift only exist on Apple (macOS app); Windows uses C++ only.
if (APPLE)
    enable_language(OBJCXX)
    enable_language(Swift)
endif()

# ─── Global C++ standard ──────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_OBJCXX_STANDARD       20)

# ─── Swift version ────────────────────────────────────────────────────────────
set(CMAKE_Swift_LANGUAGE_VERSION 5)

# ─── Output directories ───────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─── Compiler flags ───────────────────────────────────────────────────────────
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(IRCE_CXX_WARNINGS
        -Wall -Wextra -Wpedantic
        -Wshadow -Wno-unused-parameter
        -Wno-gnu-anonymous-struct)
    if (APPLE)
        list(APPEND IRCE_CXX_WARNINGS -Wno-nullability-extension)
    endif()
endif()

# ─── Platform detection ───────────────────────────────────────────────────────
if (APPLE)
    set(IRCE_PLATFORM_APPLE TRUE)
    if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(IRCE_PLATFORM_IOS TRUE)
    else()
        set(IRCE_PLATFORM_MACOS TRUE)
    endif()
endif()

# ─── Engine Core library ──────────────────────────────────────────────────────
add_library(EngineCore STATIC
    Engine/Simulation/World.cpp
    Engine/Physics/PhysicsSystem.cpp
    Engine/Network/Transport/UDPTransport.cpp
    Engine/Animation/Core/Skeleton.cpp
    Engine/Animation/Core/Pose.cpp
    Engine/Animation/Runtime/AnimationClip.cpp
    Engine/Animation/Runtime/AnimationSampler.cpp
    Engine/Animation/Runtime/AnimationStateMachine.cpp
    Engine/Animation/Runtime/AnimationController.cpp
    Engine/Animation/IK/IKSolver.cpp
    Engine/Animation/Procedural/ProceduralAnimator.cpp
    Engine/Animation/Debug/AnimationDebug.cpp
)

target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(EngineCore PRIVATE ${IRCE_CXX_WARNINGS})
target_compile_definitions(EngineCore PUBLIC
    $<$<CONFIG:Debug>:PFGE_DEBUG=1>
)

# ─── Metal renderer (Apple only) ─────────────────────────────────────────────
if (IRCE_PLATFORM_APPLE)
    add_library(EngineRenderer STATIC
        Engine/Renderer/Metal/MetalRenderer.mm
    )
    target_include_directories(EngineRenderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(EngineRenderer PRIVATE ${IRCE_CXX_WARNINGS})

    find_library(METAL_LIB      Metal)
    find_library(METALKIT_LIB   MetalKit)
    find_library(QUARTZCORE_LIB QuartzCore)
    find_library(FOUNDATION_LIB Foundation)

    target_link_libraries(EngineRenderer PUBLIC
        EngineCore
        ${METAL_LIB}
        ${METALKIT_LIB}
        ${QUARTZCORE_LIB}
        ${FOUNDATION_LIB}
    )
endif()

# ─── Sandbox app (macOS — blank Metal window for engine verification) ─────────
if (IRCE_PLATFORM_MACOS)
    set(SANDBOX_SWIFT_SOURCES
        Apps/Sandbox/SandboxApp.swift
        Apps/Sandbox/ContentView.swift
        Apps/Sandbox/GameView.swift
    )
    set(SANDBOX_OBJCXX_SOURCES
        Apps/Sandbox/EngineHost.mm
    )

    add_executable(IroncladSandbox MACOSX_BUNDLE
        ${SANDBOX_SWIFT_SOURCES}
        ${SANDBOX_OBJCXX_SOURCES}
    )

    set_target_properties(IroncladSandbox PROPERTIES
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER      "${CMAKE_CURRENT_SOURCE_DIR}/Apps/Sandbox/EngineHost.h"
        XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME "IroncladSandbox-Swift.h"
        MACOSX_BUNDLE_BUNDLE_NAME                       "IroncladSandbox"
        MACOSX_BUNDLE_BUNDLE_IDENTIFIER                 "com.ironclad.sandbox"
        MACOSX_BUNDLE_GUI_IDENTIFIER                    "com.ironclad.sandbox"
        MACOSX_BUNDLE_SHORT_VERSION_STRING              "0.1"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME         YES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER       "com.ironclad.sandbox"
        XCODE_ATTRIBUTE_SWIFT_VERSION                   "5.9"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC           YES
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET        "13.0"
        MACOSX_BUNDLE_INFO_PLIST                        "${CMAKE_CURRENT_SOURCE_DIR}/Apps/Sandbox/Info.plist"
    )

    find_library(APPKIT_LIB AppKit)

    target_link_libraries(IroncladSandbox PRIVATE
        EngineCore
        EngineRenderer
        ${APPKIT_LIB}
        ${METAL_LIB}
        ${METALKIT_LIB}
        ${QUARTZCORE_LIB}
        ${FOUNDATION_LIB}
    )

    target_compile_options(IroncladSandbox PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:${IRCE_CXX_WARNINGS}>
    )

    target_include_directories(IroncladSandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ─── Ironclad AE (animation editor app, macOS) ───────────────────────────────
if (IRCE_PLATFORM_MACOS)
    set(AE_SWIFT_SOURCES
        Apps/AnimEditor/AnimEditorApp.swift
        Apps/AnimEditor/ContentView.swift
        Apps/AnimEditor/GameView.swift
    )
    set(AE_OBJCXX_SOURCES
        Apps/AnimEditor/EngineHost.mm
    )
    set(AE_RESOURCES
        PlaceholderModels/Soldier.glb
        Apps/AnimEditor/Resources/AppIcon.icns)
    set_source_files_properties(${AE_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    add_executable(IroncladAE MACOSX_BUNDLE
        ${AE_SWIFT_SOURCES}
        ${AE_OBJCXX_SOURCES}
        ${AE_RESOURCES}
    )

    set_target_properties(IroncladAE PROPERTIES
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER       "${CMAKE_CURRENT_SOURCE_DIR}/Apps/AnimEditor/EngineHost.h"
        XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME "IroncladAE-Swift.h"
        MACOSX_BUNDLE_BUNDLE_NAME                        "IroncladAE"
        MACOSX_BUNDLE_BUNDLE_IDENTIFIER                  "com.ironclad.ae"
        MACOSX_BUNDLE_GUI_IDENTIFIER                     "com.ironclad.ae"
        MACOSX_BUNDLE_SHORT_VERSION_STRING               "0.1"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME          YES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER        "com.ironclad.ae"
        XCODE_ATTRIBUTE_SWIFT_VERSION                    "5"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC            YES
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET         "13.0"
        MACOSX_BUNDLE_INFO_PLIST                         "${CMAKE_CURRENT_SOURCE_DIR}/Apps/AnimEditor/Info.plist"
    )

    target_link_libraries(IroncladAE PRIVATE
        EngineCore
        EngineRenderer
        ${APPKIT_LIB}
        ${METAL_LIB}
        ${METALKIT_LIB}
        ${QUARTZCORE_LIB}
        ${FOUNDATION_LIB}
    )

    target_compile_options(IroncladAE PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:${IRCE_CXX_WARNINGS}>
        $<$<COMPILE_LANGUAGE:Swift>:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/Apps/AnimEditor/EngineHost.h>
    )

    target_include_directories(IroncladAE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ─── Ironclad AE (Windows: GLFW + D3D11 + Dear ImGui) ────────────────────────
if (WIN32)
    include(FetchContent)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5)
    FetchContent_MakeAvailable(glfw imgui)

    add_executable(IroncladAE
        Apps/AnimEditorWin/main.cpp
        Apps/AnimEditorWin/AnimEditorHost.cpp
        Apps/AnimEditorWin/SoldierModel.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(IroncladAE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(IroncladAE PRIVATE
        EngineCore glfw d3d11 dxgi d3dcompiler ws2_32)
    target_compile_options(IroncladAE PRIVATE ${IRCE_CXX_WARNINGS})

    # Stage the soldier asset next to the exe so the viewport can load it.
    add_custom_command(TARGET IroncladAE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/PlaceholderModels/Soldier.glb"
                "$<TARGET_FILE_DIR:IroncladAE>/Soldier.glb")
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
option(IRCE_BUILD_TESTS "Build tests" ON)
if (IRCE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()