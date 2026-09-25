# Cross-compile for Windows x86-64 from Linux with llvm-mingw (clang, UCRT, libc++):
# the toolchain Qt's win64_llvm_mingw packages are built with.
# LLVM_MINGW_ROOT: the extracted llvm-mingw release.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw prefix")
set(TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/${TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/${TRIPLE}-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/${TRIPLE}-windres")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/${TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
