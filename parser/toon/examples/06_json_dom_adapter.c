#include "json_parser.h"
#include "toon_json_adapter.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char source[] =
        "{\"name\":\"Ada\",\"active\":true,\"scores\":[7,9.5]}";
    json_value_t *json = json_parse(source, sizeof(source) - 1U);
    json_value_t *roundtrip = NULL;
    toonObject *toon = NULL;
    char *serialized = NULL;
    size_t serialized_len = 0;
    int rc;

    if (!json) {
        fputs("failed to parse the JSON example\n", stderr);
        return 1;
    }

    rc = toon_json_from_value(json, &toon);
    json_free(json);
    if (rc != SALTS_OK) {
        fprintf(stderr, "JSON to TOON conversion failed: %s\n",
            cmeta_strerror(rc));
        return 1;
    }

    printf("TOON name after releasing the source JSON DOM: %s\n",
        TOON_GET_STRING(TOONc_get(toon, "name")));

    rc = toon_json_to_value(toon, &roundtrip);
    TOONc_free(toon);
    if (rc != SALTS_OK) {
        fprintf(stderr, "TOON to JSON conversion failed: %s\n",
            cmeta_strerror(rc));
        return 1;
    }

    serialized = json_serialize_pretty(roundtrip, &serialized_len);
    json_free(roundtrip);
    if (!serialized) {
        fputs("failed to serialize the round-trip JSON DOM\n", stderr);
        return 1;
    }

    fwrite(serialized, 1U, serialized_len, stdout);
    fputc('\n', stdout);
    json_serialize_free(serialized);
    return 0;
}
