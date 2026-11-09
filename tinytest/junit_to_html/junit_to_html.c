#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmd_arger.h>
#include <file/fs.h>
#include <salts/tstr.h>
#include <xml_parser/xml_parser.h>

enum {
    DASHBOARD_COUNT_BUFFER_SIZE = 32,
    DASHBOARD_MAX_OUTPUT_BYTES = 64 * 1024 * 1024,
    DASHBOARD_MAX_TEMPLATE_DEPTH = 32
};

typedef struct dashboard_stats {
    char total[DASHBOARD_COUNT_BUFFER_SIZE];
    char passed[DASHBOARD_COUNT_BUFFER_SIZE];
    char failed[DASHBOARD_COUNT_BUFFER_SIZE];
    char skipped[DASHBOARD_COUNT_BUFFER_SIZE];
    char pass_percentage[DASHBOARD_COUNT_BUFFER_SIZE];
} dashboard_stats;

typedef struct render_context {
    xml_node node;
    xml_node suite;
    const dashboard_stats *stats;
} render_context;

typedef struct template_tag {
    const char *data;
    size_t size;
    size_t begin;
    size_t next;
    bool unescaped;
} template_tag;

static xml_string_view empty_view(void) {
    const xml_string_view view = {NULL, 0u};
    return view;
}

static xml_string_view literal_view(const char *value) {
    const xml_string_view view = {value, value ? strlen(value) : 0u};
    return view;
}

static bool view_equal_bytes(xml_string_view left,
                             const char *right,
                             size_t right_size) {
    return left.size == right_size &&
           (right_size == 0u || memcmp(left.data, right, right_size) == 0);
}

static bool node_is_named(xml_node node, const char *name, size_t name_size) {
    return node.impl != NULL && xml_node_type(node) == SALTS_XML_ELEMENT &&
           view_equal_bytes(xml_node_qualified_name(node), name, name_size);
}

static xml_string_view node_attribute(xml_node node,
                                            const char *name,
                                            size_t name_size) {
    const size_t attribute_count = xml_node_attribute_count(node);
    size_t index;

    for (index = 0u; index < attribute_count; ++index) {
        const xml_attribute attribute = xml_node_attribute_at(node, index);
        if (view_equal_bytes(xml_attribute_qualified_name(attribute), name, name_size)) {
            return xml_attribute_value(attribute);
        }
    }
    return empty_view();
}

/* XmlParser exposes indexed immutable children; the configured node limit bounds
 * this sibling scan even though repeated child_at calls are quadratic. */
static size_t named_child_count(xml_node parent,
                                const char *name,
                                size_t name_size) {
    const size_t child_count = xml_node_child_count(parent);
    size_t count = 0u;
    size_t index;

    for (index = 0u; index < child_count; ++index) {
        if (node_is_named(xml_node_child_at(parent, index), name, name_size)) {
            ++count;
        }
    }
    return count;
}

static xml_node first_named_child(xml_node parent,
                                        const char *name,
                                        size_t name_size) {
    const size_t child_count = xml_node_child_count(parent);
    size_t index;

    for (index = 0u; index < child_count; ++index) {
        const xml_node child = xml_node_child_at(parent, index);
        if (node_is_named(child, name, name_size)) return child;
    }

    {
        const xml_node missing = {NULL};
        return missing;
    }
}

static xml_node find_test_suite(xml_node root) {
    const char suite_name[] = "testsuite";

    if (node_is_named(root, suite_name, sizeof(suite_name) - 1u)) return root;
    return first_named_child(root, suite_name, sizeof(suite_name) - 1u);
}

static size_t parse_count(xml_string_view value) {
    size_t result = 0u;
    size_t index;

    if (value.size == 0u) return 0u;
    for (index = 0u; index < value.size; ++index) {
        const unsigned digit = (unsigned)((unsigned char)value.data[index] - (unsigned char)'0');
        if (digit > 9u || result > (SIZE_MAX - digit) / 10u) return 0u;
        result = result * 10u + digit;
    }
    return result;
}

