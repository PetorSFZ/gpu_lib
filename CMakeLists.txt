cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
project("gpu_lib" LANGUAGES CXX)

# Sets build type to release if no build type is specified in a single-configuration generator.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Make all projects compile to the same directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Generate a "compile_commands.json" for VSCode and such when compiling with make
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

# Compiler flags
# ------------------------------------------------------------------------------------------------

if(MSVC)
	# MSVC flags
	# /W4 = Warning level 4 (/Wall is too picky and has annoying warnings in standard headers)
	# /std:c++20 = Enables C++20 support
	# /Zc:preprocessor = Enable conforming preprocessor, see https://docs.microsoft.com/en-us/cpp/preprocessor/preprocessor-experimental-overview?view=msvc-170
	# /permissive- = This option disables permissive behaviors, and sets the /Zc compiler options for strict conformance
	# /Zc:twoPhase- = Disables two-phase name lookup. Needed because it is not compatible with OpenMP
	# /Zi = Produce .pdb debug information. Does not affect optimizations, but does imply /debug.
	# /ZI = Similar to /Zi, but supports "Edit and Continue".  Most optimizations are incompatible with "Edit and Continue"
	# /Zf = Faster pdb generation
	# /EHsc = TODO: Add explanation
	# /GR- = Disable RTTI
	# /MT(d) = Uses multi-threaded static run-time library
	# /arch:AVX = Enable (require) Intel AVX instructions for code generation
	# /openmp:experimental = "This allows loops annotated with “#pragma omp simd” to potentially be vectorized."
	# /D_CRT_SECURE_NO_WARNINGS = Removes annyoing warning when using c standard library
	# /utf-8 = Specifies that both the source and execution character sets are encoded using UTF-8.
	# /Od = "disables optimization, speeding compilation and simplifying debugging"
	# /O2 = Optimize code for fastest speed
	# /Ob3 = "which is a more aggressive version of -Ob2"
	# /fp:fast = "optimize floating-point code for speed at the expense of accuracy and correctness"
	# /DNDEBUG = defines the "NDEBUG" macro, which disables asserts
	set(CMAKE_CXX_FLAGS "/W4 /std:c++20 /Zc:preprocessor /permissive- /Zc:twoPhase- /EHsc /GR- /arch:AVX /openmp:experimental /D_CRT_SECURE_NO_WARNINGS /utf-8")
	set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Od /ZI /Zf")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT /O2 /Ob3 /fp:fast /Zi /Zf")
	set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob3 /fp:fast /DNDEBUG /Zi /Zf")

else()
	message(FATAL_ERROR "[gpu_lib]: Compiler flags not set for this platform, exiting.")
endif()

set(CMAKE_C_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_C_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
set(CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
set(CMAKE_C_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})

# Bundled externals
# ------------------------------------------------------------------------------------------------

set(EXTERNALS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/externals)

# D3D12 Agility SDK
# ${D3D12_AGILITY_SDK_FOUND}, ${D3D12_AGILITY_SDK_INCLUDE_DIRS}, ${D3D12_AGILITY_SDK_RUNTIME_FILES}
add_subdirectory(${EXTERNALS_DIR}/D3D12AgilitySDK)

# dxc
# ${DXC_FOUND}, ${DXC_INCLUDE_DIRS}, ${DXC_LIBRARIES}, ${DXC_RUNTIME_FILES}
add_subdirectory(${EXTERNALS_DIR}/dxc)

# SDL2
# ${SDL2_FOUND}, ${SDL2_INCLUDE_DIRS}, ${SDL2_LIBRARIES} and ${SDL2_RUNTIME_FILES}
add_subdirectory(${EXTERNALS_DIR}/sdl2 ${CMAKE_BINARY_DIR}/sdl2)

# gpu_lib library
# ------------------------------------------------------------------------------------------------

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS ${SRC_DIR}/*.hpp ${SRC_DIR}/*.cpp ${SRC_DIR}/*.h ${SRC_DIR}/*.c)
#list(FILTER SRC_FILES EXCLUDE REGEX "ApocMain.cpp")
source_group(TREE ${SRC_DIR} FILES ${SRC_FILES})

add_library(gpu_lib ${SRC_FILES})

target_include_directories(gpu_lib PRIVATE
	${SRC_DIR}
	${D3D12_AGILITY_SDK_INCLUDE_DIRS}
	${DXC_INCLUDE_DIRS}
)

target_link_libraries(gpu_lib
	${DXC_LIBRARIES}
)

# Samples
# ------------------------------------------------------------------------------------------------

set(SAMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/samples)

add_executable(gpu_lib_sample_1 ${SAMPLES_DIR}/gpu_lib_sample_1.cpp)
target_include_directories(gpu_lib_sample_1 PUBLIC
	${SRC_DIR}
	${SAMPLES_DIR}
	${SDL2_INCLUDE_DIRS}
)
target_link_libraries(gpu_lib_sample_1
	gpu_lib
	${SDL2_LIBRARIES}
)

# File copying
# ------------------------------------------------------------------------------------------------

# Windows only, copies files to the output binary dir (${CMAKE_BINARY_DIR}).
#
# This function takes a variable number of parameters (i.e. vararg), each parameter is a path to
# something to be copied in to your binary directory.
function(copyRuntimeFilesToBinaryDir)
	if(MSVC)
		message("-- Copying following files to binary dir:")
		foreach(runtimeFilePath ${ARGV})
			message("  -- ${runtimeFilePath}")
		endforeach()

		foreach(runtimeFilePath ${ARGV})
			# Do different things depending on if we are generating .sln or using built-in VS CMake
			if(CMAKE_CONFIGURATION_TYPES)
				file(COPY ${runtimeFilePath} DESTINATION ${CMAKE_BINARY_DIR}/Debug)
				file(COPY ${runtimeFilePath} DESTINATION ${CMAKE_BINARY_DIR}/RelWithDebInfo)
				file(COPY ${runtimeFilePath} DESTINATION ${CMAKE_BINARY_DIR}/Release)
			else()
				file(COPY ${runtimeFilePath} DESTINATION ${CMAKE_BINARY_DIR})
			endif()
		endforeach()
	endif()
endfunction()

copyRuntimeFilesToBinaryDir(${SDL2_RUNTIME_FILES} ${DXC_RUNTIME_FILES})

# Copy D3D12 Agility SDK to binary dir
if(MSVC)
	foreach(dllPath ${D3D12_AGILITY_SDK_RUNTIME_FILES})
		if(CMAKE_CONFIGURATION_TYPES)
			file(COPY ${dllPath} DESTINATION ${CMAKE_BINARY_DIR}/Debug/D3D12)
			file(COPY ${dllPath} DESTINATION ${CMAKE_BINARY_DIR}/RelWithDebInfo/D3D12)
			file(COPY ${dllPath} DESTINATION ${CMAKE_BINARY_DIR}/Release/D3D12)
		else()
			file(COPY ${dllPath} DESTINATION ${CMAKE_BINARY_DIR}/D3D12)
		endif()
	endforeach()
endif()
