if(APPLE)
  enable_language(OBJC)
endif()

cmake_add_source(media_sources DIRS src PATTERNS "*.c"
  EXCLUDES src/*_win32.c src/*_linux.c src/*_macos.c src/*_miniaudio.c)
add_library(media STATIC ${media_sources})
cmake_config_target(media ALIAS Salts::Media FOLDER "media")
cmake_config_target(media
  EXPORT_NAME Media
  OUTPUT_NAME saltscore_media)
set_target_properties(media PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
cmake_add_source(module_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_headers})
target_sources(media PRIVATE ${module_headers})
target_compile_features(media PUBLIC c_std_11)
target_include_directories(media
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(media PUBLIC
  Salts::Core)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
  target_compile_definitions(media PRIVATE _GNU_SOURCE=1)
endif()

# One miniaudio implementation for Capture and Playback. Its C symbols are
# compiled only once into the Media archive, including on iOS.
find_path(SALTS_MEDIA_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h
  PATHS "${SALTS_DEPENDENCY_PREFIX}/include" NO_DEFAULT_PATH REQUIRED)
find_file(SALTS_MEDIA_MINIAUDIO_LICENSE NAMES copyright
  PATHS "${SALTS_MEDIA_MINIAUDIO_INCLUDE_DIR}/../share/miniaudio"
  NO_DEFAULT_PATH REQUIRED)
target_include_directories(media PRIVATE
  ${SALTS_MEDIA_MINIAUDIO_INCLUDE_DIR})
set_source_files_properties(src/miniaudio_impl.c
  PROPERTIES COMPILE_OPTIONS "-w")
if(APPLE)
  set_source_files_properties(src/miniaudio_impl.c PROPERTIES LANGUAGE OBJC)
endif()
install(FILES "${SALTS_MEDIA_MINIAUDIO_LICENSE}"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/miniaudio"
  RENAME LICENSE)

# The static owner publishes actual link dependencies. The installed package
# resolves them explicitly rather than serializing build-machine archive paths.
if(WIN32 OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(JPEG REQUIRED)
  find_package(libyuv CONFIG REQUIRED
    PATHS "${SALTS_DEPENDENCY_PREFIX}/share/libyuv" NO_DEFAULT_PATH)
  target_link_libraries(media PRIVATE yuv JPEG::JPEG)
  find_file(SALTS_MEDIA_LIBYUV_LICENSE NAMES copyright
    PATHS "${libyuv_DIR}"
    NO_DEFAULT_PATH REQUIRED)
  install(FILES "${SALTS_MEDIA_LIBYUV_LICENSE}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/libyuv"
    RENAME LICENSE)
endif()
if(WIN32)
  find_package(libjpeg-turbo CONFIG REQUIRED
    PATHS "${SALTS_DEPENDENCY_PREFIX}/share/libjpeg-turbo" NO_DEFAULT_PATH)
  find_file(SALTS_MEDIA_JPEG_LICENSE NAMES copyright
    PATHS "${libjpeg-turbo_DIR}" NO_DEFAULT_PATH REQUIRED)
  install(FILES "${SALTS_MEDIA_JPEG_LICENSE}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/libjpeg-turbo"
    RENAME LICENSE)
endif()

if(ANDROID)
  cmake_add_source(media_backend_sources DIRS src/android PATTERNS "*.c" "*.h")
  find_library(SALTS_MEDIA_ANDROID_LIBRARY android REQUIRED)
  find_library(SALTS_MEDIA_OPENSL_LIBRARY OpenSLES REQUIRED)
  find_library(SALTS_MEDIA_MEDIANDK_LIBRARY mediandk REQUIRED)
  find_library(SALTS_MEDIA_CAMERA2_LIBRARY camera2ndk REQUIRED)
  target_compile_definitions(media PRIVATE SALTS_CAPTURE_ANDROID=1)
  target_link_libraries(media PRIVATE
    ${SALTS_MEDIA_ANDROID_LIBRARY}
    ${SALTS_MEDIA_OPENSL_LIBRARY}
    ${SALTS_MEDIA_MEDIANDK_LIBRARY}
    ${SALTS_MEDIA_CAMERA2_LIBRARY}
    ${CMAKE_DL_LIBS})
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  cmake_add_source(media_backend_sources DIRS src/ios PATTERNS "*.m" "*.h")
  target_include_directories(media PRIVATE src/ios)
  target_compile_definitions(media PRIVATE SALTS_CAPTURE_IOS=1)
  target_compile_options(media PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>")
  foreach(framework IN ITEMS Foundation AVFoundation CoreMedia CoreVideo
      AudioToolbox CoreAudio ReplayKit UIKit)
    find_library(SALTS_MEDIA_${framework}_FRAMEWORK ${framework} REQUIRED)
    target_link_libraries(media PRIVATE
      ${SALTS_MEDIA_${framework}_FRAMEWORK})
  endforeach()
  set_target_properties(media PROPERTIES
  XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0" XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
elseif(WIN32)
  cmake_add_source(media_backend_sources DIRS src
    PATTERNS "*_win32.c" "*_miniaudio.c")
  target_link_libraries(media PRIVATE
    mfplat mf mfuuid mfreadwrite strmiids ole32 oleaut32 d3d11 dxgi dxguid)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
  find_package(X11 REQUIRED COMPONENTS Xext)
  cmake_add_source(media_backend_sources DIRS src
    PATTERNS "*_linux.c" "*_miniaudio.c")
  target_compile_definitions(media PRIVATE SALTS_HAS_PIPEWIRE SALTS_HAS_X11)
  target_include_directories(media SYSTEM PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS} ${X11_INCLUDE_DIR})
  target_compile_options(media PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
  target_link_directories(media PRIVATE ${PIPEWIRE_LIBRARY_DIRS})
  target_link_options(media PRIVATE ${PIPEWIRE_LDFLAGS_OTHER})
  target_link_libraries(media PRIVATE
    Threads::Threads ${PIPEWIRE_LINK_LIBRARIES}
    ${X11_LIBRARIES} ${X11_Xext_LIB} ${CMAKE_DL_LIBS} m)
elseif(APPLE)
  cmake_add_source(media_backend_sources DIRS src
    PATTERNS "*_macos.c" "*_macos.m" "*_miniaudio.c")
  foreach(framework IN ITEMS Foundation AVFoundation CoreAudio AudioToolbox
      CoreFoundation CoreGraphics CoreMedia CoreVideo ScreenCaptureKit)
    find_library(SALTS_MEDIA_${framework}_FRAMEWORK ${framework} REQUIRED)
    target_link_libraries(media PRIVATE
      ${SALTS_MEDIA_${framework}_FRAMEWORK})
  endforeach()
endif()

target_sources(media PRIVATE ${media_backend_sources})

install(TARGETS media
  EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
