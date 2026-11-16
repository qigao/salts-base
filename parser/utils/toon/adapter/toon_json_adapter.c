#include <toon_json_adapter.h>

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <memory/buffer.h>
#include <core/vstr.h>

#define TOON_JSON_ARENA_INITIAL_SIZE (32U * 1024U)
#define TOON_JSON_MAX_EXACT_INTEGER_TEXT "9007199254740992"

/* Conversion-local pointer identity table. Nodes remain borrowed; an existing
 * pointer rejects shared nodes and cycles. Growth commits before insertion. */
typedef struct {
    const toonObject **slots;
    size_t count, capacity;
} toon_json_to_context_t;
static size_t toon_json_pointer_slot(const toonObject *node, size_t capacity) {
    uintptr_t hash = (uintptr_t)node;
    hash ^= hash >> 4;
    hash *= (uintptr_t)UINT64_C(11400714819323198485);
    return (size_t)hash & (capacity - 1);
}
static int toon_json_mark_seen(toon_json_to_context_t *ctx, const toonObject *node) {
    if (ctx->capacity) {
        size_t slot = toon_json_pointer_slot(node, ctx->capacity);
        while (ctx->slots[slot]) {
            if (ctx->slots[slot] == node) return SALTS_EPROTO;
            slot = (slot + 1) & (ctx->capacity - 1);
        }
    }
    if (ctx->count >= ctx->capacity / 2) {
        enum { INITIAL_SEEN_CAPACITY = 64 };
        if (ctx->capacity > SIZE_MAX / sizeof(*ctx->slots) / 2) return SALTS_ERANGE;
        size_t capacity = ctx->capacity ? ctx->capacity * 2 : INITIAL_SEEN_CAPACITY;
        const toonObject **slots = calloc(capacity, sizeof(*slots));
        if (!slots) return SALTS_ENOMEM;
        for (size_t i = 0; i < ctx->capacity; ++i) {
            if (!ctx->slots[i]) continue;
            size_t slot = toon_json_pointer_slot(ctx->slots[i], capacity);
            while (slots[slot]) slot = (slot + 1) & (capacity - 1);
            slots[slot] = ctx->slots[i];
        }
        free(ctx->slots);
        ctx->slots = slots;
        ctx->capacity = capacity;
    }
    size_t slot = toon_json_pointer_slot(node, ctx->capacity);
    while (ctx->slots[slot]) slot = (slot + 1) & (ctx->capacity - 1);
    ctx->slots[slot] = node;
    ++ctx->count;
    return SALTS_OK;
}

static int toon_json_validate_text(const char *text, size_t len)
{
    if (!text || len == SIZE_MAX)
        return SALTS_EPROTO;
    return vstr_utf8_valid(vstr_from_buf(text, len))
        ? SALTS_OK
        : SALTS_ECHARSET;
}

static int toon_json_has_prior_key(const toonObject *head,
    const toonObject *current)
{
    for (const toonObject *prior = head; prior && prior != current;
         prior = prior->next) {
        if (!prior->key)
            return -1;
        if (strcmp(prior->key, current->key) == 0)
            return 1;
    }
    return 0;
}

