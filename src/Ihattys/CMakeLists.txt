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
## Locate the `proto` tree. Prefer `src/Ihattys/proto` or repo-level `proto` and
## prefer cases where a `mavsdk` subtree exists (new layout).
set(_candidate1 "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(_candidate2 "${CMAKE_SOURCE_DIR}/proto")
set(_candidate3 "${CMAKE_CURRENT_SOURCE_DIR}/../proto")
if(EXISTS "${_candidate1}/mavsdk")
  set(PROTO_DIR "${_candidate1}")
elseif(EXISTS "${_candidate2}/mavsdk")
  set(PROTO_DIR "${_candidate2}")
elseif(EXISTS "${_candidate3}/mavsdk")
  set(PROTO_DIR "${_candidate3}")
elseif(EXISTS "${_candidate1}")
  set(PROTO_DIR "${_candidate1}")
elseif(EXISTS "${_candidate2}")
  set(PROTO_DIR "${_candidate2}")
elseif(EXISTS "${_candidate3}")
  set(PROTO_DIR "${_candidate3}")
else()
  # Fallback to the current directory. This will fail later if no protos exist,
  # which is useful to catch misconfiguration early.
  set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Discover all .proto files recursively under the chosen proto dir. Keep the
# path RELATIVE to PROTO_DIR so we can preserve folder layout in generated
# outputs and pass relative paths to protoc.
file(GLOB_RECURSE _PROTO_FILES_ABS RELATIVE "${PROTO_DIR}" "${PROTO_DIR}/*.proto")
if(NOT _PROTO_FILES_ABS)
  message(FATAL_ERROR "Ihattys: no .proto files found in ${PROTO_DIR}")
endif()

# Build lists of generated outputs and inputs. Preserve subdirectory layout so
# protoc writes outputs into corresponding subfolders under ${GENERATED_DIR}.
set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_SRCS)
set(GRPC_HDRS)
set(PROTO_BASENAMES)
set(_PROTO_FILES_FULL)
foreach(_pf IN LISTS _PROTO_FILES_ABS)
  # _pf is relative to PROTO_DIR (e.g. "mavsdk/action/action.proto")
  get_filename_component(_basename "${_pf}" NAME_WE)
  get_filename_component(_reldir "${_pf}" PATH)
  if(_reldir STREQUAL ".")
    set(_outdir "${GENERATED_DIR}")
  else()
    set(_outdir "${GENERATED_DIR}/${_reldir}")
  endif()
  # Ensure the output directory exists so CMake recognizes the outputs on Windows
  file(MAKE_DIRECTORY "${_outdir}")

  list(APPEND PROTO_SRCS "${_outdir}/${_basename}.pb.cc")
  list(APPEND PROTO_HDRS "${_outdir}/${_basename}.pb.h")
  list(APPEND GRPC_SRCS  "${_outdir}/${_basename}.grpc.pb.cc")
  list(APPEND GRPC_HDRS  "${_outdir}/${_basename}.grpc.pb.h")

  # Keep proto paths relative to PROTO_DIR for the protoc invocation
  list(APPEND PROTO_BASENAMES "${_pf}")
  # Also record the absolute path for DEPENDS so the generator knows the
  # correct input file locations (avoids Ninja looking in the wrong dir).
  list(APPEND _PROTO_FILES_FULL "${PROTO_DIR}/${_pf}")
endforeach()

# Flatten into a single list for custom command OUTPUT
set(ALL_GENERATED_FILES ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS})

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
    ${ALL_GENERATED_FILES}
  WORKING_DIRECTORY "${PROTO_DIR}"
  COMMAND "${_PROTOC}"
      -I "${PROTO_DIR}"
      --cpp_out=${REL_GENERATED_DIR}
      --grpc_out=${REL_GENERATED_DIR}
      --plugin=protoc-gen-grpc=${GRPC_PLUGIN_NATIVE}
      # proto files (relative to WORKING_DIRECTORY)
      ${PROTO_BASENAMES}
  DEPENDS ${_PROTO_FILES_FULL}
  COMMENT "Generating C++ sources from protos under ${PROTO_DIR}"
  VERBATIM
)

# Phony target to make dependencies explicit for Ninja/Qt autogen
add_custom_target(IhattysProtos
  DEPENDS
    ${ALL_GENERATED_FILES}
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
