# Find the inih XML parsing library.
#
# Sets the usual variables expected for find_package scripts:
#
# INIH_INCLUDE_DIR - header location
# INIH_LIBRARIES - library to link against
# INIH_FOUND - true if inih was found.

find_path (INIH_INCLUDE_DIR
           NAMES INIReader.h
           PATHS ${INIH_HOME}/include)
find_library (INIH_LIBRARY
              NAMES inih
              PATHS ${INIH_HOME}/lib)

# Support the REQUIRED and QUIET arguments, and set INIH_FOUND if found.
include (FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS (inih DEFAULT_MSG INIH_LIBRARY
                                   INIH_INCLUDE_DIR)

if (INIH_FOUND)
    set (INIH_LIBRARIES ${INIH_LIBRARY})
    if (NOT inih_FIND_QUIETLY)
        message (STATUS "inih include = ${INIH_INCLUDE_DIR}")
        message (STATUS "inih library = ${INIH_LIBRARY}")
    endif ()
else ()
    message (STATUS "No inih found")
endif()

mark_as_advanced (INIH_LIBRARY INIH_INCLUDE_DIR)
