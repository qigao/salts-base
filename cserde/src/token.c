#include <cserde/token.h>

bool cserde_token_kind_valid(cserde_token_kind kind) {
    return kind >= CSERDE_NULL && kind <= CSERDE_MAP_END;
}

bool cserde_view_lifetime_valid(cserde_view_lifetime lifetime) {
    return lifetime == CSERDE_VIEW_TRANSIENT ||
           lifetime == CSERDE_VIEW_STABLE;
}

bool cserde_token_valid(const cserde_token *token) {
    if (token == NULL || !cserde_token_kind_valid(token->kind))
        return false;
    if (token->kind != CSERDE_STRING && token->kind != CSERDE_BYTES)
        return true;
    if (!cserde_view_lifetime_valid(token->value.slice.lifetime))
        return false;
    return token->value.slice.size == 0u || token->value.slice.data != NULL;
}