static int toon_json_to_node(toon_json_to_context_t *ctx,
    const toonObject *node, unsigned depth, json_value_t **out_value)
{
    json_value_t *result = NULL;
    int rc;

    *out_value = NULL;
    if (!node)
        return SALTS_EPROTO;
    if (depth > TOON_JSON_ADAPTER_MAX_DEPTH)
        return SALTS_ERANGE;
    rc = toon_json_mark_seen(ctx, node);
    if (rc != SALTS_OK)
        return rc;

    switch (node->kvtype) {
    case KV_NULL:
        result = json_create_null();
        break;
    case KV_BOOL:
        result = json_create_bool(node->boolean != 0);
        break;
    case KV_INT:
        result = json_create_int64((int64_t)node->i);
        break;
    case KV_DOUBLE:
        if (!isfinite(node->d))
            return SALTS_ERANGE;
        result = json_create_number(node->d);
        break;
    case KV_STRING:
        rc = toon_json_validate_text(node->str.ptr, node->str.len);
        if (rc != SALTS_OK)
            return rc;
        result = json_create_string_n(node->str.ptr, node->str.len);
        break;
    case KV_LIST:
        if (node->array.len > 0U && !node->array.items)
            return SALTS_EPROTO;
        result = json_create_array();
        if (!result)
            return SALTS_ENOMEM;
        for (size_t i = 0; i < node->array.len; ++i) {
            const toonObject *item = node->array.items[i];
            json_value_t *json_item = NULL;
            if (!item || item->key || item->next) {
                json_free(result);
                return SALTS_EPROTO;
            }
            rc = toon_json_to_node(ctx, item, depth + 1U, &json_item);
            if (rc != SALTS_OK) {
                json_free(result);
                return rc;
            }
            if (!json_array_add_checked(result, json_item)) {
                json_free(json_item);
                json_free(result);
                return SALTS_ENOMEM;
            }
        }
        *out_value = result;
        return SALTS_OK;
    case KV_OBJ: {
        const toonObject *head = node->child;
        result = json_create_object();
        if (!result)
            return SALTS_ENOMEM;
        for (const toonObject *child = head; child; child = child->next) {
            json_value_t *json_child = NULL;
            int duplicate;
            size_t key_len;

            if (!child->key) {
                json_free(result);
                return SALTS_EPROTO;
            }
            key_len = strlen(child->key);
            rc = toon_json_validate_text(child->key, key_len);
            if (rc != SALTS_OK) {
                json_free(result);
                return rc;
            }
            duplicate = toon_json_has_prior_key(head, child);
            if (duplicate != 0) {
                json_free(result);
                return SALTS_EPROTO;
            }
            rc = toon_json_to_node(ctx, child, depth + 1U, &json_child);
            if (rc != SALTS_OK) {
                json_free(result);
                return rc;
            }
            if (!json_object_add_n(result, child->key, key_len, json_child)) {
                json_free(json_child);
                json_free(result);
                return SALTS_ENOMEM;
            }
        }
        *out_value = result;
        return SALTS_OK;
    }
    default:
        return SALTS_ENOTSUP;
    }

    if (!result)
        return SALTS_ENOMEM;
    *out_value = result;
    return SALTS_OK;
}

int toon_json_to_value(const toonObject *root, json_value_t **out_value)
{
    toon_json_to_context_t ctx = {0};
    int rc;

    if (!out_value)
        return SALTS_EINVAL;
    *out_value = NULL;
    if (!root)
        return SALTS_EINVAL;
    if (root->key || root->next)
        return SALTS_EPROTO;

    rc = toon_json_to_node(&ctx, root, 0U, out_value);
    free(ctx.slots);
    return rc;
}

static int toon_json_integer_token_exceeds_exact_double(
    const json_value_t *value)
{
    static const char limit[] = TOON_JSON_MAX_EXACT_INTEGER_TEXT;
    size_t len = 0;
    const char *text = json_number_text(value, &len);
    size_t offset = 0;
    size_t digits;

    if (!text || len == 0U)
        return 0;
    if (text[0] == '-')
        offset = 1U;
    for (size_t i = offset; i < len; ++i) {
        if (text[i] == '.' || text[i] == 'e' || text[i] == 'E')
            return 0;
    }
    while (offset < len && text[offset] == '0')
        ++offset;
    digits = len - offset;
    if (digits < sizeof(limit) - 1U)
        return 0;
    if (digits > sizeof(limit) - 1U)
        return 1;
    return memcmp(text + offset, limit, sizeof(limit) - 1U) > 0;
}

