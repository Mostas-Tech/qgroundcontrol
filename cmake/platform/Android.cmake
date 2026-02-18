# ----------------------------------------------------------------------------
# QGroundControl Android Platform Configuration
# ----------------------------------------------------------------------------

if(NOT ANDROID)
    message(FATAL_ERROR "QGC: Invalid Platform: Android.cmake included but platform is not Android")
endif()

# ----------------------------------------------------------------------------
# Android NDK Version Validation
# ----------------------------------------------------------------------------
# CMAKE_ANDROID_NDK_VERSION format varies: "27.2" or "27.2.12829759"
# Extract major.minor from ndk_full_version for reliable comparison
if(DEFINED QGC_CONFIG_NDK_FULL_VERSION AND Qt6_VERSION VERSION_GREATER_EQUAL "${QGC_CONFIG_QT_MINIMUM_VERSION}")
    string(REGEX MATCH "^([0-9]+\\.[0-9]+)" _ndk_major_minor "${QGC_CONFIG_NDK_FULL_VERSION}")
    if(_ndk_major_minor AND NOT CMAKE_ANDROID_NDK_VERSION VERSION_GREATER_EQUAL "${_ndk_major_minor}")
        message(FATAL_ERROR "QGC: NDK ${CMAKE_ANDROID_NDK_VERSION} is too old. Qt ${Qt6_VERSION} requires NDK ${_ndk_major_minor}+ (${QGC_CONFIG_NDK_VERSION})")
    endif()
    unset(_ndk_major_minor)
endif()

# ----------------------------------------------------------------------------
# Android Version Number Validation
# ----------------------------------------------------------------------------

# Generation of Android version numbers must be consistent release to release
# to ensure they are always increasing for Google Play Store
if(CMAKE_PROJECT_VERSION_MAJOR GREATER 9)
    message(FATAL_ERROR "QGC: Major version must be single digit (0-9), got: ${CMAKE_PROJECT_VERSION_MAJOR}")
endif()
if(CMAKE_PROJECT_VERSION_MINOR GREATER 9)
    message(FATAL_ERROR "QGC: Minor version must be single digit (0-9), got: ${CMAKE_PROJECT_VERSION_MINOR}")
endif()
if(CMAKE_PROJECT_VERSION_PATCH GREATER 99)
    message(FATAL_ERROR "QGC: Patch version must be two digits (0-99), got: ${CMAKE_PROJECT_VERSION_PATCH}")
endif()

# ----------------------------------------------------------------------------
# Android ABI to Bitness Code Mapping
# ----------------------------------------------------------------------------
# NOTE: Bitness codes are 66/34 instead of 64/32 due to a historical
# version number bump requirement from an earlier Android release
set(ANDROID_BITNESS_CODE)
if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
    set(ANDROID_BITNESS_CODE 34)
elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(ANDROID_BITNESS_CODE 66)
else()
    message(FATAL_ERROR "QGC: Unsupported Android ABI: ${CMAKE_ANDROID_ARCH_ABI}. Supported: armeabi-v7a, arm64-v8a, x86, x86_64")
endif()

# ----------------------------------------------------------------------------
# Android Version Code Generation
# ----------------------------------------------------------------------------
# Zero-pad patch version if less than 10
set(ANDROID_PATCH_VERSION ${CMAKE_PROJECT_VERSION_PATCH})
if(CMAKE_PROJECT_VERSION_PATCH LESS 10)
    set(ANDROID_PATCH_VERSION "0${CMAKE_PROJECT_VERSION_PATCH}")
endif()

# Version code format: BBMIPPDDD (B=Bitness, M=Major, I=Minor, P=Patch, D=Dev) - Dev not currently supported and always 000
set(ANDROID_VERSION_CODE "${ANDROID_BITNESS_CODE}${CMAKE_PROJECT_VERSION_MAJOR}${CMAKE_PROJECT_VERSION_MINOR}${ANDROID_PATCH_VERSION}000")
message(STATUS "QGC: Android version code: ${ANDROID_VERSION_CODE}")

