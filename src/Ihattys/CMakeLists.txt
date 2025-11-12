# =============================================================================
# Ihattys – gRPC/Protobuf integration (no separate library)
# =============================================================================
# Assumes top-level CMake has already done:
#   find_package(Threads REQUIRED)
#   find_package(ZLIB REQUIRED)
#   find_package(protobuf CONFIG REQUIRED)
#   find_package(gRPC CONFIG REQUIRED)
#
# If not, the following "guarded" finds will no-op when already done.

if(NOT TARGET protobuf::libprotobuf)
  find_package(protobuf CONFIG REQUIRED)
endif()

if(NOT TARGET gRPC::grpc)
  find_package(gRPC CONFIG REQUIRED)
endif()

# =============================================================================
# Paths
# =============================================================================
set(PROTO_DIR     "${CMAKE_CURRENT_SOURCE_DIR}")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(PROTO_FILE "${PROTO_DIR}/ihattys_api.proto")
get_filename_component(PROTO_BASENAME "${PROTO_FILE}" NAME)  # ihattys_api.proto

# Outputs in the build tree
set(PROTO_SRCS "${GENERATED_DIR}/ihattys_api.pb.cc")
set(PROTO_HDRS "${GENERATED_DIR}/ihattys_api.pb.h")
set(GRPC_SRCS  "${GENERATED_DIR}/ihattys_api.grpc.pb.cc")
set(GRPC_HDRS  "${GENERATED_DIR}/ihattys_api.grpc.pb.h")

# Use a path for protoc outputs that does NOT contain a Windows drive-colon,
# because protoc treats a colon as the separator between generator options and
# the output directory (e.g. --cpp_out=opt1,opt2:OUT_DIR). On Windows an
# absolute path like "E:/..." contains a colon and confuses protoc. Compute a
# path relative to the proto working directory and use that in the command.
file(RELATIVE_PATH REL_GENERATED_DIR "${PROTO_DIR}" "${GENERATED_DIR}")


# =============================================================================
# Tools (portable lookup)
# =============================================================================
# Prefer user-specified executables (cache variables), then imported targets,
# then fallback to PATH. This allows presets to provide PROTOC_EXECUTABLE and
# GRPC_CPP_PLUGIN_EXECUTABLE (useful on Windows/vcpkg setups).
if(DEFINED PROTOC_EXECUTABLE)
  set(_PROTOC "${PROTOC_EXECUTABLE}")
else()
  get_target_property(_PROTOC_LOC protobuf::protoc IMPORTED_LOCATION)
  if(NOT _PROTOC_LOC)
    find_program(_PROTOC protoc REQUIRED)
  else()
    set(_PROTOC "${_PROTOC_LOC}")
  endif()
endif()

if(DEFINED GRPC_CPP_PLUGIN_EXECUTABLE)
  set(_GRPC_PLUGIN "${GRPC_CPP_PLUGIN_EXECUTABLE}")
else()
  get_target_property(_GRPC_PLUGIN_LOC gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
  if(NOT _GRPC_PLUGIN_LOC)
    find_program(_GRPC_PLUGIN grpc_cpp_plugin REQUIRED)
  else()
    set(_GRPC_PLUGIN "${_GRPC_PLUGIN_LOC}")
  endif()
endif()

# =============================================================================
# Code generation
# =============================================================================
# _PROTOC and _GRPC_PLUGIN should now be set (from cache vars, imported targets,
# or found on PATH). Convert plugin path to native Windows form (no quotes here)

# Convert plugin path to native Windows form (no quotes here)
file(TO_NATIVE_PATH "${_GRPC_PLUGIN}" GRPC_PLUGIN_NATIVE)

add_custom_command(
  OUTPUT
    ${PROTO_SRCS} ${PROTO_HDRS}
    ${GRPC_SRCS}  ${GRPC_HDRS}
  WORKING_DIRECTORY "${PROTO_DIR}"
  COMMAND "${_PROTOC}"
      -I "${PROTO_DIR}"
      --cpp_out=${REL_GENERATED_DIR}
      --grpc_out=${REL_GENERATED_DIR}
      # IMPORTANT: no extra quotes around the plugin mapping:
      --plugin=protoc-gen-grpc=${GRPC_PLUGIN_NATIVE}
      "${PROTO_BASENAME}"
  DEPENDS "${PROTO_FILE}"
  COMMENT "Generating C++ sources from ${PROTO_FILE}"
  VERBATIM
)

# Phony target to make dependencies explicit for Ninja/Qt autogen
add_custom_target(IhattysProtos
  DEPENDS
    ${PROTO_SRCS} ${PROTO_HDRS}
    ${GRPC_SRCS}  ${GRPC_HDRS}
)

# Ensure the main app sees/generated files before autogen comp
add_dependencies(${CMAKE_PROJECT_NAME} IhattysProtos)

# =============================================================================
# Compile into main target
# =============================================================================
target_sources(${CMAKE_PROJECT_NAME}
  PRIVATE
    IhattysServer.cc
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

# Headers for both your code and generated code
target_include_directories(${CMAKE_PROJECT_NAME}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${GENERATED_DIR}"
)

# If you want to link here, you can, but typically these are linked at the top level.
# Keep this block only if your top-level does NOT already link them.
# target_link_libraries(${CMAKE_PROJECT_NAME}
#   PRIVATE
#     gRPC::grpc
#     gRPC::grpc++
#     protobuf::libprotobuf
# )

# =============================================================================
# (Optional) extras if you actually use Abseil in IhattysServer.cc/.h
# =============================================================================
# if(NOT TARGET absl::strings)
#   find_package(absl CONFIG REQUIRED)
# endif()
# target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE absl::strings absl::status absl::statusor)
