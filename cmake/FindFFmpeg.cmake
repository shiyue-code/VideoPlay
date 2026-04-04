# Find FFmpeg libraries
#
# This module finds the FFmpeg libraries.
#
# The following variables are set:
#  FFmpeg_FOUND - True if FFmpeg is found
#  FFmpeg_INCLUDE_DIRS - The include directories
#  FFmpeg_LIBRARIES - The libraries to link
#  FFmpeg_VERSION - The version of FFmpeg
#
# This module will set the following imported targets:
#  FFmpeg::AVCodec
#  FFmpeg::AVFormat
#  FFmpeg::AVUtil
#  FFmpeg::SWScale
#  FFmpeg::SWResample

find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_FFmpeg QUIET libavcodec libavformat libavutil libswscale libswresample)
endif()

find_path(FFmpeg_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    HINTS ${PC_FFmpeg_INCLUDE_DIRS}
          ${FFmpeg_ROOT}
          ${FFmpeg_ROOT}/include
          /usr/include
          /usr/local/include
          /opt/local/include
          /sw/include
          /opt/homebrew/include
    PATH_SUFFIXES ffmpeg
)

find_library(FFmpeg_AVCODEC_LIBRARY
    NAMES avcodec libavcodec
    HINTS ${PC_FFmpeg_LIBRARY_DIRS}
          ${FFmpeg_ROOT}
          ${FFmpeg_ROOT}/lib
          ${FFmpeg_ROOT}/lib64
          /usr/lib
          /usr/local/lib
          /opt/local/lib
          /sw/lib
          /opt/homebrew/lib
)

find_library(FFmpeg_AVFORMAT_LIBRARY
    NAMES avformat libavformat
    HINTS ${PC_FFmpeg_LIBRARY_DIRS}
          ${FFmpeg_ROOT}
          ${FFmpeg_ROOT}/lib
          ${FFmpeg_ROOT}/lib64
          /usr/lib
          /usr/local/lib
          /opt/local/lib
          /sw/lib
          /opt/homebrew/lib
)

find_library(FFmpeg_AVUTIL_LIBRARY
    NAMES avutil libavutil
    HINTS ${PC_FFmpeg_LIBRARY_DIRS}
          ${FFmpeg_ROOT}
          ${FFmpeg_ROOT}/lib
          ${FFmpeg_ROOT}/lib64
          /usr/lib
          /usr/local/lib
          /opt/local/lib
          /sw/lib
          /opt/homebrew/lib
)

find_library(FFmpeg_SWSCALE_LIBRARY
    NAMES swscale libswscale
    HINTS ${PC_FFmpeg_LIBRARY_DIRS}
          ${FFmpeg_ROOT}
          ${FFmpeg_ROOT}/lib
          ${FFmpeg_ROOT}/lib64
          /usr/lib
          /usr/local/lib
          /opt/local/lib
          /sw/lib
          /opt/homebrew/lib
)

find_library(FFmpeg_SWRESAMPLE_LIBRARY
    NAMES swresample libswresample
    HINTS ${PC_FFmpeg_LIBRARY_DIRS}
          ${FFmpeg_ROOT}
          ${FFmpeg_ROOT}/lib
          ${FFmpeg_ROOT}/lib64
          /usr/lib
          /usr/local/lib
          /opt/local/lib
          /sw/lib
          /opt/homebrew/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS 
        FFmpeg_INCLUDE_DIR
        FFmpeg_AVCODEC_LIBRARY
        FFmpeg_AVFORMAT_LIBRARY
        FFmpeg_AVUTIL_LIBRARY
        FFmpeg_SWSCALE_LIBRARY
        FFmpeg_SWRESAMPLE_LIBRARY
)

if(FFmpeg_FOUND)
    set(FFmpeg_INCLUDE_DIRS ${FFmpeg_INCLUDE_DIR})
    set(FFmpeg_LIBRARIES 
        ${FFmpeg_AVCODEC_LIBRARY}
        ${FFmpeg_AVFORMAT_LIBRARY}
        ${FFmpeg_AVUTIL_LIBRARY}
        ${FFmpeg_SWSCALE_LIBRARY}
        ${FFmpeg_SWRESAMPLE_LIBRARY}
    )
    
    # 创建导入目标
    if(NOT TARGET FFmpeg::AVCodec)
        add_library(FFmpeg::AVCodec UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::AVCodec PROPERTIES
            IMPORTED_LOCATION ${FFmpeg_AVCODEC_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${FFmpeg_INCLUDE_DIR}
        )
    endif()
    
    if(NOT TARGET FFmpeg::AVFormat)
        add_library(FFmpeg::AVFormat UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::AVFormat PROPERTIES
            IMPORTED_LOCATION ${FFmpeg_AVFORMAT_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${FFmpeg_INCLUDE_DIR}
        )
    endif()
    
    if(NOT TARGET FFmpeg::AVUtil)
        add_library(FFmpeg::AVUtil UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::AVUtil PROPERTIES
            IMPORTED_LOCATION ${FFmpeg_AVUTIL_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${FFmpeg_INCLUDE_DIR}
        )
    endif()
    
    if(NOT TARGET FFmpeg::SWScale)
        add_library(FFmpeg::SWScale UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::SWScale PROPERTIES
            IMPORTED_LOCATION ${FFmpeg_SWSCALE_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${FFmpeg_INCLUDE_DIR}
        )
    endif()
    
    if(NOT TARGET FFmpeg::SWResample)
        add_library(FFmpeg::SWResample UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::SWResample PROPERTIES
            IMPORTED_LOCATION ${FFmpeg_SWRESAMPLE_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${FFmpeg_INCLUDE_DIR}
        )
    endif()
endif()

mark_as_advanced(
    FFmpeg_INCLUDE_DIR
    FFmpeg_AVCODEC_LIBRARY
    FFmpeg_AVFORMAT_LIBRARY
    FFmpeg_AVUTIL_LIBRARY
    FFmpeg_SWSCALE_LIBRARY
    FFmpeg_SWRESAMPLE_LIBRARY
)