static int format_stats(xml_node suite, dashboard_stats *stats) {
    const size_t total = parse_count(node_attribute(suite, "tests", 5u));
    const size_t failed = parse_count(node_attribute(suite, "failures", 8u));
    const size_t skipped = parse_count(node_attribute(suite, "skipped", 7u));
    const size_t unavailable = failed <= SIZE_MAX - skipped ? failed + skipped : SIZE_MAX;
    const size_t passed = unavailable <= total ? total - unavailable : 0u;
    const double percentage = total > 0u ? (double)passed * 100.0 / (double)total : 0.0;

    if (snprintf(stats->total, sizeof(stats->total), "%zu", total) < 0 ||
        snprintf(stats->passed, sizeof(stats->passed), "%zu", passed) < 0 ||
        snprintf(stats->failed, sizeof(stats->failed), "%zu", failed) < 0 ||
        snprintf(stats->skipped, sizeof(stats->skipped), "%zu", skipped) < 0 ||
        snprintf(stats->pass_percentage, sizeof(stats->pass_percentage), "%.1f", percentage) < 0) {
        return -1;
    }
    return 0;
}

static xml_string_view testcase_status(xml_node node) {
    if (named_child_count(node, "failure", 7u) > 0u) return literal_view("failed");
    if (named_child_count(node, "skipped", 7u) > 0u) return literal_view("skipped");
    return literal_view("passed");
}

static xml_string_view computed_value(const render_context *context,
                                            const char *name,
                                            size_t name_size) {
    if (context->node.impl == context->suite.impl) {
        if (name_size == 11u && memcmp(name, "total_tests", name_size) == 0)
            return literal_view(context->stats->total);
        if (name_size == 12u && memcmp(name, "passed_tests", name_size) == 0)
            return literal_view(context->stats->passed);
        if (name_size == 12u && memcmp(name, "failed_tests", name_size) == 0)
            return literal_view(context->stats->failed);
        if (name_size == 13u && memcmp(name, "skipped_tests", name_size) == 0)
            return literal_view(context->stats->skipped);
        if (name_size == 15u && memcmp(name, "pass_percentage", name_size) == 0)
            return literal_view(context->stats->pass_percentage);
        if (name_size == 15u && memcmp(name, "test_suite_name", name_size) == 0) {
            const xml_string_view suite_name =
                node_attribute(context->suite, "name", 4u);
            return suite_name.size > 0u ? suite_name : literal_view("Test Results");
        }
        if (name_size == 4u && memcmp(name, "name", name_size) == 0) {
            const xml_string_view suite_name =
                node_attribute(context->suite, "name", 4u);
            return suite_name.size > 0u ? suite_name : literal_view("Test Results");
        }
        if (name_size == 9u && memcmp(name, "timestamp", name_size) == 0) {
            const xml_string_view timestamp =
                node_attribute(context->suite, "timestamp", 9u);
            return timestamp.size > 0u ? timestamp : literal_view("Unknown");
        }
    }

    if (name_size == 6u && memcmp(name, "status", name_size) == 0 &&
        node_is_named(context->node, "testcase", 8u)) {
        return testcase_status(context->node);
    }

    return node_attribute(context->node, name, name_size);
}

static int append_bytes(tstr *output, const char *data, size_t size) {
    tstr next;

    if (size == 0u) return 0;
    if (!output || !*output || !data || size > DASHBOARD_MAX_OUTPUT_BYTES ||
        tstr_len(*output) > DASHBOARD_MAX_OUTPUT_BYTES - size) {
        return -1;
    }
    next = tstr_cat_len(*output, data, size);
    if (!next) return -1;
    *output = next;
    return 0;
}