static int toon_json_from_node(mem_pool_t *arena, const json_value_t *value,
    unsigned depth, toonObject **out_node)
{
    toonObject *node = NULL;
    int rc = SALTS_OK;

    *out_node = NULL;
    if (!value)
        return SALTS_EPROTO;
    if (depth > TOON_JSON_ADAPTER_MAX_DEPTH)
        return SALTS_ERANGE;

    switch (json_type(value)) {
    case JSON_NULL:
        node = TOONc_newNullObjArena(arena);
        break;
    case JSON_BOOL:
        node = TOONc_newBoolObjArena(arena, json_bool(value) ? 1 : 0);
        break;
    case JSON_NUMBER: {
        double number = json_number(value);
        if (!isfinite(number) || toon_json_integer_token_exceeds_exact_double(value))
            return SALTS_ERANGE;
        if (!(number == 0.0 && signbit(number))
            && number >= (double)INT_MIN && number <= (double)INT_MAX
            && (double)(int)number == number) {
            node = TOONc_newIntObjArena(arena, (int)number);
        } else {
            node = TOONc_newDoubleObjArena(arena, number);
        }
        break;
    }
    case JSON_STRING: {
        const char *string = json_string(value);
        size_t len = json_string_len(value);
        rc = toon_json_validate_text(string, len);
        if (rc != SALTS_OK)
            return rc;
        node = TOONc_newStringObjArena(arena, (char *)string, len);
        break;
    }
    case JSON_ARRAY: {
        size_t count = json_array_size(value);
        if (count > SIZE_MAX / sizeof(toonObject *))
            return SALTS_ERANGE;
        node = TOONc_newListObjArena(arena, count);
        if (!node)
            return SALTS_ENOMEM;
        for (size_t i = 0; i < count; ++i) {
            toonObject *item = NULL;
            size_t previous_len = node->array.len;
            rc = toon_json_from_node(arena, json_array_get(value, i),
                depth + 1U, &item);
            if (rc != SALTS_OK)
                return rc;
            TOONc_listPushArena(arena, node, item);
            if (node->array.len != previous_len + 1U)
                return SALTS_ENOMEM;
        }
        break;
    }
    case JSON_OBJECT: {
        size_t count = json_object_size(value);
        toonObject *last = NULL;
        node = TOONc_newObjectArena(arena, KV_OBJ);
        if (!node)
            return SALTS_ENOMEM;
        for (size_t i = 0; i < count; ++i) {
            const char *key = json_object_key(value, i);
            size_t key_len = json_object_key_len(value, i);
            toonObject *child = NULL;
            char *key_copy;

            if (!key || key_len == SIZE_MAX)
                return SALTS_EPROTO;
            rc = toon_json_validate_text(key, key_len);
            if (rc != SALTS_OK)
                return rc;
            if (memchr(key, '\0', key_len))
                return SALTS_ENOTSUP;
            rc = toon_json_from_node(arena, json_object_value(value, i),
                depth + 1U, &child);
            if (rc != SALTS_OK)
                return rc;
            key_copy = mem_alloc(arena, key_len + 1U);
            if (!key_copy)
                return SALTS_ENOMEM;
            memcpy(key_copy, key, key_len);
            key_copy[key_len] = '\0';
            child->key = key_copy;
            if (last)
                last->next = child;
            else
                node->child = child;
            last = child;
        }
        break;
    }
    default:
        return SALTS_ENOTSUP;
    }

    if (!node)
        return SALTS_ENOMEM;
    *out_node = node;
    return SALTS_OK;
}

int toon_json_from_value(const json_value_t *value, toonObject **out_root)
{
    mem_pool_t *arena;
    toonObject *root = NULL;
    int rc;

    if (!out_root)
        return SALTS_EINVAL;
    *out_root = NULL;
    if (!value)
        return SALTS_EINVAL;

    arena = malloc(sizeof(*arena));
    if (!arena)
        return SALTS_ENOMEM;
    if (mem_init(arena, TOON_JSON_ARENA_INITIAL_SIZE) != 0) {
        free(arena);
        return SALTS_ENOMEM;
    }

    rc = toon_json_from_node(arena, value, 0U, &root);
    if (rc != SALTS_OK) {
        mem_destroy(arena);
        free(arena);
        return rc;
    }

    root->arena = arena;
    *out_root = root;
    return SALTS_OK;
}
