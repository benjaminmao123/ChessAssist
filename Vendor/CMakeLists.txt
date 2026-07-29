cmake_minimum_required(VERSION 3.15) # Changed 4.0.0 to standard 3.15 to prevent version warnings
project(stockfish_wrapper NONE)

# 1. Define and Sanitize Paths to Forward Slashes for MSYS2
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/stockfish/src" SF_VENDOR_DIR)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}" SF_OUTPUT_DIR)

set(SF_ARCH "x86-64-avx2")

if (WIN32)
    set(MSYS_BASH "C:/msys64/usr/bin/bash.exe")
    set(INTERNAL_EXE "${SF_VENDOR_DIR}/stockfish.exe")
    set(FINAL_EXE "${CMAKE_BINARY_DIR}/Source/$<CONFIG>/stockfish.exe")

    # 2. Command to run the official Makefile (Outputs stockfish.exe)
    add_custom_command(
        OUTPUT "${INTERNAL_EXE}"
        COMMAND ${MSYS_BASH} -c "export PATH='/mingw64/bin:/usr/bin:$PATH' && cd '${SF_VENDOR_DIR}' && make -j profile-build ARCH=${SF_ARCH}"
        WORKING_DIRECTORY "${SF_VENDOR_DIR}"
        COMMENT "Compiling Stockfish using its official Makefile at ${SF_VENDOR_DIR}..."
    )

    # 3. Command to copy the compiled binary to the CMake output directory
    add_custom_command(
        OUTPUT "${FINAL_EXE}"
        COMMAND ${CMAKE_COMMAND} -E copy "${INTERNAL_EXE}" "${FINAL_EXE}"
        DEPENDS "${INTERNAL_EXE}"
        COMMENT "Copying Stockfish executable to output directory at ${FINAL_EXE}..."
    )
elseif (LINUX)
    set(INTERNAL_EXE "${SF_VENDOR_DIR}/stockfish")
    set(FINAL_EXE "${CMAKE_BINARY_DIR}/Source/stockfish")
    
    # 2. Command to run the official Makefile (Outputs stockfish)
    add_custom_command(
        OUTPUT "${INTERNAL_EXE}"
        COMMAND make -C "${SF_VENDOR_DIR}" -j profile-build ARCH=${SF_ARCH}
        WORKING_DIRECTORY "${SF_VENDOR_DIR}"
        COMMENT "Compiling Stockfish using its official Makefile at ${SF_VENDOR_DIR}..."
    )

    # 3. Command to copy the compiled binary to the CMake output directory
    add_custom_command(
        OUTPUT "${FINAL_EXE}"
        COMMAND ${CMAKE_COMMAND} -E copy "${INTERNAL_EXE}" "${FINAL_EXE}"
        DEPENDS "${INTERNAL_EXE}"
        COMMENT "Copying Stockfish executable to output directory at ${FINAL_EXE}..."
    )
else()
    message(FATAL_ERROR "Unsupported platform for Stockfish compilation.")
endif() 
# 4. Tie the commands to a build target (Now matching FINAL_EXE perfectly)
add_custom_target(build_stockfish ALL DEPENDS "${FINAL_EXE}")