static int append_html_escaped(tstr *output, xml_string_view value) {
    size_t plain_begin = 0u;
    size_t index;

    for (index = 0u; index < value.size; ++index) {
        const char *replacement = NULL;
        size_t replacement_size = 0u;

        switch (value.data[index]) {
            case '&': replacement = "&amp;"; replacement_size = 5u; break;
            case '<': replacement = "&lt;"; replacement_size = 4u; break;
            case '>': replacement = "&gt;"; replacement_size = 4u; break;
            case '"': replacement = "&quot;"; replacement_size = 6u; break;
            case '\'': replacement = "&#39;"; replacement_size = 5u; break;
            default: break;
        }

        if (!replacement) continue;
        if (append_bytes(output, value.data + plain_begin, index - plain_begin) != 0 ||
            append_bytes(output, replacement, replacement_size) != 0) {
            return -1;
        }
        plain_begin = index + 1u;
    }
    return append_bytes(output, value.data + plain_begin, value.size - plain_begin);
}

static int append_node_text(tstr *output, xml_node node, bool escaped) {
    const size_t child_count = xml_node_child_count(node);
    size_t index;

    for (index = 0u; index < child_count; ++index) {
        if (xml_node_type(xml_node_child_at(node, index)) == SALTS_XML_ELEMENT) {
            return append_bytes(output, "[object]", 8u);
        }
    }

    for (index = 0u; index < child_count; ++index) {
        const xml_node child = xml_node_child_at(node, index);
        if (xml_node_type(child) == SALTS_XML_TEXT) {
            const xml_string_view value = xml_node_value(child);
            const int result = escaped ? append_html_escaped(output, value)
                                       : append_bytes(output, value.data, value.size);
            if (result != 0) return result;
        }
    }
    return 0;
}

static int append_value(tstr *output,
                        const render_context *context,
                        const char *name,
                        size_t name_size,
                        bool escaped) {
    xml_string_view value;

    if (name_size == 1u && name[0] == '.') {
        return append_node_text(output, context->node, escaped);
    }

    value = computed_value(context, name, name_size);
    if (value.data || value.size > 0u) {
        return escaped ? append_html_escaped(output, value)
                       : append_bytes(output, value.data, value.size);
    }

    if (named_child_count(context->node, name, name_size) == 1u) {
        return append_node_text(output, first_named_child(context->node, name, name_size), escaped);
    }
    return 0;
}

static void trim_tag(template_tag *tag) {
    while (tag->size > 0u &&
           (tag->data[0] == ' ' || tag->data[0] == '\t' ||
            tag->data[0] == '\r' || tag->data[0] == '\n')) {
        ++tag->data;
        --tag->size;
    }
    while (tag->size > 0u) {
        const char last = tag->data[tag->size - 1u];
        if (last != ' ' && last != '\t' && last != '\r' && last != '\n') break;
        --tag->size;
    }
}

static bool next_template_tag(const char *input,
                              size_t input_size,
                              size_t search_from,
                              template_tag *tag) {
    size_t begin;

    for (begin = search_from; begin + 1u < input_size; ++begin) {
        size_t content_begin;
        size_t close;
        bool triple;

        if (input[begin] != '{' || input[begin + 1u] != '{') continue;
        triple = begin + 2u < input_size && input[begin + 2u] == '{';
        content_begin = begin + (triple ? 3u : 2u);
        for (close = content_begin; close + (triple ? 2u : 1u) < input_size; ++close) {
            if (input[close] != '}' || input[close + 1u] != '}') continue;
            if (triple && input[close + 2u] != '}') continue;
            tag->data = input + content_begin;
            tag->size = close - content_begin;
            tag->begin = begin;
            tag->next = close + (triple ? 3u : 2u);
            tag->unescaped = triple;
            trim_tag(tag);
            return true;
        }
        return false;
    }
    return false;
}

static bool tag_name_equal(const template_tag *tag,
                           char marker,
                           const char *name,
                           size_t name_size) {
    return tag->size == name_size + 1u && tag->data[0] == marker &&
           memcmp(tag->data + 1u, name, name_size) == 0;
}

