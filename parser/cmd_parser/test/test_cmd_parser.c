#include "tinytest.h"
#include <cmd_arger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static int test_process_environment_set(const char* name, const char* value) {
#if defined(_WIN32)
    return SetEnvironmentVariableA(name, value) ? 0 : -1;
#else
    return value ? setenv(name, value, 1) : unsetenv(name);
#endif
}

static CmdArgerBool validate_port(const char* value, const char** error_message) {
    char* end;
    long v = strtol(value, &end, 10);
    if (*end != '\0') {
        *error_message = "must be an integer";
        return cmd_arger_false;
    }
    if (v < 1024 || v > 65535) {
        *error_message = "must be between 1024 and 65535";
        return cmd_arger_false;
    }
    return cmd_arger_true;
}

spec("cmd_parser") {
    describe("Flag Parsing") {
        it("should parse long flags correctly") {
            CmdArgerBool verbose = cmd_arger_false;
            CmdArgerBool help = cmd_arger_false;

            CmdArgerDesc optional_args[] = {
                cmd_arger_desc_flag(&verbose, "verbose", "Enable verbose output"),
                cmd_arger_desc_flag(&help, "help", "Show help"),
            };

            char* argv[] = {"test_app", "--verbose"};
            int argc = 2;

            cmd_arger_parse(optional_args, 2, NULL, 0, argc, argv, "test_app v1.0", cmd_arger_false);

            check(verbose == cmd_arger_true);
            check(help == cmd_arger_false);
        }

        it("should parse short flags correctly") {
            CmdArgerBool verbose = cmd_arger_false;
            char* output = NULL;

            CmdArgerDesc optional_args[] = {
                cmd_arger_desc_flag_sh(&verbose, "verbose", "v", "Enable verbose"),
                cmd_arger_desc_string_sh(&output, "output", "o", "Output file"),
            };

            char* argv[] = {"test_app", "-v", "-o", "result.txt"};
            int argc = 4;

            cmd_arger_parse(optional_args, 2, NULL, 0, argc, argv, "test_app v1.0", cmd_arger_false);

            check(verbose == cmd_arger_true);
            check_equal(output, "result.txt");
        }
    }

    describe("Argument Parsing") {
        it("should parse string arguments correctly") {
            char* name = NULL;
            char* mode = "default";

            CmdArgerDesc optional_args[] = {
                cmd_arger_desc_string(&mode, "mode", "Set mode"),
            };
            CmdArgerDesc required_args[] = {
                cmd_arger_desc_string(&name, "name", "Name argument"),
            };

            char* argv[] = {"test_app", "--mode", "fast", "myname"};
            int argc = 4;

            cmd_arger_parse(optional_args, 1, required_args, 1, argc, argv, "test_app v1.0", cmd_arger_false);

            check_equal(mode, "fast");
            check_equal(name, "myname");
        }

        it("should parse variadic positional arguments") {
            char* files[10];
            uint32_t files_count = 0;

            CmdArgerDesc required_args[] = {
                cmd_arger_desc_string_list(files, &files_count, 10, "files", "List of files"),
            };

            char* argv[] = {"app", "file1.txt", "file2.txt", "file3.txt"};
            int argc = 4;

            cmd_arger_parse(NULL, 0, required_args, 1, argc, argv, "app", cmd_arger_false);

            check_equal(files_count, 3);
            check_equal(files[0], "file1.txt");
            check_equal(files[1], "file2.txt");
            check_equal(files[2], "file3.txt");
        }
    }

    describe("Subcommands") {
        it("should parse subcommands correctly") {
            CmdArgerBool verbose = cmd_arger_false;
            CmdArgerDesc global_opts[] = {
                cmd_arger_desc_flag(&verbose, "verbose", "Enable verbose output"),
            };

            char* msg = NULL;
            CmdArgerDesc commit_opts[] = {
                cmd_arger_desc_string(&msg, "message", "Commit message"),
            };
            CmdArgerSubCommand subcommands[] = {
                {
                    .name = "commit",
                    .info = "Commit changes",
                    .optional_args = commit_opts,
                    .optional_args_count = 1,
                    .required_args = NULL,
                    .required_args_count = 0
                }
            };

            char* argv[] = {"app", "--verbose", "commit", "--message", "hello"};
            int argc = 5;
            int selected = -1;

            cmd_arger_parse_subcommand(global_opts, 1, subcommands, 1, &selected, argc, argv, "app v1", cmd_arger_false);

            check(verbose == cmd_arger_true);
            check_equal(selected, 0);
            check_equal(msg, "hello");
        }
    }

    describe("Environment Variables") {
        it("should read the process environment across runtime boundaries") {
            static const char name[] = "TEST_CMD_PROCESS_ENV";
            char* value = "default";
            CmdArgerDesc opts[] = {
                cmd_arger_with_env(
                    cmd_arger_desc_string(&value, "value", "Process value"),
                    name),
            };
            char* argv[] = {"app"};

            check_equal(test_process_environment_set(name, "process-value"), 0);
            cmd_arger_parse(opts, 1, NULL, 0, 1, argv, "app", cmd_arger_false);
            check_equal(value, "process-value");
            check_equal(test_process_environment_set(name, NULL), 0);
        }

        it("should load values from environment variables") {
            CmdArgerBool debug = cmd_arger_false;
            char* api_key = "default";
            int64_t retries = 0;

            CmdArgerDesc opts[] = {
                cmd_arger_with_env(cmd_arger_desc_flag(&debug, "debug", "Debug mode"), "TEST_APP_DEBUG"),
                cmd_arger_with_env(cmd_arger_desc_string(&api_key, "key", "API Key"), "TEST_APP_KEY"),
                cmd_arger_with_env(cmd_arger_desc_integer(&retries, "retries", "Retry count"), "TEST_APP_RETRIES"),
            };

            #ifdef _WIN32
            _putenv("TEST_APP_DEBUG=true");
            _putenv("TEST_APP_KEY=secret");
            _putenv("TEST_APP_RETRIES=5");
            #else
            setenv("TEST_APP_DEBUG", "true", 1);
            setenv("TEST_APP_KEY", "secret", 1);
            setenv("TEST_APP_RETRIES", "5", 1);
            #endif

            char* argv[] = {"app"};
            cmd_arger_parse(opts, 3, NULL, 0, 1, argv, "app", cmd_arger_false);

            check(debug == cmd_arger_true);
            check_equal(api_key, "secret");
            check_equal(retries, 5);

            // Test override by CLI
            #ifdef _WIN32
            _putenv("TEST_APP_DEBUG=false");
            #else
            setenv("TEST_APP_DEBUG", "false", 1);
            #endif

            char* argv2[] = {"app", "--debug"};
            cmd_arger_parse(opts, 3, NULL, 0, 2, argv2, "app", cmd_arger_false);
            check(debug == cmd_arger_true); // CLI overrides Env
        }

        it("should integrate with .env files") {
            char* api_url = "default";
            CmdArgerDesc opts[] = {
                cmd_arger_with_env(cmd_arger_desc_string(&api_url, "url", "API URL"), "TEST_API_URL"),
            };

            FILE* f = fopen(".env", "wb");
            if (f) {
                fprintf(f, "TEST_API_URL=https://api.example.com\n");
                fclose(f);
            }

            char* argv[] = {"app"};
            cmd_arger_parse(opts, 1, NULL, 0, 1, argv, "app", cmd_arger_false);

            check_equal(api_url, "https://api.example.com");

            remove(".env");
        }
    }

    describe("Advanced Features") {
        it("should handle response files (@args.txt)") {
            char* mode = "default";
            CmdArgerDesc opts[] = {
                cmd_arger_desc_string(&mode, "mode", "Mode"),
            };

            const char* filename = "args.txt";
            FILE* f = fopen(filename, "wb");
            if (f) {
                fprintf(f, "--mode fast");
                fclose(f);
            }

            char* argv[] = {"app", "@args.txt"};
            cmd_arger_parse(opts, 1, NULL, 0, 2, argv, "app", cmd_arger_false);

            check_equal(mode, "fast");

            remove(filename);
        }

        it("should support custom validators") {
            int64_t port = 0;
            CmdArgerDesc opts[] = {
                cmd_arger_with_validator(cmd_arger_desc_integer(&port, "port", "Port number"), validate_port),
            };

            char* argv[] = {"app", "--port", "8080"};
            cmd_arger_parse(opts, 1, NULL, 0, 3, argv, "app", cmd_arger_false);
            check_equal(port, 8080);
        }

        it("should support option grouping") {
            char* input = NULL;
            char* output = NULL;

            CmdArgerDesc opts[] = {
                cmd_arger_with_group(cmd_arger_desc_string(&input, "input", "Input file"), "IO Options"),
                cmd_arger_with_group(cmd_arger_desc_string(&output, "output", "Output file"), "IO Options"),
                cmd_arger_desc_flag(NULL, "verbose", "Verbose mode"),
            };

            char* argv[] = {"app", "--input", "in.txt"};
            cmd_arger_parse(opts, 3, NULL, 0, 3, argv, "app", cmd_arger_false);
            check_equal(input, "in.txt");
        }

        it("should support string choices") {
            char* method = "GET";
            const char* choices[] = {"GET", "POST", "DELETE"};

            CmdArgerDesc opts[] = {
                cmd_arger_with_choices(cmd_arger_desc_string(&method, "method", "HTTP Method"), choices, 3),
            };

            char* argv[] = {"app", "--method", "POST"};
            cmd_arger_parse(opts, 1, NULL, 0, 3, argv, "app", cmd_arger_false);
            check_equal(method, "POST");
        }
    }
}
