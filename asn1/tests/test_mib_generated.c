#include "Basic-MIB.h"
#include "Empty-MIB.h"
#include <tinytest.h>

spec("compiled MIB output") {
    it("links generated constants and resolves the generated runtime table") {
        const uint32_t expected[] = {1u, 3u, 6u, 1u, 4u, 1u, 32473u, 7u};
        check_equal(example_node_OID_LEN, 8u);
        check_equal(example_node_OID, expected, sizeof(expected));
        check_equal(Basic_MIB_module.name, "Basic-MIB");
        check_equal(Basic_MIB_module.count, (size_t)3u);
        check_equal(Basic_MIB_module.entries[2].name, "example-node");
        check_equal(Basic_MIB_module.entries[2].count, (size_t)8u);
        check_equal(Basic_MIB_module.entries[2].components, expected, sizeof(expected));
        check_equal(Basic_MIB_module.entries[0].components, internet_OID,
                    internet_OID_LEN * sizeof(uint32_t));
    }
    it("compiles an empty module without zero-length arrays") {
        check_equal(Empty_MIB_module.name, "Empty-MIB");
        check_equal(Empty_MIB_module.count, (size_t)0u);
        check_null(Empty_MIB_module.entries);
    }
}
