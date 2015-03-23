# This was borrowed from the gnuradio repository.

# - Try to find jack-2.6
# Once done this will define
#
#  JACK_FOUND - system has jack
#  JACK_INCLUDE_DIRS - the jack include directory
#  JACK_LIBRARIES - Link these to use jack
#  JACK_DEFINITIONS - Compiler switches required for using jack
#
#  Copyright (c) 2008 Andreas Schneider <mail@cynapses.org>
#  Modified for other libraries by Lasse Kärkkäinen <tronic>
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#
if (JACK_LIBRARIES AND JACK_INCLUDE_DIRS)
  # in cache already
  set(JACK_FOUND TRUE)
else (JACK_LIBRARIES AND JACK_INCLUDE_DIRS)
  # use pkg-config to get the directories and then use these values
  # in the FIND_PATH() and FIND_LIBRARY() calls
  if (${CMAKE_MAJOR_VERSION} EQUAL 2 AND ${CMAKE_MINOR_VERSION} EQUAL 4)
    include(UsePkgConfig)
    pkgconfig(jack _JACK_INCLUDEDIR _JACK_LIBDIR _JACK_LDFLAGS _JACK_CFLAGS)
  else (${CMAKE_MAJOR_VERSION} EQUAL 2 AND ${CMAKE_MINOR_VERSION} EQUAL 4)
    find_package(PkgConfig)
    if (PKG_CONFIG_FOUND)
      pkg_check_modules(_JACK jack)
    endif (PKG_CONFIG_FOUND)
  endif (${CMAKE_MAJOR_VERSION} EQUAL 2 AND ${CMAKE_MINOR_VERSION} EQUAL 4)
  find_path(JACK_INCLUDE_DIR
    NAMES
      jack/jack.h
    PATHS
      ${_JACK_INCLUDEDIR}
      /usr/include
      /usr/local/include
      /opt/local/include
      /sw/include
  )

  find_library(JACK_LIBRARY
    NAMES
      jack
    PATHS
      ${_JACK_LIBDIR}
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /sw/lib
  )

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(JACK DEFAULT_MSG
    JACK_INCLUDE_DIRS JACK_LIBRARIES)

  # show the JACK_INCLUDE_DIRS and JACK_LIBRARIES variables only in the advanced view
  mark_as_advanced(JACK_INCLUDE_DIRS JACK_LIBRARIES)
endif (JACK_LIBRARIES AND JACK_INCLUDE_DIRS)