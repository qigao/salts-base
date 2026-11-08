#include "toonc.h"
#include <stdio.h>

int main() {

    const char *test1 = "numbers[5]: 1,2,3,4,5\n";
    printf("Testing: %s", test1);
    toonObject *root = TOONc_parseString(test1);
    if (root) {
        printf("SUCCESS 1!\n");
        TOONc_printRoot(root);
        TOONc_free(root);
    } else {
        printf("FAILED 1!\n");
    }

    const char *test2 = "empty[0]:\n";
    printf("\nTesting: %s", test2);
    root = TOONc_parseString(test2);
    if (root) {
        printf("SUCCESS 2!\n");
        TOONc_printRoot(root);
        TOONc_free(root);
    } else {
        printf("FAILED 2!\n");
    }

    const char *test3 = "mixed[3]: 1, true, false\n";
    printf("\nTesting: %s", test3);
    root = TOONc_parseString(test3);
    if (root) {
        printf("SUCCESS 3!\n");
        TOONc_printRoot(root);
        TOONc_free(root);
    } else {
        printf("FAILED 3!\n");
    }

    return 0;
}
