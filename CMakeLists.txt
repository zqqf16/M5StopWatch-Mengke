# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build for the esp-emulator web StopWatch emulator: idf.py -DM5EMU=1 build
if(M5EMU OR DEFINED ENV{M5EMU})
    message(STATUS "M5EMU port enabled (components/m5emu)")
    idf_build_set_property(COMPILE_DEFINITIONS "M5EMU_PORT=1" APPEND)
else()
    set(EXCLUDE_COMPONENTS m5emu)
endif()
project(StopWatch-UserDemo)
