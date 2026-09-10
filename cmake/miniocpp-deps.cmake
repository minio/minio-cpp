# miniocpp-deps.cmake -- resolve the third-party dependencies of minio-cpp.
# Shared with the installed miniocpp-config.cmake.
#
# Resolution order per dependency: vcpkg CONFIG package -> pkg-config ->
# upstream source.  The source branch is for distros where vcpkg is
# impractical (its default setup downloads glibc-linked tools that do not run
# on musl) and no C++ INIReader packages exist.  It clones at
# configure time with plain git, so it works on the CMake 3.13.4 floor (no
# FetchContent); vcpkg builds never reach it.
#
# Defines for the caller:
#   MINIO_CPP_DEPS_LINK_LIBS      -- link targets, in link order
#   MINIO_CPP_DEPS_EXPORT_TARGETS -- source-built targets the caller must
#                                    install into an export set
#   MINIO_CPP_PC_REQUIRES_PRIVATE -- pkg-config modules a static consumer needs
#   MINIO_CPP_PC_LIBS_PRIVATE     -- link flags for private deps with no .pc

set(MINIO_CPP_DEPS_EXPORT_TARGETS)

# miniocpp.pc cannot see target_link_libraries(), so the dependencies that
# reach a consumer only through libminio.a have to be recorded here, where the
# resolution branch that picked them is known.  Each branch below appends to
# one of these; a hardcoded list in miniocpp.pc.in goes stale as soon as the
# dependency set moves, which is what left INIReader and brotli out of it.
set(MINIO_CPP_PC_REQUIRES_PRIVATE)
set(MINIO_CPP_PC_LIBS_PRIVATE)

# Requires.private is preferred over a bare -l: these archives live in the
# dependency's own prefix (a vcpkg tree, say), not in miniocpp's libdir, so
# only the module's own .pc supplies the -L that finds them.  The fallback is
# for a dependency installed into miniocpp's libdir, which -L${libdir} on the
# Libs line already covers.
macro(miniocpp_pc_private_dep module fallback)
  set(_miniocpp_pc_ok FALSE)
  if (PkgConfig_FOUND)
    # A per-module result prefix: pkg_check_modules caches under it, and a
    # stale hit would name a module that is not installed, which fails every
    # later pkg-config query outright rather than merely under-linking.
    pkg_check_modules(_miniocpp_pc_probe_${module} QUIET ${module})
    if (_miniocpp_pc_probe_${module}_FOUND)
      set(_miniocpp_pc_ok TRUE)
    endif()
  endif()
  if (_miniocpp_pc_ok)
    list(APPEND MINIO_CPP_PC_REQUIRES_PRIVATE ${module})
  elseif (NOT "${fallback}" STREQUAL "")
    list(APPEND MINIO_CPP_PC_LIBS_PRIVATE ${fallback})
  endif()
endmacro()

find_package(PkgConfig QUIET)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# cpp-httplib -- header-only; vcpkg -> pkg-config -> upstream source (pinned
# tag).  The code needs the progress overloads and set_max_timeout added in
# 0.19; require 0.51 (the current vcpkg port) so ancient distro packages
# cannot be selected.
find_package(httplib CONFIG QUIET)
if (httplib_FOUND AND DEFINED httplib_VERSION AND
    httplib_VERSION VERSION_LESS "0.51")
  message(STATUS "cpp-httplib ${httplib_VERSION} is too old; falling back")
  set(httplib_FOUND FALSE)
endif()
if (httplib_FOUND)
  set(MINIO_CPP_HTTPLIB_TARGET httplib::httplib)