static int find_section_end(const char *input,
                            size_t input_size,
                            size_t body_begin,
                            const char *name,
                            size_t name_size,
                            size_t *body_end,
                            size_t *section_end) {
    size_t cursor = body_begin;
    size_t nested = 0u;
    template_tag tag;

    while (next_template_tag(input, input_size, cursor, &tag)) {
        if (tag_name_equal(&tag, '#', name, name_size) ||
            tag_name_equal(&tag, '^', name, name_size)) {
            ++nested;
        } else if (tag_name_equal(&tag, '/', name, name_size)) {
            if (nested == 0u) {
                *body_end = tag.begin;
                *section_end = tag.next;
                return 0;
            }
            --nested;
        }
        cursor = tag.next;
    }
    return -1;
}

static int render_template_range(const char *input,
                                 size_t input_size,
                                 const render_context *context,
                                 tstr *output,
                                 unsigned depth);

static int render_section(const char *input,
                          size_t input_size,
                          const render_context *context,
                          const char *name,
                          size_t name_size,
                          bool inverted,
                          tstr *output,
                          unsigned depth) {
    const size_t child_count = xml_node_child_count(context->node);
    size_t matches = 0u;
    size_t index;

    if (name_size == 1u && name[0] == '.') {
        tstr probe = tstr_new();
        bool truthy;
        int result;
        if (!probe) return -1;
        result = append_node_text(&probe, context->node, false);
        truthy = result == 0 && tstr_len(probe) > 0u;
        tstr_free(probe);
        if (result != 0) return result;
        return truthy != inverted
                   ? render_template_range(input, input_size, context, output, depth)
                   : 0;
    }

    for (index = 0u; index < child_count; ++index) {
        if (node_is_named(xml_node_child_at(context->node, index), name, name_size)) {
            ++matches;
        }
    }
    if (matches == 0u) {
        const xml_string_view attribute = computed_value(context, name, name_size);
        const bool truthy = attribute.size > 0u;
        return truthy != inverted
                   ? render_template_range(input, input_size, context, output, depth)
                   : 0;
    }
    if (inverted) return 0;

    for (index = 0u; index < child_count; ++index) {
        const xml_node child = xml_node_child_at(context->node, index);
        render_context child_context;
        if (!node_is_named(child, name, name_size)) continue;
        child_context = *context;
        child_context.node = child;
        if (render_template_range(input, input_size, &child_context, output, depth) != 0)
            return -1;
    }
    return 0;
}

static int render_template_range(const char *input,
                                 size_t input_size,
                                 const render_context *context,
                                 tstr *output,
                                 unsigned depth) {
    size_t cursor = 0u;
    template_tag tag;

    if (depth > DASHBOARD_MAX_TEMPLATE_DEPTH) return -1;
    while (next_template_tag(input, input_size, cursor, &tag)) {
        if (append_bytes(output, input + cursor, tag.begin - cursor) != 0) return -1;
        if (tag.size == 0u) {
            cursor = tag.next;
            continue;
        }

        if (tag.data[0] == '#' || tag.data[0] == '^') {
            size_t body_end;
            size_t section_end;
            const bool inverted = tag.data[0] == '^';
            const char *name = tag.data + 1u;
            const size_t name_size = tag.size - 1u;
            if (find_section_end(input, input_size, tag.next, name, name_size,
                                 &body_end, &section_end) != 0 ||
                render_section(input + tag.next, body_end - tag.next, context,
                               name, name_size, inverted, output, depth + 1u) != 0) {
                return -1;
            }
            cursor = section_end;
            continue;
        }
        if (tag.data[0] == '/') return -1;
        if (tag.data[0] == '!') {
            cursor = tag.next;
            continue;
        }
        if (tag.data[0] == '&') {
            ++tag.data;
            --tag.size;
            tag.unescaped = true;
            trim_tag(&tag);
        }
        if (append_value(output, context, tag.data, tag.size, !tag.unescaped) != 0)
            return -1;
        cursor = tag.next;
    }
    return append_bytes(output, input + cursor, input_size - cursor);
}

