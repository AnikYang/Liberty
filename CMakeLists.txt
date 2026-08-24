cmake_minimum_required(VERSION 3.20)
project(Liberty VERSION 1.0.0 LANGUAGES CXX)

add_executable(Liberty WIN32 src/main.cpp)
target_compile_features(Liberty PRIVATE cxx_std_17)
target_compile_definitions(Liberty PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(Liberty PRIVATE user32 shell32 advapi32 gdi32)

if(MSVC)
  # Static C/C++ runtime: the release remains one portable EXE.
  set_property(TARGET Liberty PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(Liberty PRIVATE /W4 /permissive- /O1 /Os /GL /utf-8)
  target_link_options(Liberty PRIVATE /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS)
else()
  target_compile_options(Liberty PRIVATE -Wall -Wextra -Os -ffunction-sections -fdata-sections)
  target_link_options(Liberty PRIVATE -static -s -Wl,--gc-sections -mwindows)
endif()

install(TARGETS Liberty RUNTIME DESTINATION .)

