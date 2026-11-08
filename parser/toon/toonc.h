#ifndef _TOONC_H
#define _TOONC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>

/* ===================== Key-value types ======================*/
#define KV_STRING 0
#define KV_INT    1
#define KV_BOOL   2
#define KV_NULL   3
#define KV_DOUBLE 4
#define KV_OBJ    5
#define KV_LIST   6
#define KV_LOBJ   7

/* ======================= Data Structures ======================= */

struct toonStr {
    char *ptr;
    size_t len;
};

typedef struct toonObject {
    int kvtype;
    int indent;
    char *key;
    union {
        struct toonStr str;
        int i;
        double d;
        int boolean;

        struct {
            struct toonObject **items;
            size_t len;
            size_t capacity;
        } array;
    };

    struct toonObject *child;
    struct toonObject *next;
    void *arena; // Optional arena for root objects
} toonObject;

typedef struct toonParser {
    char *source;
    char *p;
    int line;
} toonParser;

/* ======================= Memory Management ======================= */

void *TOONc_malloc(size_t size);
void *TOONc_calloc(size_t nmemb, size_t size);
void *TOONc_realloc(void *ptr, size_t size);

/* ======================= Object Creation ======================= */

toonObject *TOONc_newObject(int kvtype);
toonObject *TOONc_newStringObj(char *s, size_t len);
toonObject *TOONc_newIntObj(int value);
toonObject *TOONc_newDoubleObj(double value);
toonObject *TOONc_newBoolObj(int value);
toonObject *TOONc_newNullObj(void);
toonObject *TOONc_newListObj(void);

toonObject *TOONc_newObjectArena(void *arena, int kvtype);
toonObject *TOONc_newStringObjArena(void *arena, char *s, size_t len);
toonObject *TOONc_newIntObjArena(void *arena, int value);
toonObject *TOONc_newDoubleObjArena(void *arena, double value);
toonObject *TOONc_newBoolObjArena(void *arena, int value);
toonObject *TOONc_newNullObjArena(void *arena);
toonObject *TOONc_newListObjArena(void *arena, size_t initial_capacity);

void TOONc_listPush(toonObject *list, toonObject *item);
void TOONc_listPushArena(void *arena, toonObject *list, toonObject *item);
void TOONc_listReserveArena(void *arena, toonObject *list, size_t capacity);

/* ======================= Core API ======================= */

/**
 * Parse a TOON file
 * @param fp File pointer (will be closed by this function)
 * @return Root toonObject or NULL on error
 */
toonObject *TOONc_parseFile(FILE *fp);

/**
 * Parse a TOON string with known length
 * @param str TOON formatted string
 * @param len Length of the string
 * @return Root toonObject or NULL on error
 */
toonObject *TOONc_parseStringLen(const char *str, size_t len);

/**
 * Parse a TOON string
 * @param str TOON formatted string
 * @return Root toonObject or NULL on error
 */
toonObject *TOONc_parseString(const char *str);

/**
 * Get an object by path (dot notation)
 * @param root Root object
 * @param path Path like "context.task" or "friends"
 * @return Found object or NULL
 */
toonObject *TOONc_get(toonObject *root, const char *path);

/**
 * Get item of an array
 * @param arr Array object
 * @param index Array index
 * @return The indexed item or NULL
 */
toonObject *TOONc_getArrayItem(toonObject *arr, size_t index);

/**
 * Get the array length
 * @param arr Array object
 * @return Number (size_t) of items or -1
 */
size_t TOONc_getArrayLength(toonObject *arr);

/**
 * Print recursively an object
 * @param o Generic object
 * @param depth Max indentation depth
 */
void TOONc_printObject(toonObject *o, int depth);

/**
 * Print from root object (root excluded)
 * @param root Root object
 */
void TOONc_printRoot(toonObject *root);

/**
 * Free a TOON object tree recursively
 * @param obj Object to free
 */
void TOONc_free(toonObject *obj);

/**
 * Serialize a TOON tree as pretty-printed JSON.
 * @param obj Root TOON value; must not have a key or sibling
 * @param fp Output stream
 * @param depth Reserved for source compatibility; pass 0
 */
void TOONc_toJSON(toonObject *obj, FILE *fp, int depth);

/**
 * Convert a TOON tree to a pretty-printed JSON string.
 * @param obj Root TOON value; must not have a key or sibling
 * @param out_len Optional output byte length
 * @return Allocated JSON string, or NULL on conversion/allocation failure;
 *         release with TOONc_serializeFree()
 */
char *TOONc_toJSONString(const toonObject *obj, size_t *out_len);

/**
 * Parse a JSON string into an independently owned TOON tree.
 * @param json JSON input buffer
 * @param len Input byte length
 * @return TOON root released with TOONc_free(), or NULL on failure
 */
toonObject *TOONc_fromJSONString(const char *json, size_t len);

/**
 * Serialize TOON object to string
 * @param obj Object to serialize
 * @param out_len Optional pointer to receive output length
 * @return Allocated string or NULL (must be freed with TOONc_serializeFree)
 */
char *TOONc_serialize(const toonObject *obj, size_t *out_len);

/**
 * Free string allocated by TOONc_serialize
 * @param str String to free
 */
void TOONc_serializeFree(char *str);

/* ======================= Type Checking Macros ======================= */

#define TOON_IS_STRING(obj)  ((obj) && (obj)->kvtype == KV_STRING)
#define TOON_IS_INT(obj)     ((obj) && (obj)->kvtype == KV_INT)
#define TOON_IS_DOUBLE(obj)  ((obj) && (obj)->kvtype == KV_DOUBLE)
#define TOON_IS_BOOL(obj)    ((obj) && (obj)->kvtype == KV_BOOL)
#define TOON_IS_NULL(obj)    ((obj) && (obj)->kvtype == KV_NULL)
#define TOON_IS_LIST(obj)    ((obj) && (obj)->kvtype == KV_LIST)
#define TOON_IS_OBJ(obj)     ((obj) && (obj)->kvtype == KV_OBJ)

/* ======================= Value Getters ======================= */

#define TOON_GET_STRING(obj) (TOON_IS_STRING(obj) ? (obj)->str.ptr : NULL)
#define TOON_GET_INT(obj)    (TOON_IS_INT(obj) ? (obj)->i : 0)
#define TOON_GET_DOUBLE(obj) (TOON_IS_DOUBLE(obj) ? (obj)->d : 0.0)
#define TOON_GET_BOOL(obj)   (TOON_IS_BOOL(obj) ? (obj)->boolean : 0)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _TOONC_H */
