# The RichDoc document/visitor assertions formerly living in Stun ui_core_test.c.
add_executable(salts_markdown_document_view_test markdown_document_view_test.c)
target_compile_features(salts_markdown_document_view_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_document_view_test PRIVATE Salts::MarkdownParser)
add_test(NAME salts_markdown_document_view_test COMMAND salts_markdown_document_view_test)

add_executable(salts_markdown_c11_test markdown_c11_test.c)
target_compile_features(salts_markdown_c11_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_c11_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_c11_test COMMAND salts_markdown_c11_test)

# #185: canonical HTML5 named/numeric character references through one C11
# frontend; independent native AST and exact HTML byte gates.
add_executable(salts_markdown_entities_test markdown_entities_test.c)
target_compile_features(salts_markdown_entities_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_entities_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_entities_test COMMAND salts_markdown_entities_test)

# #183: typed decoded link destinations and optional title metadata. Native
# C11 owner/URI/safety tests run under the installed-SDK sanitizer gate.
add_executable(salts_markdown_link_target_test markdown_link_target_test.c)
target_compile_features(salts_markdown_link_target_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_link_target_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_link_target_test
         COMMAND salts_markdown_link_target_test)

# #183/#184: URI autolink grammar is scheme-neutral; the existing SAFE
# HTML writer never emits an unapproved scheme as an active anchor.
add_executable(salts_markdown_uri_policy_test markdown_uri_policy_test.c)
target_compile_features(salts_markdown_uri_policy_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_uri_policy_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_uri_policy_test COMMAND salts_markdown_uri_policy_test)

# #183: bounded root ASCII reference definitions; forward/first-win
# source ownership and typed full/collapsed/shortcut LINK admission.
add_executable(salts_markdown_reference_test markdown_reference_test.c)
target_compile_features(salts_markdown_reference_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_reference_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_reference_test COMMAND salts_markdown_reference_test)

# #183: official multiline/escaped root definitions, exact source intervals,
# LINK/TITLE ownership, URL security and bounded failure-atomic rollback.
add_executable(salts_markdown_reference_multiline_test markdown_reference_multiline_test.c)
target_compile_features(salts_markdown_reference_multiline_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_reference_multiline_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_reference_multiline_test
         COMMAND salts_markdown_reference_multiline_test)


# #182/#185: strict C11 root paragraph cross-line inline owner and rollback.
add_executable(salts_markdown_paragraph_inline_test markdown_paragraph_inline_test.c)
target_compile_features(salts_markdown_paragraph_inline_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_paragraph_inline_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_paragraph_inline_test
         COMMAND salts_markdown_paragraph_inline_test)

# #185: strict typed hard-vs-soft break origin and fail-closed HTML semantics.
add_executable(salts_markdown_break_semantics_test markdown_break_semantics_test.c)
target_compile_features(salts_markdown_break_semantics_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_break_semantics_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_break_semantics_test
         COMMAND salts_markdown_break_semantics_test)

# #224: owned IMAGE references, typed title/alt, safe src and rollback.
add_executable(salts_markdown_image_test markdown_image_test.c)
target_compile_features(salts_markdown_image_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_image_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_image_test COMMAND salts_markdown_image_test)

# #225: existing quote/list block ownership admits references into the
# same bounded document table, preserving source order and failure atomicity.
add_executable(salts_markdown_container_reference_test
               markdown_container_reference_test.c)
target_compile_features(salts_markdown_container_reference_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_container_reference_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_container_reference_test
         COMMAND salts_markdown_container_reference_test)

# #184: typed owned inline RAW_HTML and explicit spec/safe policy.
add_executable(salts_markdown_raw_html_policy_test
               markdown_raw_html_policy_test.c)
target_compile_features(salts_markdown_raw_html_policy_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_raw_html_policy_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_raw_html_policy_test
         COMMAND salts_markdown_raw_html_policy_test)

add_executable(salts_markdown_golden_test markdown_golden_test.c)
target_link_libraries(salts_markdown_golden_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
target_compile_features(salts_markdown_golden_test PRIVATE c_std_11)
add_test(NAME salts_markdown_golden_test COMMAND salts_markdown_golden_test)

# Test-only semantic introspection. A separate Python process parses the same
# Markdown using Mistune and compares complete normalized RichDoc ASTs.
add_executable(salts_markdown_ast_dump markdown_c11_dump.c)
target_compile_features(salts_markdown_ast_dump PRIVATE c_std_11)
target_link_libraries(salts_markdown_ast_dump PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)

add_executable(salts_markdown_html_test markdown_html_test.c)
target_compile_features(salts_markdown_html_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_html_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_html_test COMMAND salts_markdown_html_test)

# Test-only native HTML emitter for independent cmark-gfm oracle.
add_executable(salts_markdown_html_dump markdown_html_dump.c)
target_compile_features(salts_markdown_html_dump PRIVATE c_std_11)
target_link_libraries(salts_markdown_html_dump PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)

# CommonMark #177: native typed indented-code AST, HTML and ownership checks.
add_executable(salts_markdown_indented_test markdown_indented_test.c)
target_compile_features(salts_markdown_indented_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_indented_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_indented_test COMMAND salts_markdown_indented_test)

# CommonMark #181 typed list/container metadata, exact HTML and ownership.
add_executable(salts_markdown_containers_test markdown_containers_test.c)
target_compile_features(salts_markdown_containers_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_containers_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_containers_test COMMAND salts_markdown_containers_test)

# #181 second slice: root blockquote lazy continuation and exact owned AST/HTML.
add_executable(salts_markdown_quote_test markdown_quote_test.c)
target_compile_features(salts_markdown_quote_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_quote_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_quote_test COMMAND salts_markdown_quote_test)


add_executable(salts_markdown_nested_quote_test markdown_nested_quote_test.c)
target_compile_features(salts_markdown_nested_quote_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_nested_quote_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_nested_quote_test COMMAND salts_markdown_nested_quote_test)

# CommonMark #181: the quote owns nested lists; the root owns following lists.
add_executable(salts_markdown_quoted_list_test markdown_quoted_list_test.c)
target_compile_features(salts_markdown_quoted_list_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_quoted_list_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_quoted_list_test COMMAND salts_markdown_quoted_list_test)


# Joint Salts #1209 Unicode 17 candidate ABI only; no released-SDK fallback.
# This experimental branch is deliberately isolated from the 223/226 gates.
add_executable(salts_markdown_unicode17_candidate_test
               markdown_unicode17_candidate_test.c)
target_compile_features(salts_markdown_unicode17_candidate_test PRIVATE c_std_11)
target_link_libraries(salts_markdown_unicode17_candidate_test PRIVATE Salts::MarkdownParser Salts::MarkdownHtml)
add_test(NAME salts_markdown_unicode17_candidate_test
         COMMAND salts_markdown_unicode17_candidate_test)

# One strict in-tree oracle gate; Python reference packages are TEST-only,
# never a production dependency or an alternative parser.
# Missing Python/cmarkgfm/Mistune fails this selected BUILD_TESTS profile.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME salts_markdown_strict_commonmark_gfm
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run_strict_markdown.py"
    "$<TARGET_FILE:salts_markdown_html_dump>"
    "$<TARGET_FILE:salts_markdown_ast_dump>"
    "${CMAKE_CURRENT_BINARY_DIR}/oracle")
set_tests_properties(salts_markdown_strict_commonmark_gfm
  PROPERTIES TIMEOUT 360)
