cmake_minimum_required(VERSION 3.16)
project(FaiOS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(FAIOS_BUILD_SIMULATOR "Build the LVGL/SDL2 desktop simulator (needs network + libsdl2-dev)" ON)

# Toolkit-independent core: screens, key routing, simulated device state.
add_library(faios_core core/app_manager.cpp core/screens.cpp core/services.cpp)
target_include_directories(faios_core PUBLIC core)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core faios_core)
add_test(NAME core COMMAND test_core)

if(FAIOS_BUILD_SIMULATOR)
  find_package(SDL2 REQUIRED)
  include(FetchContent)
  set(LV_CONF_PATH ${CMAKE_SOURCE_DIR}/simulator/lv_conf.h CACHE STRING "" FORCE)
  FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.2.2 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(lvgl)
  target_link_libraries(lvgl PUBLIC SDL2::SDL2)

  add_executable(faios_sim simulator/main.cpp)
  target_link_libraries(faios_sim faios_core lvgl)
endif()
