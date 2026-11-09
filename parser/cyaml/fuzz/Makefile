CC = afl-clang-fast
CFLAGS = -O2 -g -I../src
BUILD = build
YPATH_LEXER = $(BUILD)/cyaml_ypath_lexer_gen.c
YPATH_GRAMMAR = $(BUILD)/cyaml_ypath_grammar_gen.c
YPATH_GRAMMAR_H = $(BUILD)/cyaml_ypath_grammar_gen.h

all: $(BUILD)/fuzz_parse $(BUILD)/fuzz_emit $(BUILD)/fuzz_dump $(BUILD)/fuzz_json $(BUILD)/fuzz_json_parse $(BUILD)/fuzz_ypath

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/out:
	mkdir -p $(BUILD)/out

$(YPATH_LEXER): ../src/cyaml_ypath_lexer.re ../src/cyaml_ypath_lexer.h | $(BUILD)
	re2c -o $@ $<

$(YPATH_GRAMMAR) $(YPATH_GRAMMAR_H): ../src/cyaml_ypath_expr_grammar.y ../src/cyaml_ypath_expr_parser.h | $(BUILD)
	cp $< $(BUILD)/cyaml_ypath_grammar_gen.y
	lemon -T../../tools/lemon/lempar.c $(BUILD)/cyaml_ypath_grammar_gen.y

YPATH_GENERATED = $(YPATH_LEXER) $(YPATH_GRAMMAR)

$(BUILD)/fuzz_parse: fuzz_parse.c $(YPATH_GENERATED) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BUILD) -o $@ $^

$(BUILD)/fuzz_emit: fuzz_emit.c $(YPATH_GENERATED) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BUILD) -o $@ $^

$(BUILD)/fuzz_dump: fuzz_dump.c $(YPATH_GENERATED) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BUILD) -o $@ $^

$(BUILD)/fuzz_json: fuzz_json.c $(YPATH_GENERATED) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BUILD) -o $@ $^

$(BUILD)/fuzz_json_parse: fuzz_json_parse.c $(YPATH_GENERATED) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BUILD) -o $@ $^

$(BUILD)/fuzz_ypath: fuzz_ypath.c $(YPATH_GENERATED) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BUILD) -o $@ $^

corpus:
	mkdir -p corpus/yaml corpus/json corpus/ypath

run-parse: $(BUILD)/fuzz_parse corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/parse -- $(BUILD)/fuzz_parse

run-emit: $(BUILD)/fuzz_emit corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/emit -- $(BUILD)/fuzz_emit

run-dump: $(BUILD)/fuzz_dump corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/dump -- $(BUILD)/fuzz_dump

run-json: $(BUILD)/fuzz_json corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/json -- $(BUILD)/fuzz_json

run-json-parse: $(BUILD)/fuzz_json_parse corpus | $(BUILD)/out
	afl-fuzz -i corpus/json -o $(BUILD)/out/json_parse -- $(BUILD)/fuzz_json_parse

run-ypath: $(BUILD)/fuzz_ypath corpus | $(BUILD)/out
	afl-fuzz -i corpus/ypath -o $(BUILD)/out/ypath -- $(BUILD)/fuzz_ypath

clean:
	rm -rf $(BUILD)
