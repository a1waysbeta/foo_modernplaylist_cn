cmake_minimum_required(VERSION 3.24)
project(foo_modernplaylist VERSION 0.9.0 LANGUAGES CXX)
include(CTest)
if(BUILD_TESTING)
  if(WIN32)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(NAME search_layout_native
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_layout_test.py" "${CMAKE_CXX_COMPILER}")
    endif()
  endif()
  if(NOT WIN32)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(NAME group_header_scrolling
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/group_header_scroll_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME tab_appearance
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tab_appearance_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME feedback_changes
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/feedback_changes_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME grouping
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/grouping_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME column_configuration_migration
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/config_migration_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME search_controller
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_controller_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME search_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_controls_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME special_playlist_lifecycle
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/special_playlists_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME special_columns
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/special_columns_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME artwork
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/artwork_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME drag_drop
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/drag_drop_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME scrollbar
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/scrollbar_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME menus_shortcuts
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/menus_shortcuts_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME manager_status
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/manager_status_test.py" "${CMAKE_CXX_COMPILER}")
      add_test(NAME subclass_dispatch_shutdown
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/subclass_dispatch_test.py" "${CMAKE_CXX_COMPILER}")
    endif()
  endif()
endif()
if(NOT WIN32)
  message(STATUS "Linux: configuring Python-driven portable checks only. The component requires Windows/MSVC.")
  return()
endif()
if(NOT MSVC)
  message(FATAL_ERROR "Build with the Microsoft C++ toolchain (Visual Studio 2022 or newer).")
endif()
enable_language(RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/columns_ui/foobar2000-sdk-modified" CACHE PATH "foobar2000 SDK directory")
get_filename_component(SDK_PARENT "${SDK_ROOT}" DIRECTORY)
# Use the supplied SDK's own source manifests, without changing vendored projects.
function(sdk_library target dir project)
  file(READ "${dir}/${project}.vcxproj" manifest)
  string(REGEX MATCHALL "<ClCompile Include=\"[^\"]+\\.cpp\"" entries "${manifest}")
  set(sources)
  foreach(entry IN LISTS entries)
    string(REGEX REPLACE ".*Include=\"([^\"]+)\"" "\\1" source "${entry}")
    # Match PFC's FB2K configuration: utility.cpp in the SDK supplies the hooks.
    if(source STREQUAL "pfc-fb2k-hooks.cpp" AND target STREQUAL "fb2k_pfc")
      continue()
    endif()
    if(NOT source STREQUAL "stdafx.cpp")
      list(APPEND sources "${dir}/${source}")
    endif()
  endforeach()
  add_library(${target} STATIC ${sources})
  target_include_directories(${target} PUBLIC "${SDK_ROOT}" "${SDK_PARENT}" "${CMAKE_CURRENT_SOURCE_DIR}/msbuild/compat/include")
  target_compile_definitions(${target} PUBLIC UNICODE _UNICODE WIN32 NOMINMAX CUI_SDK_DWRITE_ENABLED=0)
  target_compile_options(${target} PRIVATE /utf-8 /permissive-)
endfunction()
sdk_library(fb2k_pfc "${SDK_PARENT}/pfc" pfc)
sdk_library(fb2k_sdk "${SDK_ROOT}/SDK" foobar2000_SDK)
sdk_library(columns_sdk "${SDK_PARENT}/columns_ui-sdk" columns_ui-sdk-public)
if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM")
  message(FATAL_ERROR "This build currently supports x64 and Win32 only.")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(SDK_ARCH x64)
else()
  set(SDK_ARCH Win32)
endif()
add_library(foo_modernplaylist SHARED src/component.cpp src/special_playlists.cpp src/playlist_view.cpp src/playlist_viewport.cpp src/columns_panel.cpp src/preferences.cpp src/dialogs.rc
  "${SDK_ROOT}/foobar2000_component_client/component_client.cpp")
target_include_directories(foo_modernplaylist PRIVATE "${SDK_ROOT}" "${SDK_PARENT}" "${CMAKE_CURRENT_SOURCE_DIR}/msbuild/compat/include")
target_compile_definitions(foo_modernplaylist PRIVATE UNICODE _UNICODE WIN32 NOMINMAX CUI_SDK_DWRITE_ENABLED=0)
target_compile_options(foo_modernplaylist PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(foo_modernplaylist PRIVATE columns_sdk fb2k_sdk fb2k_pfc
  "${SDK_ROOT}/shared/shared-${SDK_ARCH}.lib" msimg32 windowscodecs d2d1 dwrite oleacc comdlg32 comctl32 uxtheme shlwapi shell32 ole32 oleaut32 uuid winmm ws2_32 dbghelp)
set_target_properties(foo_modernplaylist PROPERTIES PREFIX "")
install(TARGETS foo_modernplaylist RUNTIME DESTINATION .)
