#=============================================================================
# @brief  : HIF cmake file.
# @author : Enrico Fraccaroli
# @create : 03/01/2022
# @update : 03/01/2022
#
# This module defines
#   HIF_LIBRARY     : the name of the library to link against
#   HIF_FOUND       : if false, do not try to link to HIF
#   HIF_INCLUDE_DIR : where to find hif.hpp
#=============================================================================

include(FindPackageHandleStandardArgs)

SET(HIF_SEARCH_PATHS
	# User defined directory.
	${HIF_DIR}
    $ENV{HIF_DIR}
	# Non-installed core build directory.
    ${CMAKE_SOURCE_DIR}/../hif-core.private/build
    ${CMAKE_SOURCE_DIR}/../hif-core.private
    ${CMAKE_SOURCE_DIR}/../hif-core/build
    ${CMAKE_SOURCE_DIR}/../hif-core/
	# Installed core directory.
	/usr/local
	/usr
)

# Find the include directory.
FIND_PATH(HIF_INCLUDE_DIR
	hif/hif.hpp
	HINTS			$ENV{HIF_DIR}
	PATH_SUFFIXES	include
	PATHS			${HIF_SEARCH_PATHS}
)

# Find the library.
FIND_LIBRARY(HIF_LIBRARY
	NAMES hif
	HINTS			$ENV{HIF_DIR}
	PATH_SUFFIXES	lib lib64
	PATHS			${HIF_SEARCH_PATHS}
)

# Find the package.
find_package_handle_standard_args(
    HIF
    DEFAULT_MSG
    HIF_LIBRARY
    HIF_INCLUDE_DIR
)
