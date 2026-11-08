#ifndef CMD_ARGER_H
#define CMD_ARGER_H

/**
 * @file cmd_arger.h
 * @brief A minimal command line argument parsing library.
 *
 * Features:
 * - Simple easy to use API.
 * - No dependencies other than libc.
 * - Linux & Windows support.
 * - C99 compatible.
 * - Parse boolean, integers, floats and strings with error checking.
 * - Auto generates a help message when parsing fails and can be invoked manually with --help
 * - Unix style optional arguments
 * - Optional arguments are allowed to be before, after and in-between required arguments.
 * - Terminal colors
 * - Subcommands support
 * - Response file expansion (@file)
 * - Environment variable fallback
 * - Argument grouping
 * - Choices validation
 * - Custom validators
 * - Required optional arguments
 *
 * Non-features:
 * - Short hand optional arguments (with a single hyphen: tar -xvf): as you cannot clearly read what they mean.
 *   Note: Bundled flags like -abc are supported if they are defined as individual short options.
 *
 * USAGE:
 * Everywhere you need to use this library, put this at the top of the file:
 * @code{.c}
 * #include "cmd_arger.h"
 * @endcode
 *
 * EXAMPLE PROGRAM:
 * @code{.c}
 * int main(int argc, char** argv) {
 *     CmdArgerBool flag = cmd_arger_false;
 *     char* string = "default value";
 *     int64_t integer = 1024;
 *     CmdArgerDesc optional_arg_descs[] = {
 *         cmd_arger_desc_flag(&flag, "flag", "a boolean value"),
 *         cmd_arger_desc_string(&string, "string", "a string value"),
 *         cmd_arger_desc_integer(&integer, "integer", "a 64 bit signed integer value"),
 *     };
 *
 *     char* required_string = NULL;
 *     int64_t required_integer = 0;
 *     CmdArgerDesc required_arg_descs[] = {
 *         cmd_arger_desc_string(&required_string, "string", "a string value"),
 *         cmd_arger_desc_integer(&required_integer, "integer", "a 64 bit signed integer value"),
 *     };
 *
 *     static char* app_name_and_version = "example cmd arger";
 *     static CmdArgerBool colors = cmd_arger_true;
 *     cmd_arger_parse(
 *         optional_arg_descs, sizeof(optional_arg_descs) / sizeof(*optional_arg_descs),
 *         required_arg_descs, sizeof(required_arg_descs) / sizeof(*required_arg_descs),
 *         argc, argv, app_name_and_version, colors);
 * }
 * @endcode
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

/**
 * @brief Boolean type for cmd_arger.
 */
typedef uint8_t CmdArgerBool;
#define cmd_arger_true 1
#define cmd_arger_false 0

/**
 * @brief Supported types of command line arguments.
 */
typedef enum {
	CmdArgerDescKind_flag,        /**< A boolean flag (e.g., --verbose) */
	CmdArgerDescKind_string,      /**< A string value (e.g., --name "John") */
	CmdArgerDescKind_integer,     /**< A 64-bit signed integer (e.g., --port 8080) */
	CmdArgerDescKind_float,       /**< A double precision floating point (e.g., --ratio 0.5) */
	CmdArgerDescKind_enum,        /**< A string that maps to an integer value */
	CmdArgerDescKind_string_list, /**< A list of strings (can be specified multiple times) */
} CmdArgerDescKind;

/**
 * @brief Description of an enum choice.
 */
typedef struct {
	const char* name;    /**< The string that represents this enum value on the command line */
	const char* info;    /**< Description of this choice (used in help) */
	int64_t value; /**< The integer value this choice maps to */
} CmdArgerEnumDesc;

/**
 * @brief Signature for custom argument validation functions.
 * @param value The raw string value provided on the command line.
 * @param error_message Output pointer to a static string explaining why validation failed.
 * @return cmd_arger_true if valid, cmd_arger_false otherwise.
 */
typedef CmdArgerBool (*CmdArgerValidator)(const char* value, const char** error_message);

/**
 * @brief Detailed description of a single command line argument.
 *
 * This struct is typically initialized using the `cmd_arger_desc_*` factory functions
 * and optionally modified with `cmd_arger_with_*` functions.
 */
