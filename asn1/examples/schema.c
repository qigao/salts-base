#include <mib_compiler.h>

int main(void) {
    return mib_compile_string("Demo DEFINITIONS ::= BEGIN "
        "internet OBJECT IDENTIFIER ::= { iso(1) 3 6 1 } "
        "example OBJECT IDENTIFIER ::= { internet 4 1 32473 } END", ".") == 0 ? 0 : 1;
}