static int load_template(const char *template_path, fs_buf_t *template_buffer) {
    if (fs_read_file(template_path, template_buffer) == 0) return 0;
    if (strcmp(template_path, "dashboard.html") != 0) {
        fprintf(stderr,
                "Warning: Could not open template file at: %s. Trying dashboard.html...\n",
                template_path);
    }
    return fs_read_file("dashboard.html", template_buffer);
}

int main(int argc, char **argv) {
    const char *xml_file = NULL;
    const char *output_file = "test_dashboard.html";
    const char *template_path = "dashboard.html";
    CmdArgerBool no_color = cmd_arger_false;
    CmdArgerBool use_colors = cmd_arger_true;
    CmdArgerDesc optional_args[] = {
        cmd_arger_required(
            cmd_arger_desc_string_sh((char **)&xml_file, "input", "i", "JUnit XML input file")),
        cmd_arger_desc_string_sh((char **)&output_file, "output", "o", "Output HTML file"),
        cmd_arger_desc_string_sh((char **)&template_path, "template", "t", "Dashboard template file"),
        cmd_arger_desc_flag(&no_color, "no-color", "Disable terminal colors"),
    };
    fs_buf_t xml_buffer = {0};
    fs_buf_t template_buffer = {0};
    xml_document document = {0};
    xml_diagnostic diagnostic = {0};
    xml_node suite;
    dashboard_stats stats;
    render_context context;
    tstr output = NULL;
    int result = 1;
    int argument_index;

    for (argument_index = 1; argument_index < argc; ++argument_index) {
        if (strcmp(argv[argument_index], "--no-color") == 0) use_colors = cmd_arger_false;
    }
    cmd_arger_parse(optional_args, sizeof(optional_args) / sizeof(*optional_args), NULL, 0,
                    argc, argv, "JUnit to HTML Dashboard Generator v1.3", use_colors);

    if (!xml_file) {
        fprintf(stderr, "Error: XML input file is required.\n");
        goto cleanup;
    }
    if (fs_read_file(xml_file, &xml_buffer) != 0) {
        fprintf(stderr, "Error: Could not read XML: %s\n", xml_file);
        goto cleanup;
    }
    if (xml_parse(&document, xml_buffer.base, xml_buffer.len, NULL, &diagnostic) !=
        SALTS_XML_OK) {
        fprintf(stderr, "Error: Could not parse XML at %u:%u: %s\n",
                diagnostic.location.line, diagnostic.location.column, diagnostic.message);
        goto cleanup;
    }

    suite = find_test_suite(xml_document_root(&document));
    if (!suite.impl) {
        fprintf(stderr, "Error: No testsuite found in XML\n");
        goto cleanup;
    }
    if (format_stats(suite, &stats) != 0) {
        fprintf(stderr, "Error: Could not format dashboard statistics\n");
        goto cleanup;
    }
    if (load_template(template_path, &template_buffer) != 0) {
        fprintf(stderr, "Error: Could not find dashboard template\n");
        goto cleanup;
    }

    output = tstr_new();
    if (!output) {
        fprintf(stderr, "Error: Could not allocate dashboard output\n");
        goto cleanup;
    }
    context.node = suite;
    context.suite = suite;
    context.stats = &stats;
    if (render_template_range(template_buffer.base, template_buffer.len, &context, &output, 0u) !=
        0) {
        fprintf(stderr, "Error: Failed to render dashboard template\n");
        goto cleanup;
    }

    {
        const fs_buf_t output_buffer = fs_buf_init(output, tstr_len(output));
        if (fs_write_file(output_file, &output_buffer) != 0) {
            fprintf(stderr, "Error: Could not write output file: %s\n", output_file);
            goto cleanup;
        }
    }

    printf("Processing %zu test cases...\n", named_child_count(suite, "testcase", 8u));
    printf("Dashboard generated: %s\n", output_file);
    result = 0;

cleanup:
    tstr_free(output);
    xml_document_destroy(&document);
    fs_buf_free(&template_buffer);
    fs_buf_free(&xml_buffer);
    return result;
}