typedef struct {
	const char* name;       /**< Long name of the argument (e.g., "verbose") */
	const char* short_name; /**< Short name (one character, e.g., "v") */
	const char* info;       /**< Description shown in help message */
	const char* group;      /**< Help group name for categorization */
	const char* env_var;    /**< Environment variable fallback */

	CmdArgerDescKind kind;  /**< Type of the argument */
	void* value_out;        /**< Pointer where the parsed value will be stored */

	union {
		struct {
			CmdArgerEnumDesc* descs;
			uint32_t count;
		} enums;            /**< Configuration for CmdArgerDescKind_enum */
		struct {
			uint32_t* count_out;
			uint32_t max_count;
		} list;             /**< Configuration for CmdArgerDescKind_string_list */
		struct {
			const char** items;
			uint32_t count;
		} choices;          /**< Configuration for restricted choices (strings) */
	} spec;                 /**< Type-specific configuration */

	CmdArgerValidator validator; /**< Custom validation function */
	CmdArgerBool is_required;    /**< Whether the option is mandatory */
} CmdArgerDesc;

/**
 * @brief Description of a subcommand (e.g., `git commit`).
 */
typedef struct {
	const char* name;                 /**< Name of the subcommand */
	const char* info;                 /**< Description shown in help message */
	CmdArgerDesc* optional_args;    /**< Options specific to this subcommand */
	uint32_t optional_args_count;   /**< Number of subcommand options */
	CmdArgerDesc* required_args;    /**< Positional arguments specific to this subcommand */
	uint32_t required_args_count;   /**< Number of subcommand positional arguments */
} CmdArgerSubCommand;

/* @{ */
/**
 * @name Argument Descriptor Factories
 * Functions to create a basic CmdArgerDesc for various types.
 */

/**
 * @brief Create a boolean flag descriptor.
 * @param value_out Pointer to CmdArgerBool. Set to cmd_arger_true if present.
 * @param name Long name of the flag.
 * @param info Description for help text.
 */
extern CmdArgerDesc cmd_arger_desc_flag(CmdArgerBool* value_out, const char* name, const char* info);

/**
 * @brief Create a string argument descriptor.
 * @param value_out Pointer to char*. Set to the provided string value.
 * @param name Long name of the option.
 * @param info Description for help text.
 */
extern CmdArgerDesc cmd_arger_desc_string(char** value_out, const char* name, const char* info);

/**
 * @brief Create an integer argument descriptor.
 * @param value_out Pointer to int64_t.
 * @param name Long name of the option.
 * @param info Description for help text.
 */
extern CmdArgerDesc cmd_arger_desc_integer(int64_t* value_out, const char* name, const char* info);

/**
 * @brief Create a floating point argument descriptor.
 * @param value_out Pointer to double.
 * @param name Long name of the option.
 * @param info Description for help text.
 */
extern CmdArgerDesc cmd_arger_desc_float(double* value_out, const char* name, const char* info);

/**
 * @brief Create an enum argument descriptor.
 * @param value_out Pointer to int64_t.
 * @param name Long name of the option.
 * @param info Description for help text.
 * @param enum_descs Array of possible choices and their values.
 * @param enum_descs_count Size of enum_descs array.
 */
extern CmdArgerDesc cmd_arger_desc_enum(int64_t* value_out, const char* name, const char* info, CmdArgerEnumDesc* enum_descs, uint32_t enum_descs_count);

/**
 * @brief Create a string list argument descriptor.
 * @param values_out Pointer to an array of char* where list items will be stored.
 * @param count_out Pointer to uint32_t that will store the final number of items.
 * @param max_count Maximum number of items values_out can hold.
 * @param name Long name of the option.
 * @param info Description for help text.
 */
extern CmdArgerDesc cmd_arger_desc_string_list(char** values_out, uint32_t* count_out, uint32_t max_count, const char* name, const char* info);

/* Same as above but with short names */
extern CmdArgerDesc cmd_arger_desc_flag_sh(CmdArgerBool* value_out, const char* name, const char* short_name, const char* info);
extern CmdArgerDesc cmd_arger_desc_string_sh(char** value_out, const char* name, const char* short_name, const char* info);
extern CmdArgerDesc cmd_arger_desc_integer_sh(int64_t* value_out, const char* name, const char* short_name, const char* info);
extern CmdArgerDesc cmd_arger_desc_float_sh(double* value_out, const char* name, const char* short_name, const char* info);
extern CmdArgerDesc cmd_arger_desc_enum_sh(int64_t* value_out, const char* name, const char* short_name, const char* info, CmdArgerEnumDesc* enum_descs, uint32_t enum_descs_count);
extern CmdArgerDesc cmd_arger_desc_string_list_sh(char** values_out, uint32_t* count_out, uint32_t max_count, const char* name, const char* short_name, const char* info);
/* @} */

