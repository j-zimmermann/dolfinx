# - Try to find BLAS
# Once done this will define
#
#  BLAS_FOUND        - system has BLAS
#  BLAS_LIBRARIES    - libraries for BLAS
#
# This test is necessary since the CMake supplied FindBLAS script
# only looks for Fortran BLAS.
#
# This test currently assumes ATLAS BLAS but we could easily
# expand it to other vendor versions.

message(STATUS "Checking for package 'BLAS'")

# Check for library
find_library(BLAS_LIBRARIES
  NAMES atlas cblas blas
  PATHS ${BLAS_DIR}/lib $ENV{BLAS_DIR}/lib
  PATH_SUFFIXES atlas-amd64sse3 atlas
  DOC "The BLAS library"
  )
mark_as_advanced(BLAS_LIBRARIES)

# Standard package handling
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BLAS
  "BLAS could not be found. Be sure to set BLAS_DIR." BLAS_LIBRARIES)