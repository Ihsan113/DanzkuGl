cmake_minimum_required(VERSION 3.18)
project(danzgl C ASM)

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "Hanya arm64-v8a. Pakai -DANDROID_ABI=arm64-v8a")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SYMS ${CMAKE_SOURCE_DIR}/symbols/mali_symbols.txt)
if(NOT EXISTS ${SYMS})
  message(FATAL_ERROR "symbols/mali_symbols.txt belum ada. Jalankan tools/dump_symbols.sh di HP lalu commit hasilnya.")
endif()

set(GEN ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})

set(ALLOW ${CMAKE_SOURCE_DIR}/symbols/allow_skip_data.txt)
set(ALLOW_ARGS "")
if(EXISTS ${ALLOW})
  set(ALLOW_ARGS --allow-data ${ALLOW})
endif()

add_custom_command(
  OUTPUT ${GEN}/dz_trampolines.S ${GEN}/dz_names.c
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_wrapper.py
          --symbols ${SYMS}
          --override ${CMAKE_SOURCE_DIR}/src/hooked_symbols.txt
          ${ALLOW_ARGS}
          --out ${GEN}
  DEPENDS ${SYMS} ${ALLOW} ${CMAKE_SOURCE_DIR}/src/hooked_symbols.txt ${CMAKE_SOURCE_DIR}/tools/gen_wrapper.py
  COMMENT "Generate trampoline dari symbols/mali_symbols.txt")

# target bernama GLES_mali -> menghasilkan libGLES_mali.so (SONAME sama dengan driver asli)
add_library(GLES_mali SHARED
  src/wrapper_core.c
  src/hooks.c
  ${GEN}/dz_names.c
  ${GEN}/dz_trampolines.S)

target_include_directories(GLES_mali PRIVATE src)
target_compile_options(GLES_mali PRIVATE -O2 -fvisibility=hidden -fno-exceptions -Wall -Wextra)
# JANGAN link libEGL / libGLESv*: libEGL yang memuat library ini.
target_link_libraries(GLES_mali PRIVATE log dl)
target_link_options(GLES_mali PRIVATE -Wl,--no-undefined -Wl,-z,relro -Wl,-z,now)