/* @{ */
/**
 * @name Argument Modifiers
 * Functions to add additional behavior to an existing descriptor.
 */

/**
 * @brief Adds an environment variable fallback to a descriptor.
 * @param desc The descriptor to modify.
 * @param env_var Name of the environment variable.
 */
extern CmdArgerDesc cmd_arger_with_env(CmdArgerDesc desc, const char* env_var);

/**
 * @brief Assigns a descriptor to a specific help group.
 * @param desc The descriptor to modify.
 * @param group Name of the group (e.g., "Network Options").
 */
extern CmdArgerDesc cmd_arger_with_group(CmdArgerDesc desc, const char* group);

/**
 * @brief Restricts a string argument to a specific set of choices.
 * @param desc The descriptor to modify.
 * @param choices Array of allowed strings.
 * @param choices_count Size of choices array.
 */
extern CmdArgerDesc cmd_arger_with_choices(CmdArgerDesc desc, const char** choices, uint32_t choices_count);

/**
 * @brief Adds a custom validator function to a descriptor.
 * @param desc The descriptor to modify.
 * @param validator The validation function.
 */
extern CmdArgerDesc cmd_arger_with_validator(CmdArgerDesc desc, CmdArgerValidator validator);

/**
 * @brief Makes an optional argument required.
 * @param desc The descriptor to modify.
 */
extern CmdArgerDesc cmd_arger_required(CmdArgerDesc desc);
/* @} */

/**
 * @brief Main entry point for argument parsing.
 *
 * Parses argc and argv according to the provided descriptors.
 * If parsing fails or --help is encountered, displays help/error and terminates.
 *
 * @param optional_args Array of optional argument descriptors.
 * @param optional_args_count Number of optional arguments.
 * @param required_args Array of positional argument descriptors.
 * @param required_args_count Number of positional arguments.
 * @param argc Count of arguments from main().
 * @param argv Arguments array from main().
 * @param app_name_and_version String shown in help header.
 * @param colors Whether to use ANSI terminal colors in output.
 */
extern void cmd_arger_parse(CmdArgerDesc* optional_args, uint32_t optional_args_count, CmdArgerDesc* required_args, uint32_t required_args_count, int argc, char** argv, const char* app_name_and_version, CmdArgerBool colors);

/**
 * @brief Parse arguments with subcommand support.
 *
 * @param global_optional_args Options that can appear before or after subcommands.
 * @param global_optional_args_count Number of global options.
 * @param subcommands Array of available subcommands.
 * @param subcommands_count Number of subcommands.
 * @param selected_subcommand_idx Output pointer to the index of chosen subcommand.
 * @param argc Arguments count.
 * @param argv Arguments array.
 * @param app_name_and_version Help header string.
 * @param colors Use colors in output.
 */
extern void cmd_arger_parse_subcommand(
    CmdArgerDesc* global_optional_args, uint32_t global_optional_args_count,
    CmdArgerSubCommand* subcommands, uint32_t subcommands_count,
    int* selected_subcommand_idx,
    int argc, char** argv, const char* app_name_and_version, CmdArgerBool colors);

/**
 * @brief Display help and exit program.
 */
extern void cmd_arger_show_help_and_exit(CmdArgerDesc* optional_args, uint32_t optional_args_count, CmdArgerDesc* required_args, uint32_t required_args_count, const char* exe_name, const char* app_name_and_version, CmdArgerBool colors);

/**
 * @brief Display detailed help for subcommands and exit.
 */
extern void cmd_arger_show_subcommand_help_and_exit(
    CmdArgerDesc* global_optional_args, uint32_t global_optional_args_count,
    CmdArgerDesc* active_optional_args, uint32_t active_optional_args_count,
    CmdArgerDesc* active_required_args, uint32_t active_required_args_count,
    CmdArgerSubCommand* all_subcommands, uint32_t all_subcommands_count,
    const char* active_subcommand_name,
    const char* exe_name, const char* app_name_and_version, CmdArgerBool colors);

#endif