else()
  if (PkgConfig_FOUND)
    pkg_check_modules(MINIO_CPP_HTTPLIB QUIET IMPORTED_TARGET cpp-httplib)
  endif()
  if (MINIO_CPP_HTTPLIB_FOUND AND DEFINED MINIO_CPP_HTTPLIB_VERSION AND
      MINIO_CPP_HTTPLIB_VERSION VERSION_LESS "0.51")
    message(STATUS "cpp-httplib ${MINIO_CPP_HTTPLIB_VERSION} is too old")
    set(MINIO_CPP_HTTPLIB_FOUND FALSE)
  endif()
  if (MINIO_CPP_HTTPLIB_FOUND)
    set(MINIO_CPP_HTTPLIB_TARGET PkgConfig::MINIO_CPP_HTTPLIB)
  else()
    message(STATUS "cpp-httplib: no usable package found, building from source")
    set(MINIO_CPP_HTTPLIB_SRC "${CMAKE_CURRENT_BINARY_DIR}/_deps/cpp-httplib-src")
    set(MINIO_CPP_HTTPLIB_PINNED_TAG "v0.53.1")
    if (NOT EXISTS "${MINIO_CPP_HTTPLIB_SRC}/CMakeLists.txt")
      execute_process(COMMAND git clone --quiet
              https://github.com/yhirose/cpp-httplib.git
              "${MINIO_CPP_HTTPLIB_SRC}"
              RESULT_VARIABLE _httplib_clone)
      if (NOT _httplib_clone STREQUAL "0")
        message(FATAL_ERROR "cpp-httplib: git clone failed")
      endif()
    endif()
    # Fetch tags so a cached clone can resolve the pinned tag.
    execute_process(COMMAND git fetch --quiet --tags origin
            WORKING_DIRECTORY "${MINIO_CPP_HTTPLIB_SRC}"
            RESULT_VARIABLE _httplib_fetch)
    if (NOT _httplib_fetch STREQUAL "0")
      message(FATAL_ERROR "cpp-httplib: git fetch failed")
    endif()
    execute_process(COMMAND git checkout --quiet
            ${MINIO_CPP_HTTPLIB_PINNED_TAG}
            WORKING_DIRECTORY "${MINIO_CPP_HTTPLIB_SRC}"
            RESULT_VARIABLE _httplib_checkout)
    if (NOT _httplib_checkout STREQUAL "0")
      message(FATAL_ERROR "cpp-httplib: git checkout of pinned tag failed")
    endif()
    # cpp-httplib is header-only; enable its CMake install target so that
    # find_package(httplib) works for downstream consumers after install.
    set(HTTPLIB_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${MINIO_CPP_HTTPLIB_SRC}"
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/cpp-httplib-build")
    set(MINIO_CPP_HTTPLIB_TARGET httplib::httplib)
  endif()
endif()

# Brotli is not a dependency minio-cpp declares: it reaches libminio.a through
# cpp-httplib, which links it whenever it was built with CPPHTTPLIB_BROTLI_SUPPORT
# (the vcpkg port is).  A consumer linking statically therefore needs it, so
# work out whether the httplib that was actually resolved uses it.  Its config
# package exports HTTPLIB_IS_USING_BROTLI; the source build sets that only in
# its own directory scope, so fall back to the compile definition the target
# carries, whose BOOL genex is already expanded at this point.  Do not match
# CPPHTTPLIB_BROTLI_SUPPORT as a plain substring: httplib lists the features it
# was built without in exactly the same way, wrapped in a false genex.
set(MINIO_CPP_HTTPLIB_USES_BROTLI FALSE)
if (DEFINED HTTPLIB_IS_USING_BROTLI)
  set(MINIO_CPP_HTTPLIB_USES_BROTLI ${HTTPLIB_IS_USING_BROTLI})
elseif (TARGET ${MINIO_CPP_HTTPLIB_TARGET})
  get_target_property(_miniocpp_httplib_defs
                      ${MINIO_CPP_HTTPLIB_TARGET} INTERFACE_COMPILE_DEFINITIONS)
  if (_miniocpp_httplib_defs MATCHES "\\$<\\$<BOOL:([^>]*)>:CPPHTTPLIB_BROTLI_SUPPORT>")
    if (CMAKE_MATCH_1)
      set(MINIO_CPP_HTTPLIB_USES_BROTLI TRUE)
    endif()
  elseif (_miniocpp_httplib_defs MATCHES "(^|;)CPPHTTPLIB_BROTLI_SUPPORT(;|$)")
    set(MINIO_CPP_HTTPLIB_USES_BROTLI TRUE)
  endif()
endif()
if (MINIO_CPP_HTTPLIB_USES_BROTLI)
  miniocpp_pc_private_dep(libbrotlienc "-lbrotlienc")
  miniocpp_pc_private_dep(libbrotlidec "-lbrotlidec")
  miniocpp_pc_private_dep(libbrotlicommon "-lbrotlicommon")
endif()

# inih -- Alpine ships only the C library; build the C++ INIReader from source
# (inih is meson-only, hence the manual target).  An installed
# miniocpp::miniocpp_inih (shipped with the miniocpp install) is reused as-is.
if (TARGET miniocpp::miniocpp_inih)
  set(MINIO_CPP_INIH_TARGET miniocpp::miniocpp_inih)
  list(APPEND MINIO_CPP_PC_LIBS_PRIVATE -lminiocpp_inih)
else()
  find_package(unofficial-inih CONFIG QUIET)
  if (unofficial-inih_FOUND)
    set(MINIO_CPP_INIH_TARGET unofficial::inih::inireader)
    miniocpp_pc_private_dep(INIReader "-lINIReader -linih")
  else()
    if (PkgConfig_FOUND)
      pkg_check_modules(MINIO_CPP_INIREADER QUIET IMPORTED_TARGET inireader)
    endif()
    if (MINIO_CPP_INIREADER_FOUND)
      set(MINIO_CPP_INIH_TARGET PkgConfig::MINIO_CPP_INIREADER)
      list(APPEND MINIO_CPP_PC_REQUIRES_PRIVATE inireader)
    else()
      message(STATUS "inih INIReader: no package found, building from source")
      set(MINIO_CPP_INIH_SRC "${CMAKE_CURRENT_BINARY_DIR}/_deps/inih-src")
      if (NOT EXISTS "${MINIO_CPP_INIH_SRC}/ini.h")
        execute_process(COMMAND git clone --quiet
                        https://github.com/benhoyt/inih.git
                        "${MINIO_CPP_INIH_SRC}"
                        RESULT_VARIABLE _inih_clone)
        if (NOT _inih_clone STREQUAL "0")
          message(FATAL_ERROR "inih: git clone failed")
        endif()
      endif()
      # Also reset a cached checkout to the pinned commit, not just a fresh
      # clone.
      execute_process(COMMAND git checkout --quiet
                      5cc5e2c24642513aaa5b19126aad42d0e4e0923e # r58
                      WORKING_DIRECTORY "${MINIO_CPP_INIH_SRC}"
                      RESULT_VARIABLE _inih_checkout)
      if (NOT _inih_checkout STREQUAL "0")
        message(FATAL_ERROR "inih: git checkout of pinned commit failed")
      endif()
      add_library(miniocpp_inih STATIC
        "${MINIO_CPP_INIH_SRC}/ini.c"
        "${MINIO_CPP_INIH_SRC}/cpp/INIReader.cpp"
      )
      # BUILD_INTERFACE only: the cloned tree lives in the build dir.
      target_include_directories(miniocpp_inih PUBLIC
        $<BUILD_INTERFACE:${MINIO_CPP_INIH_SRC}/cpp>)
      set_target_properties(miniocpp_inih PROPERTIES POSITION_INDEPENDENT_CODE ON)
      set(MINIO_CPP_INIH_TARGET miniocpp_inih)
      list(APPEND MINIO_CPP_DEPS_EXPORT_TARGETS miniocpp_inih)
      list(APPEND MINIO_CPP_PC_LIBS_PRIVATE -lminiocpp_inih)
    endif()
  endif()
endif()

find_package(pugixml CONFIG QUIET)
if (pugixml_FOUND)
  set(MINIO_CPP_PUGIXML_TARGET pugixml)
else()
  if (PkgConfig_FOUND)
    pkg_check_modules(MINIO_CPP_PUGIXML QUIET IMPORTED_TARGET pugixml)
  endif()
  if (NOT MINIO_CPP_PUGIXML_FOUND)
    message(FATAL_ERROR
      "pugixml: neither a CMake package (pugixml) nor pkg-config (pugixml.pc) was found")
  endif()
  set(MINIO_CPP_PUGIXML_TARGET PkgConfig::MINIO_CPP_PUGIXML)
endif()

set(MINIO_CPP_DEPS_LINK_LIBS
  ${MINIO_CPP_HTTPLIB_TARGET}
  ${MINIO_CPP_INIH_TARGET}
  nlohmann_json::nlohmann_json
  ${MINIO_CPP_PUGIXML_TARGET}
  OpenSSL::SSL
  OpenSSL::Crypto
  ZLIB::ZLIB
)
