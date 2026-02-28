if(TARGET libzip::libzip)
  set(libzip_FOUND TRUE)
  set(LIBZIP_FOUND TRUE)
  return()
endif()

if(TARGET zip::zip)
  add_library(libzip::zip ALIAS zip::zip)
  set(libzip_FOUND TRUE)
  set(LIBZIP_FOUND TRUE)
  return()
endif()

if(TARGET libzip::zip)
  set(libzip_FOUND TRUE)
  set(LIBZIP_FOUND TRUE)
  return()
endif()

if(TARGET zip)
  add_library(libzip::zip ALIAS zip)
  add_library(zip::zip ALIAS zip)
  set(libzip_FOUND TRUE)
  set(LIBZIP_FOUND TRUE)
  return()
endif()

find_path(LIBZIP_INCLUDE_DIR zip.h)
find_library(LIBZIP_LIBRARY NAMES zip libzip)
if(LIBZIP_INCLUDE_DIR AND LIBZIP_LIBRARY)
  add_library(libzip::zip UNKNOWN IMPORTED)
  set_target_properties(libzip::zip PROPERTIES
    IMPORTED_LOCATION "${LIBZIP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBZIP_INCLUDE_DIR}"
  )
  add_library(zip::zip ALIAS libzip::zip)
  set(libzip_FOUND TRUE)
  set(LIBZIP_FOUND TRUE)
  return()
endif()

set(libzip_FOUND FALSE)
set(LIBZIP_FOUND FALSE)