set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
        # QT_ANDROID_ABIS ${CMAKE_ANDROID_ARCH_ABI}
        # QT_ANDROID_SDK_BUILD_TOOLS_REVISION
        QT_ANDROID_MIN_SDK_VERSION ${QGC_QT_ANDROID_MIN_SDK_VERSION}
        QT_ANDROID_TARGET_SDK_VERSION ${QGC_QT_ANDROID_TARGET_SDK_VERSION}
        QT_ANDROID_COMPILE_SDK_VERSION ${QGC_QT_ANDROID_COMPILE_SDK_VERSION}
        QT_ANDROID_PACKAGE_NAME "${QGC_ANDROID_PACKAGE_NAME}"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${QGC_ANDROID_PACKAGE_SOURCE_DIR}"
        QT_ANDROID_VERSION_NAME "${CMAKE_PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE ${ANDROID_VERSION_CODE}
        QT_ANDROID_APP_NAME "${CMAKE_PROJECT_NAME}"
        QT_ANDROID_APP_ICON "@drawable/icon"
        # QT_QML_IMPORT_PATH
        QT_QML_ROOT_PATH "${CMAKE_SOURCE_DIR}"
        # QT_ANDROID_SYSTEM_LIBS_PREFIX
)

# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#     set(QT_ANDROID_APPLICATION_ARGUMENTS)
# endif()

list(APPEND QT_ANDROID_MULTI_ABI_FORWARD_VARS QGC_STABLE_BUILD QT_HOST_PATH)

# ----------------------------------------------------------------------------
# Android OpenSSL Libraries
# ----------------------------------------------------------------------------
CPMAddPackage(
    NAME android_openssl
    GITHUB_REPOSITORY KDAB/android_openssl
    GIT_TAG b71f1470962019bd89534a2919f5925f93bc5779
)

if(android_openssl_ADDED)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(${CMAKE_PROJECT_NAME})
    message(STATUS "QGC: Android OpenSSL libraries added")
    # Ensure androiddeployqt copies the OpenSSL runtime shared libs into the APK.
    #
    # Note: KDAB's script only populates `_OPENSSL_EXTRA_LIBS_PATHS` if it had to
    # create the OpenSSL imported targets. If another toolchain (e.g. vcpkg)
    # already provides OpenSSL::SSL/Crypto, `_OPENSSL_EXTRA_LIBS_PATHS` may be
    # unset even though the runtime .so files are still required by Qt's
    # qopensslbackend plugin (loaded via dlopen at runtime).
    set(_qgc_android_openssl_libs)
    if(DEFINED _OPENSSL_EXTRA_LIBS_PATHS)
        list(APPEND _qgc_android_openssl_libs ${_OPENSSL_EXTRA_LIBS_PATHS})
    endif()

    if(NOT _qgc_android_openssl_libs)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_qgc_android_openssl_root "${android_openssl_SOURCE_DIR}/no-asm")
        else()
            set(_qgc_android_openssl_root "${android_openssl_SOURCE_DIR}")
        endif()

        if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5.0)
            set(_qgc_android_openssl_dir "ssl_3")
            set(_qgc_android_libcrypto "libcrypto_3.so")
            set(_qgc_android_libssl "libssl_3.so")
        else()
            set(_qgc_android_openssl_dir "ssl_1.1")
            set(_qgc_android_libcrypto "libcrypto_1_1.so")
            set(_qgc_android_libssl "libssl_1_1.so")
        endif()

        set(_qgc_android_openssl_libdir
            "${_qgc_android_openssl_root}/${_qgc_android_openssl_dir}/${CMAKE_ANDROID_ARCH_ABI}"
        )

        list(APPEND _qgc_android_openssl_libs
            "${_qgc_android_openssl_libdir}/${_qgc_android_libcrypto}"
            "${_qgc_android_openssl_libdir}/${_qgc_android_libssl}"
        )

        # Some loaders look for unversioned libssl.so/libcrypto.so. The KDAB
        # package provides them as symlinks, which don't copy well on Windows,
        # so create real copies in the build dir.
        set(_qgc_android_openssl_copy_dir
            "${CMAKE_BINARY_DIR}/android-openssl/${CMAKE_ANDROID_ARCH_ABI}"
        )
        file(MAKE_DIRECTORY "${_qgc_android_openssl_copy_dir}")
        if(EXISTS "${_qgc_android_openssl_libdir}/${_qgc_android_libssl}")
            configure_file(
                "${_qgc_android_openssl_libdir}/${_qgc_android_libssl}"
                "${_qgc_android_openssl_copy_dir}/libssl.so"
                COPYONLY
            )
            list(APPEND _qgc_android_openssl_libs
                "${_qgc_android_openssl_copy_dir}/libssl.so"
            )
        endif()
        if(EXISTS "${_qgc_android_openssl_libdir}/${_qgc_android_libcrypto}")
            configure_file(
                "${_qgc_android_openssl_libdir}/${_qgc_android_libcrypto}"
                "${_qgc_android_openssl_copy_dir}/libcrypto.so"
                COPYONLY
            )
            list(APPEND _qgc_android_openssl_libs
                "${_qgc_android_openssl_copy_dir}/libcrypto.so"
            )
        endif()
    endif()

    set(_qgc_android_openssl_libs_existing)
    foreach(_qgc_lib IN LISTS _qgc_android_openssl_libs)
        if(EXISTS "${_qgc_lib}")
            list(APPEND _qgc_android_openssl_libs_existing "${_qgc_lib}")
        else()
            message(WARNING "QGC: OpenSSL runtime library not found: ${_qgc_lib}")
        endif()
    endforeach()

    if(_qgc_android_openssl_libs_existing)
        set_property(TARGET ${CMAKE_PROJECT_NAME}
            APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
                ${_qgc_android_openssl_libs_existing}
        )
    endif()

    unset(_qgc_android_openssl_libs)
    unset(_qgc_android_openssl_libs_existing)
    unset(_qgc_android_openssl_root)
    unset(_qgc_android_openssl_dir)
    unset(_qgc_android_openssl_libdir)
    unset(_qgc_android_libcrypto)
    unset(_qgc_android_libssl)
    unset(_qgc_android_openssl_copy_dir)
    unset(_qgc_lib)
