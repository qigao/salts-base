
void test_subcommand_parsing(void) {
    // Global options
    CmdArgerBool verbose = cmd_arger_false;
    CmdArgerDesc global_opts[] = {
        cmd_arger_desc_flag(&verbose, "verbose", "Enable verbose output"),
    };

    // Subcommand: commit
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

    // Case 1: Select subcommand
    char* argv1[] = {"app", "--verbose", "commit", "--message", "hello"};
    int argc1 = 5;
    int selected = -1;

    cmd_arger_parse_subcommand(global_opts, 1, subcommands, 1, &selected, argc1, argv1, "app v1", cmd_arger_false);

    TEST_ASSERT_TRUE(verbose);
    TEST_ASSERT_EQUAL_INT(0, selected);
    TEST_ASSERT_EQUAL_STRING("hello", msg);
}
