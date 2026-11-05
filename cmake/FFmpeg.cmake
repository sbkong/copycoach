# FFmpeg from vcpkg (port 9.0.2, LGPL 2.1+: no gpl / nonfree / version3 feature, docs/features/video.md section 9),
# linked dynamically as the LGPL asks, used for video only (audio decoding is libsndfile). The manifest requests
# avcodec, avformat, swresample and swscale; the port's FindFFMPEG module hands back variables, wrapped here as the
# target cc_ffmpeg so core/ and tests/ link one name.
find_package(FFMPEG REQUIRED)
add_library(cc_ffmpeg INTERFACE)
target_include_directories(cc_ffmpeg SYSTEM INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(cc_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(cc_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
