cmake_minimum_required(VERSION 3.15)
project(Ihattys)

# Find required packages
find_package(Threads REQUIRED)

# Find ZLIB first and create the target if it doesn't exist
find_package(ZLIB REQUIRED)
if(NOT TARGET ZLIB::ZLIB)
    # Create an alias target if vcpkg didn't export it properly
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${ZLIB_LIBRARIES}"
    )
endif()

find_package(absl CONFIG REQUIRED)

# For cross-compilation, we need host tools (protoc, grpc_cpp_plugin)
# and target libraries (libprotobuf, libgrpc++)
if(CMAKE_CROSSCOMPILING)
    # Manually set paths to host tools
    set(PROTOC_EXECUTABLE "C:/vcpkg/installed/x64-windows/tools/protobuf/protoc.exe" CACHE FILEPATH "protoc executable")
    set(GRPC_CPP_PLUGIN_EXECUTABLE "C:/vcpkg/installed/x64-windows/tools/grpc/grpc_cpp_plugin.exe" CACHE FILEPATH "grpc_cpp_plugin executable")
    
    # Find target libraries
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
else()
    # Native build - find everything normally
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    
    get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION)
    get_target_property(GRPC_CPP_PLUGIN_EXECUTABLE gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
endif()

# Verify we found the executables
if(NOT EXISTS ${PROTOC_EXECUTABLE})
    message(FATAL_ERROR "protoc executable not found at ${PROTOC_EXECUTABLE}. Install protobuf:x64-windows in vcpkg")
endif()

if(NOT EXISTS ${GRPC_CPP_PLUGIN_EXECUTABLE})
    message(FATAL_ERROR "grpc_cpp_plugin executable not found at ${GRPC_CPP_PLUGIN_EXECUTABLE}. Install grpc:x64-windows in vcpkg")
endif()

# Set proto file path
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ihattys_api.proto")

# Output directories for generated files
set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

# Generated source files
set(PROTO_SRCS "${PROTO_BINARY_DIR}/ihattys_api.pb.cc")
set(PROTO_HDRS "${PROTO_BINARY_DIR}/ihattys_api.pb.h")
set(GRPC_SRCS "${PROTO_BINARY_DIR}/ihattys_api.grpc.pb.cc")
set(GRPC_HDRS "${PROTO_BINARY_DIR}/ihattys_api.grpc.pb.h")

# Custom command to generate protobuf files
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --grpc_out=${PROTO_BINARY_DIR}
         --cpp_out=${PROTO_BINARY_DIR}
         -I${CMAKE_CURRENT_SOURCE_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC and Protobuf files from ${PROTO_FILE}"
)

# Create library
add_library(Ihattys STATIC
    IhattysServer.cc
    IhattysServer.h
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

# Include directories
target_include_directories(Ihattys
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROTO_BINARY_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..             # parent src/
        ${CMAKE_CURRENT_SOURCE_DIR}/../Utilities   # for Utilities/ headers
        ${CMAKE_CURRENT_SOURCE_DIR}/../QmlControls # for QmlObjectListModel.h
)

# Link libraries
target_link_libraries(Ihattys
    PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
        absl::strings
        absl::status
        absl::statusor
        Threads::Threads
        ZLIB::ZLIB
        Qt6::Core
)

# Only link reflection on non-Android platforms
if(NOT ANDROID)
    target_link_libraries(Ihattys PUBLIC gRPC::grpc++_reflection)
endif()

# Set C++ standard
target_compile_features(Ihattys PUBLIC cxx_std_17)

# For Android builds, add position independent code
if(ANDROID)
    set_target_properties(Ihattys PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Export include directories for parent project
set(IHATTYS_INCLUDE_DIRS 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROTO_BINARY_DIR}
    PARENT_SCOPE
)
