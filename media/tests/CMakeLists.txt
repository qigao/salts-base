function(capture_original_test target source)
  cmake_add_test(${target} SOURCES ${source}
    FOLDER "media/tests")
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "media/tests")
  target_link_libraries(${target} PRIVATE Salts::Media Salts::TinyTest)
  set_tests_properties(${target} PROPERTIES TIMEOUT 60)
endfunction()

if(WIN32)
  # Original fake-COM control test compiles the exact Windows backend body.
  # This is an isolated regression, not a replacement production provider.
  cmake_add_test(capture_exposure_windows_test SOURCES test_capture_exposure_windows.c
    FOLDER "media/tests")
  target_compile_features(capture_exposure_windows_test PRIVATE c_std_11)
  # Test the actual single static Media archive alongside the source-owned
  # Windows MF control implementation. The native playback invalid-owner
  # assertions now also resolve from the same Salts::Media archive.
  target_link_libraries(capture_exposure_windows_test PRIVATE
    Salts::Media Salts::TinyTest yuv
    mfplat mf mfuuid mfreadwrite strmiids ole32 oleaut32)
  set_target_properties(capture_exposure_windows_test PROPERTIES FOLDER "media/tests")
  set_tests_properties(capture_exposure_windows_test PROPERTIES
    TIMEOUT 15)
  # This executable enumerates physical audio/video/screen/GPU devices and may
  # open camera modes. Build it on Windows to protect the API/ABI, but do not
  # register it in generic hosted-runner CTest. Device-backed qualification
  # belongs on an explicit hardware environment.
  cmake_add_executable(capture_windows_test SOURCES test_capture_windows.c
    FOLDER "media/tests")
  target_compile_features(capture_windows_test PRIVATE c_std_11)
  set_target_properties(capture_windows_test PROPERTIES
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "media/tests")
  target_link_libraries(capture_windows_test PRIVATE Salts::Media Salts::TinyTest)
endif()

if(ANDROID)
  capture_original_test(capture_android_test test_capture_android.c)
endif()

if(UNIX AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  capture_original_test(capture_posix_test test_capture_posix.c)
endif()

cmake_add_executable(playback_buffer_test
  SOURCES test_playback_buffer.c ../src/playback_buffer.c
  FOLDER "media/tests")
target_compile_features(playback_buffer_test PRIVATE c_std_11)
target_link_libraries(playback_buffer_test PRIVATE Salts::Core Salts::TinyTest)
target_include_directories(playback_buffer_test PRIVATE ../src)
add_test(NAME saltscore.playback_original_buffer COMMAND playback_buffer_test)

cmake_add_source(playback_test_sources DIRS ../src
  PATTERNS "playback*.c" "miniaudio*.c")
add_library(playback_null_test_impl STATIC ${playback_test_sources})
# This is the original deterministic test device. It is never installed or
# linked into the production Playback owner and does not replace its backend.
set_target_properties(playback_null_test_impl PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "media/tests")
if(APPLE)
  set_source_files_properties(../src/miniaudio_impl.c PROPERTIES LANGUAGE OBJC)
  target_link_libraries(playback_null_test_impl PRIVATE
    ${SALTS_MEDIA_AudioToolbox_FRAMEWORK}
    ${SALTS_MEDIA_CoreAudio_FRAMEWORK}
    ${SALTS_MEDIA_CoreFoundation_FRAMEWORK}
    ${SALTS_MEDIA_Foundation_FRAMEWORK}
    ${SALTS_MEDIA_AVFoundation_FRAMEWORK})
endif()
target_compile_features(playback_null_test_impl PUBLIC c_std_11)
target_compile_definitions(
  playback_null_test_impl PRIVATE SALTS_PLAYBACK_TEST_NULL_BACKEND=1)
set_target_properties(playback_null_test_impl PROPERTIES C_VISIBILITY_PRESET hidden)
target_include_directories(
  playback_null_test_impl
  PUBLIC ../include
  PRIVATE ../src ${SALTS_MEDIA_MINIAUDIO_INCLUDE_DIR})
target_link_libraries(
  playback_null_test_impl PRIVATE Salts::Core)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(playback_null_test_impl PRIVATE m)
endif()

cmake_add_executable(playback_lifecycle_test
  SOURCES test_playback_lifecycle.c FOLDER "media/tests")
target_compile_features(playback_lifecycle_test PRIVATE c_std_11)
target_link_libraries(playback_lifecycle_test PRIVATE playback_null_test_impl Salts::TinyTest)
add_test(NAME saltscore.playback_original_lifecycle COMMAND playback_lifecycle_test)
set_tests_properties(saltscore.playback_original_buffer
  saltscore.playback_original_lifecycle PROPERTIES TIMEOUT 30)


if(SALTSCORE_ENABLE_TSAN)
  # The root sanitizer preset instruments the one Media archive, including
  # miniaudio. The private null monitor never opens a physical audio device.
  find_program(SALTS_CAPTURE_BASH bash REQUIRED)
  find_program(SALTS_CAPTURE_PULSEAUDIO pulseaudio REQUIRED)
  find_program(SALTS_CAPTURE_PACTL pactl REQUIRED)
  find_program(SALTS_CAPTURE_PACAT pacat REQUIRED)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(salts_capture_native_tsan_regression native_tsan/test_native_capture_tsan.c)
  target_link_libraries(salts_capture_native_tsan_regression PRIVATE
    Salts::Media Salts::TinyTest Threads::Threads)
  set_target_properties(salts_capture_native_tsan_regression PROPERTIES
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "media/tests")
  add_test(NAME salts_capture_native_tsan_virtual_monitor
    COMMAND "${SALTS_CAPTURE_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/native_tsan/run_private_pulse.sh"
      $<TARGET_FILE:salts_capture_native_tsan_regression> "${Python3_EXECUTABLE}")
  set_tests_properties(salts_capture_native_tsan_virtual_monitor PROPERTIES TIMEOUT 60)
endif()

