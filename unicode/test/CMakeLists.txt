include("${CMAKE_CURRENT_SOURCE_DIR}/../data/Download.cmake")
foreach(_fixture IN ITEMS NormalizationTest.txt CaseFolding.txt UnicodeData.txt
    GraphemeBreakTest.txt WordBreakTest.txt LineBreakTest.txt emoji-data.txt)
  download_unicode_file("${_fixture}")
endforeach()
unset(_fixture)

foreach(_name IN ITEMS test_unicode test_unicode_normalize
    test_unicode_casefold test_unicode_combined_abi
    test_unicode_category_conformance
    test_unicode_grapheme test_unicode_grapheme_conformance
    test_unicode_word test_unicode_word_conformance
    test_unicode_line_break_conformance
    test_unicode_bidi test_unicode_bidi_conformance
    test_unicode_emoji test_unicode_emoji_conformance)
  cmake_add_executable("${_name}" SOURCES "${_name}.c"
    LIBS Salts::Unicode Salts::TinyTest FOLDER "unicode/test")
  target_compile_features("${_name}" PRIVATE c_std_11)
  set_target_properties("${_name}" PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME "saltscore.native.${_name}" COMMAND "${_name}")
  set_tests_properties("saltscore.native.${_name}" PROPERTIES TIMEOUT 300)
endforeach()
target_compile_definitions(test_unicode_normalize PRIVATE
  "SALTS_NFC_TEST_DATA=\"${SALTS_UNICODE_DATA_DIR}/NormalizationTest.txt\"")
target_compile_definitions(test_unicode_casefold PRIVATE
  "SALTS_UNICODE_CASEFOLD_TEST_DATA=\"${SALTS_UNICODE_DATA_DIR}/CaseFolding.txt\"")
target_compile_definitions(test_unicode_category_conformance PRIVATE
  "SALTS_UNICODE_CATEGORY_TEST_DATA=\"${SALTS_UNICODE_DATA_DIR}/UnicodeData.txt\"")
target_compile_definitions(test_unicode_grapheme_conformance PRIVATE
  "SALTS_UNICODE_GRAPHEME_TEST_DATA=\"${SALTS_UNICODE_DATA_DIR}/GraphemeBreakTest.txt\"")
target_compile_definitions(test_unicode_word_conformance PRIVATE
  "SALTS_UNICODE_WORD_TEST_DATA=\"${SALTS_UNICODE_DATA_DIR}/WordBreakTest.txt\"")
target_compile_definitions(test_unicode_line_break_conformance PRIVATE
  "SALTS_UNICODE_LINE_BREAK_TEST_DATA=\"${SALTS_UNICODE_DATA_DIR}/LineBreakTest.txt\"")
target_compile_definitions(test_unicode_bidi_conformance PRIVATE
  "SALTS_UNICODE_BIDI_PARAGRAPH_TEST_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data/BidiParagraphTest.txt\"")
target_compile_definitions(test_unicode_emoji_conformance PRIVATE
  "SALTS_UNICODE_EMOJI_TEST_DATA=\"${SALTS_UNICODE_DATA_DIR}/emoji-data.txt\"")


cmake_add_executable(test_unicode_header_cpp SOURCES test_unicode_header_cpp.cpp
  LIBS Salts::Unicode Salts::TinyTest FOLDER "unicode/test")
target_compile_features(test_unicode_header_cpp PRIVATE cxx_std_17)
add_test(NAME saltscore.native.test_unicode_header_cpp COMMAND test_unicode_header_cpp)
set_tests_properties(saltscore.native.test_unicode_header_cpp PROPERTIES TIMEOUT 120)
