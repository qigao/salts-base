foreach(_name IN ITEMS test_salts_unicode test_salts_unicode_normalize
    test_salts_unicode_casefold test_salts_unicode_combined_abi
    test_salts_unicode_category_conformance
    test_salts_unicode_grapheme test_salts_unicode_grapheme_conformance
    test_salts_unicode_word test_salts_unicode_word_conformance
    test_salts_unicode_line_break_conformance
    test_salts_unicode_bidi test_salts_unicode_bidi_conformance
    test_salts_unicode_emoji test_salts_unicode_emoji_conformance)
  cmake_add_executable("${_name}" SOURCES "${_name}.c"
    LIBS Salts::Unicode Salts::TinyTest FOLDER "unicode/test")
  target_compile_features("${_name}" PRIVATE c_std_11)
  set_target_properties("${_name}" PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME "saltscore.native.${_name}" COMMAND "${_name}")
  set_tests_properties("saltscore.native.${_name}" PROPERTIES TIMEOUT 300)
endforeach()
target_compile_definitions(test_salts_unicode_normalize PRIVATE
  "SALTS_NFC_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/NormalizationTest.txt\"")
target_compile_definitions(test_salts_unicode_casefold PRIVATE
  "SALTS_UNICODE_CASEFOLD_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/CaseFolding.txt\"")
target_compile_definitions(test_salts_unicode_category_conformance PRIVATE
  "SALTS_UNICODE_CATEGORY_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/UnicodeData.txt\"")
target_compile_definitions(test_salts_unicode_grapheme_conformance PRIVATE
  "SALTS_UNICODE_GRAPHEME_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/GraphemeBreakTest.txt\"")
target_compile_definitions(test_salts_unicode_word_conformance PRIVATE
  "SALTS_UNICODE_WORD_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/WordBreakTest.txt\"")
target_compile_definitions(test_salts_unicode_line_break_conformance PRIVATE
  "SALTS_UNICODE_LINE_BREAK_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/LineBreakTest.txt\"")
target_compile_definitions(test_salts_unicode_bidi_conformance PRIVATE
  "SALTS_UNICODE_BIDI_PARAGRAPH_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/BidiParagraphTest.txt\"")
target_compile_definitions(test_salts_unicode_emoji_conformance PRIVATE
  "SALTS_UNICODE_EMOJI_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/emoji-data.txt\"")


cmake_add_executable(test_salts_unicode_header_cpp SOURCES test_salts_unicode_header_cpp.cpp
  LIBS Salts::Unicode Salts::TinyTest FOLDER "unicode/test")
target_compile_features(test_salts_unicode_header_cpp PRIVATE cxx_std_17)
add_test(NAME saltscore.native.test_salts_unicode_header_cpp COMMAND test_salts_unicode_header_cpp)
set_tests_properties(saltscore.native.test_salts_unicode_header_cpp PROPERTIES TIMEOUT 120)