else()
    message(WARNING "QGC: Failed to add Android OpenSSL libraries")
endif()

# Ensure the OpenSSL-backed TLS plugin ships in the APK; without it Qt reports
# "no functional TLS backend was found" and encrypted sockets fail. Resolve the
# plugin directory from the Android Qt install (androiddeployqt expects a
# directory, not an individual .so path).
get_filename_component(_qt_cmake_dir "${Qt6_DIR}" DIRECTORY)      # .../lib/cmake
get_filename_component(_qt_prefix_dir "${_qt_cmake_dir}/../.." REALPATH) # Qt root
set(_qt_tls_plugin_dir "${_qt_prefix_dir}/plugins/tls")
if(IS_DIRECTORY "${_qt_tls_plugin_dir}")
    set_property(TARGET ${CMAKE_PROJECT_NAME}
        APPEND PROPERTY QT_ANDROID_EXTRA_PLUGINS
            "${_qt_tls_plugin_dir}"
    )
else()
    message(WARNING "QGC: TLS plugin directory not found: ${_qt_tls_plugin_dir}")
endif()

# ----------------------------------------------------------------------------
# Android Permissions
# ----------------------------------------------------------------------------

if(QGC_ENABLE_BLUETOOTH)
    qt_add_android_permission(${CMAKE_PROJECT_NAME}
        NAME android.permission.BLUETOOTH_SCAN
        ATTRIBUTES
            minSdkVersion 31
            usesPermissionFlags neverForLocation
    )
    qt_add_android_permission(${CMAKE_PROJECT_NAME}
        NAME android.permission.BLUETOOTH_CONNECT
        ATTRIBUTES
            minSdkVersion 31
            usesPermissionFlags neverForLocation
    )
endif()

if(NOT QGC_NO_SERIAL_LINK)
    qt_add_android_permission(${CMAKE_PROJECT_NAME}
        NAME android.permission.USB_PERMISSION
    )
endif()

# Need MulticastLock to receive broadcast UDP packets
qt_add_android_permission(${CMAKE_PROJECT_NAME}
    NAME android.permission.CHANGE_WIFI_MULTICAST_STATE
)

# Needed to keep working while 'asleep'
qt_add_android_permission(${CMAKE_PROJECT_NAME}
    NAME android.permission.WAKE_LOCK
)

# Needed for read/write to SD Card Path in AppSettings
qt_add_android_permission(${CMAKE_PROJECT_NAME}
    NAME android.permission.WRITE_EXTERNAL_STORAGE
    ATTRIBUTES
        maxSdkVersion 32
)
qt_add_android_permission(${CMAKE_PROJECT_NAME}
    NAME android.permission.READ_EXTERNAL_STORAGE
    ATTRIBUTES
        maxSdkVersion 33
)
qt_add_android_permission(${CMAKE_PROJECT_NAME}
    NAME android.permission.MANAGE_EXTERNAL_STORAGE
)

# Joystick
qt_add_android_permission(${CMAKE_PROJECT_NAME}
    NAME android.permission.VIBRATE
)

message(STATUS "QGC: Android platform configuration applied")
