#include "unicode.h"
#include "unicode_line_break_data.h"

#include <stddef.h>
#include <stdint.h>

typedef struct salts_unicode_lb_info {
  uint32_t scalar;
  salts_unicode_line_break original;
  salts_unicode_line_break cls;
  size_t start;
  size_t end;
  uint8_t ignored;
  uint8_t pi;
  uint8_t pf;
  uint8_t east_asian;
  uint8_t potential_emoji;
} salts_unicode_lb_info;

typedef struct salts_unicode_lb_scan_state {
  salts_unicode_line_break inherited;
  uint8_t inherited_valid;
} salts_unicode_lb_scan_state;

typedef struct salts_unicode_lb_context {
  salts_unicode_lb_info left_any;
  salts_unicode_lb_info left_any_non_space;
  salts_unicode_lb_info right_any;
  salts_unicode_lb_info left;
  salts_unicode_lb_info left2;
  salts_unicode_lb_info left_non_space;
  salts_unicode_lb_info before_left_non_space;
  salts_unicode_lb_info right;
  salts_unicode_lb_info right2;
  salts_unicode_lb_info right3;
  uint32_t trailing_ri;
  uint8_t suffix_nu_sy_is;
  uint8_t suffix_nu_sy_is_before_left;
  uint8_t has_left_any;
  uint8_t has_left_any_non_space;
  uint8_t has_right_any;
  uint8_t has_left;
  uint8_t has_left2;
  uint8_t has_left_non_space;
  uint8_t has_before_left_non_space;
  uint8_t has_right;
  uint8_t has_right2;
  uint8_t has_right3;
} salts_unicode_lb_context;

typedef enum salts_unicode_lb_decision {
  SALTS_UNICODE_LB_NO_BREAK = 0,
  SALTS_UNICODE_LB_ALLOW_BREAK = 1,
  SALTS_UNICODE_LB_MUST_BREAK = 2
} salts_unicode_lb_decision;

static uint8_t salts_unicode_lb_lookup_value(
    const salts_unicode_line_break_range *ranges, size_t count,
    uint32_t scalar, uint8_t default_value) {
  size_t lo = 0u;
  size_t hi = count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const salts_unicode_line_break_range *range = &ranges[mid];
    if (scalar < range->first) {
      hi = mid;
    } else if (scalar > range->last) {
      lo = mid + 1u;
    } else {
      return range->value;
    }
  }
  return default_value;
}

static int salts_unicode_lb_lookup_bool(
    const salts_unicode_line_break_bool_range *ranges, size_t count,
    uint32_t scalar) {
  size_t lo = 0u;
  size_t hi = count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const salts_unicode_line_break_bool_range *range = &ranges[mid];
    if (scalar < range->first) {
      hi = mid;
    } else if (scalar > range->last) {
      lo = mid + 1u;
    } else {
      return 1;
    }
  }
  return 0;
}

static salts_unicode_line_break salts_unicode_lb_original(uint32_t scalar) {
  return (salts_unicode_line_break)salts_unicode_lb_lookup_value(
      salts_unicode_line_break_ranges, SALTS_UNICODE_LINE_BREAK_RANGES_COUNT,
      scalar, SALTS_UNICODE_LINE_BREAK_XX);
}

static int salts_unicode_lb_is_mn_mc(uint32_t scalar) {
  return salts_unicode_lb_lookup_bool(
      salts_unicode_line_break_mn_mc_ranges,
      SALTS_UNICODE_LINE_BREAK_MN_MC_RANGES_COUNT, scalar);
}

static int salts_unicode_lb_is_pi(uint32_t scalar) {
  return salts_unicode_lb_lookup_bool(
      salts_unicode_line_break_pi_ranges,
      SALTS_UNICODE_LINE_BREAK_PI_RANGES_COUNT, scalar);
}

static int salts_unicode_lb_is_pf(uint32_t scalar) {
  return salts_unicode_lb_lookup_bool(
      salts_unicode_line_break_pf_ranges,
      SALTS_UNICODE_LINE_BREAK_PF_RANGES_COUNT, scalar);
}

static int salts_unicode_lb_is_east_asian(uint32_t scalar) {
  return salts_unicode_lb_lookup_bool(
      salts_unicode_line_break_east_asian_ranges,
      SALTS_UNICODE_LINE_BREAK_EAST_ASIAN_RANGES_COUNT, scalar);
}

static int salts_unicode_lb_is_potential_emoji(uint32_t scalar) {
  return salts_unicode_lb_lookup_bool(
      salts_unicode_line_break_potential_emoji_ranges,
      SALTS_UNICODE_LINE_BREAK_POTENTIAL_EMOJI_RANGES_COUNT, scalar);
}

static salts_unicode_line_break salts_unicode_lb_resolve(
    salts_unicode_line_break value, uint32_t scalar) {
  switch (value) {
    case SALTS_UNICODE_LINE_BREAK_AI:
    case SALTS_UNICODE_LINE_BREAK_SG:
    case SALTS_UNICODE_LINE_BREAK_XX:
      return SALTS_UNICODE_LINE_BREAK_AL;
    case SALTS_UNICODE_LINE_BREAK_CJ:
      return SALTS_UNICODE_LINE_BREAK_NS;
    case SALTS_UNICODE_LINE_BREAK_SA:
      return salts_unicode_lb_is_mn_mc(scalar)
                 ? SALTS_UNICODE_LINE_BREAK_CM
                 : SALTS_UNICODE_LINE_BREAK_AL;
    default:
      return value;
  }
}

static int salts_unicode_lb_is_lb9_barrier(salts_unicode_line_break value) {
  return value == SALTS_UNICODE_LINE_BREAK_BK ||
         value == SALTS_UNICODE_LINE_BREAK_CR ||
         value == SALTS_UNICODE_LINE_BREAK_LF ||
         value == SALTS_UNICODE_LINE_BREAK_NL ||
         value == SALTS_UNICODE_LINE_BREAK_SP ||
         value == SALTS_UNICODE_LINE_BREAK_ZW;
}

static salts_unicode_status salts_unicode_lb_scan_one(
    vstr input, size_t *cursor, salts_unicode_lb_scan_state *state,
    salts_unicode_lb_info *out) {
  salts_unicode_scalar scalar;
  salts_unicode_status status;
  salts_unicode_line_break resolved;
  const size_t start = *cursor;

  status = salts_unicode_utf8_next(input, cursor, &scalar);
  if (status != SALTS_UNICODE_OK) return status;

  out->scalar = scalar.value;
  out->start = start;
  out->end = *cursor;
  out->original = salts_unicode_lb_original(scalar.value);
  resolved = salts_unicode_lb_resolve(out->original, scalar.value);
  out->ignored = 0u;

  if (resolved == SALTS_UNICODE_LINE_BREAK_CM ||
      resolved == SALTS_UNICODE_LINE_BREAK_ZWJ) {
    if (state->inherited_valid) {
      out->cls = state->inherited;
      out->ignored = 1u;
    } else {
      /* LB10: remaining CM/ZWJ behave as AL. They do not become an LB9
       * base retroactively for a following combining mark. */
      out->cls = SALTS_UNICODE_LINE_BREAK_AL;
    }
  } else {
    out->cls = resolved;
    if (salts_unicode_lb_is_lb9_barrier(resolved)) {
      state->inherited_valid = 0u;
    } else {
      state->inherited = resolved;
      state->inherited_valid = 1u;
    }
  }

  out->pi = (uint8_t)salts_unicode_lb_is_pi(scalar.value);
  out->pf = (uint8_t)salts_unicode_lb_is_pf(scalar.value);
  out->east_asian = (uint8_t)salts_unicode_lb_is_east_asian(scalar.value);
  out->potential_emoji =
      (uint8_t)salts_unicode_lb_is_potential_emoji(scalar.value);
  return SALTS_UNICODE_OK;
}

static salts_unicode_status salts_unicode_lb_validate(vstr input,
                                                      size_t target) {
  size_t cursor = 0u;
  int found = target == 0u;
  salts_unicode_scalar scalar;

  if ((input.data == NULL && input.len != 0u) || target > input.len)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  while (cursor < input.len) {
    salts_unicode_status status =
        salts_unicode_utf8_next(input, &cursor, &scalar);
    if (status != SALTS_UNICODE_OK) return status;
    if (cursor == target) found = 1;
  }
  return found ? SALTS_UNICODE_OK : SALTS_UNICODE_ERR_INVALID_ARGUMENT;
}

static void salts_unicode_lb_push_left_logical(
    salts_unicode_lb_context *ctx, const salts_unicode_lb_info *info) {
  ctx->suffix_nu_sy_is_before_left = ctx->suffix_nu_sy_is;
  if (ctx->has_left) {
    ctx->left2 = ctx->left;
    ctx->has_left2 = 1u;
  }
  if (info->cls != SALTS_UNICODE_LINE_BREAK_SP) {
    if (ctx->has_left) {
      ctx->before_left_non_space = ctx->left;
      ctx->has_before_left_non_space = 1u;
    } else {
      ctx->has_before_left_non_space = 0u;
    }
    ctx->left_non_space = *info;
    ctx->has_left_non_space = 1u;
  }
  ctx->left = *info;
  ctx->has_left = 1u;

  if (info->cls == SALTS_UNICODE_LINE_BREAK_RI) {
    ++ctx->trailing_ri;
  } else {
    ctx->trailing_ri = 0u;
  }

  if (info->cls == SALTS_UNICODE_LINE_BREAK_NU) {
    ctx->suffix_nu_sy_is = 1u;
  } else if ((info->cls == SALTS_UNICODE_LINE_BREAK_SY ||
              info->cls == SALTS_UNICODE_LINE_BREAK_IS) &&
             ctx->suffix_nu_sy_is) {
    /* Preserve NU (SY|IS)* suffix state. */
  } else {
    ctx->suffix_nu_sy_is = 0u;
  }
}

static void salts_unicode_lb_push_right_logical(
    salts_unicode_lb_context *ctx, const salts_unicode_lb_info *info) {
  if (!ctx->has_right) {
    ctx->right = *info;
    ctx->has_right = 1u;
  } else if (!ctx->has_right2) {
    ctx->right2 = *info;
    ctx->has_right2 = 1u;
  } else if (!ctx->has_right3) {
    ctx->right3 = *info;
    ctx->has_right3 = 1u;
  }
}

static salts_unicode_status salts_unicode_lb_context_at(
    vstr input, size_t offset, salts_unicode_lb_context *ctx) {
  size_t cursor = 0u;
  salts_unicode_lb_scan_state state = {SALTS_UNICODE_LINE_BREAK_AL, 0u};
  *ctx = (salts_unicode_lb_context){0};

  while (cursor < input.len) {
    salts_unicode_lb_info info;
    salts_unicode_status status =
        salts_unicode_lb_scan_one(input, &cursor, &state, &info);
    if (status != SALTS_UNICODE_OK) return status;

    if (info.end <= offset) {
      ctx->left_any = info;
      ctx->has_left_any = 1u;
      if (info.original != SALTS_UNICODE_LINE_BREAK_SP) {
        ctx->left_any_non_space = info;
        ctx->has_left_any_non_space = 1u;
      }
      if (!info.ignored)
        salts_unicode_lb_push_left_logical(ctx, &info);
    } else if (info.start >= offset) {
      if (!ctx->has_right_any) {
        ctx->right_any = info;
        ctx->has_right_any = 1u;
      }
      if (!info.ignored)
        salts_unicode_lb_push_right_logical(ctx, &info);
    } else {
      return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
    }
  }
  return SALTS_UNICODE_OK;
}

static int salts_unicode_lb_is_hard(salts_unicode_line_break value) {
  return value == SALTS_UNICODE_LINE_BREAK_BK ||
         value == SALTS_UNICODE_LINE_BREAK_CR ||
         value == SALTS_UNICODE_LINE_BREAK_LF ||
         value == SALTS_UNICODE_LINE_BREAK_NL;
}

static int salts_unicode_lb_is_alpha(salts_unicode_line_break value) {
  return value == SALTS_UNICODE_LINE_BREAK_AL ||
         value == SALTS_UNICODE_LINE_BREAK_HL;
}

static int salts_unicode_lb_is_hangul(salts_unicode_line_break value) {
  return value == SALTS_UNICODE_LINE_BREAK_JL ||
         value == SALTS_UNICODE_LINE_BREAK_JV ||
         value == SALTS_UNICODE_LINE_BREAK_JT ||
         value == SALTS_UNICODE_LINE_BREAK_H2 ||
         value == SALTS_UNICODE_LINE_BREAK_H3;
}

static int salts_unicode_lb_is_aksara(const salts_unicode_lb_info *info) {
  return info->cls == SALTS_UNICODE_LINE_BREAK_AK ||
         info->cls == SALTS_UNICODE_LINE_BREAK_AS ||
         info->scalar == 0x25CCu;
}

static int salts_unicode_lb_is_ak_or_dotted(
    const salts_unicode_lb_info *info) {
  return info->cls == SALTS_UNICODE_LINE_BREAK_AK ||
         info->scalar == 0x25CCu;
}

static int salts_unicode_lb_is_lb15b_follow(salts_unicode_line_break value) {
  return value == SALTS_UNICODE_LINE_BREAK_SP ||
         value == SALTS_UNICODE_LINE_BREAK_GL ||
         value == SALTS_UNICODE_LINE_BREAK_WJ ||
         value == SALTS_UNICODE_LINE_BREAK_CL ||
         value == SALTS_UNICODE_LINE_BREAK_QU ||
         value == SALTS_UNICODE_LINE_BREAK_CP ||
         value == SALTS_UNICODE_LINE_BREAK_EX ||
         value == SALTS_UNICODE_LINE_BREAK_IS ||
         value == SALTS_UNICODE_LINE_BREAK_SY ||
         salts_unicode_lb_is_hard(value) ||
         value == SALTS_UNICODE_LINE_BREAK_ZW;
}

static int salts_unicode_lb_is_lb15a_prefix(salts_unicode_line_break value) {
  return salts_unicode_lb_is_hard(value) ||
         value == SALTS_UNICODE_LINE_BREAK_OP ||
         value == SALTS_UNICODE_LINE_BREAK_QU ||
         value == SALTS_UNICODE_LINE_BREAK_GL ||
         value == SALTS_UNICODE_LINE_BREAK_SP ||
         value == SALTS_UNICODE_LINE_BREAK_ZW;
}

static int salts_unicode_lb_is_lb20a_prefix(salts_unicode_line_break value) {
  return salts_unicode_lb_is_hard(value) ||
         value == SALTS_UNICODE_LINE_BREAK_SP ||
         value == SALTS_UNICODE_LINE_BREAK_ZW ||
         value == SALTS_UNICODE_LINE_BREAK_CB ||
         value == SALTS_UNICODE_LINE_BREAK_GL;
}

static salts_unicode_lb_decision salts_unicode_lb_decide(
    vstr input, size_t offset, salts_unicode_status *out_status) {
  salts_unicode_lb_context ctx;
  salts_unicode_status status;

  *out_status = SALTS_UNICODE_OK;
  if (offset == 0u) return SALTS_UNICODE_LB_NO_BREAK;
  if (offset == input.len) return SALTS_UNICODE_LB_MUST_BREAK;

  status = salts_unicode_lb_context_at(input, offset, &ctx);
  if (status != SALTS_UNICODE_OK) {
    *out_status = status;
    return SALTS_UNICODE_LB_NO_BREAK;
  }
  if (!ctx.has_left_any || !ctx.has_right_any) {
    *out_status = SALTS_UNICODE_ERR_INVALID_ARGUMENT;
    return SALTS_UNICODE_LB_NO_BREAK;
  }

  /* LB4 / LB5. */
  if (ctx.left_any.original == SALTS_UNICODE_LINE_BREAK_BK)
    return SALTS_UNICODE_LB_MUST_BREAK;
  if (ctx.left_any.original == SALTS_UNICODE_LINE_BREAK_CR &&
      ctx.right_any.original == SALTS_UNICODE_LINE_BREAK_LF)
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.left_any.original == SALTS_UNICODE_LINE_BREAK_CR ||
      ctx.left_any.original == SALTS_UNICODE_LINE_BREAK_LF ||
      ctx.left_any.original == SALTS_UNICODE_LINE_BREAK_NL)
    return SALTS_UNICODE_LB_MUST_BREAK;

  /* LB6 / LB7. */
  if (salts_unicode_lb_is_hard(ctx.right_any.original))
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.right_any.original == SALTS_UNICODE_LINE_BREAK_SP ||
      ctx.right_any.original == SALTS_UNICODE_LINE_BREAK_ZW)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB8. */
  if (ctx.has_left_any_non_space &&
      ctx.left_any_non_space.original == SALTS_UNICODE_LINE_BREAK_ZW)
    return SALTS_UNICODE_LB_ALLOW_BREAK;

  /* LB8a. */
  if (ctx.left_any.original == SALTS_UNICODE_LINE_BREAK_ZWJ)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB9. */
  if (ctx.right_any.ignored)
    return SALTS_UNICODE_LB_NO_BREAK;

  if (!ctx.has_left || !ctx.has_right) {
    *out_status = SALTS_UNICODE_ERR_INVALID_ARGUMENT;
    return SALTS_UNICODE_LB_NO_BREAK;
  }

  /* LB11 / LB12 / LB12a. */
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_WJ ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_WJ)
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_GL)
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_GL &&
      ctx.left.cls != SALTS_UNICODE_LINE_BREAK_SP &&
      ctx.left.cls != SALTS_UNICODE_LINE_BREAK_BA &&
      ctx.left.cls != SALTS_UNICODE_LINE_BREAK_HY &&
      ctx.left.cls != SALTS_UNICODE_LINE_BREAK_HH)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB13. */
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_CL ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_CP ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_EX ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_SY)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB14. */
  if (ctx.has_left_non_space &&
      ctx.left_non_space.cls == SALTS_UNICODE_LINE_BREAK_OP)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB15a. */
  if (ctx.has_left_non_space &&
      ctx.left_non_space.cls == SALTS_UNICODE_LINE_BREAK_QU &&
      ctx.left_non_space.pi &&
      (!ctx.has_before_left_non_space ||
       salts_unicode_lb_is_lb15a_prefix(
           ctx.before_left_non_space.cls)))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB15b. */
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_QU && ctx.right.pf &&
      (!ctx.has_right2 ||
       salts_unicode_lb_is_lb15b_follow(ctx.right2.cls)))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB15c / LB15d. */
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_SP &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_IS &&
      ctx.has_right2 && ctx.right2.cls == SALTS_UNICODE_LINE_BREAK_NU)
    return SALTS_UNICODE_LB_ALLOW_BREAK;
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_IS)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB16 / LB17. */
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_NS &&
      ctx.has_left_non_space &&
      (ctx.left_non_space.cls == SALTS_UNICODE_LINE_BREAK_CL ||
       ctx.left_non_space.cls == SALTS_UNICODE_LINE_BREAK_CP))
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_B2 &&
      ctx.has_left_non_space &&
      ctx.left_non_space.cls == SALTS_UNICODE_LINE_BREAK_B2)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB18. */
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_SP)
    return SALTS_UNICODE_LB_ALLOW_BREAK;

  /* LB19. */
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_QU && !ctx.right.pi)
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_QU && !ctx.left.pf)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB19a. */
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_QU) {
    if (!ctx.left.east_asian)
      return SALTS_UNICODE_LB_NO_BREAK;
    if (!ctx.has_right2 || !ctx.right2.east_asian)
      return SALTS_UNICODE_LB_NO_BREAK;
  }
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_QU) {
    if (!ctx.right.east_asian)
      return SALTS_UNICODE_LB_NO_BREAK;
    if (!ctx.has_left2 || !ctx.left2.east_asian)
      return SALTS_UNICODE_LB_NO_BREAK;
  }

  /* LB20. */
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_CB ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_CB)
    return SALTS_UNICODE_LB_ALLOW_BREAK;

  /* LB20a. */
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_HY ||
       ctx.left.cls == SALTS_UNICODE_LINE_BREAK_HH) &&
      salts_unicode_lb_is_alpha(ctx.right.cls) &&
      (!ctx.has_left2 || salts_unicode_lb_is_lb20a_prefix(ctx.left2.cls)))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB21 / LB21a / LB21b. */
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_BA ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_HH ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_HY ||
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_NS ||
      ctx.left.cls == SALTS_UNICODE_LINE_BREAK_BB)
    return SALTS_UNICODE_LB_NO_BREAK;
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_HY ||
       ctx.left.cls == SALTS_UNICODE_LINE_BREAK_HH) &&
      ctx.has_left2 && ctx.left2.cls == SALTS_UNICODE_LINE_BREAK_HL &&
      ctx.right.cls != SALTS_UNICODE_LINE_BREAK_HL)
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_SY &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_HL)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB22. */
  if (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_IN)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB23 / LB23a / LB24. */
  if ((salts_unicode_lb_is_alpha(ctx.left.cls) &&
       ctx.right.cls == SALTS_UNICODE_LINE_BREAK_NU) ||
      (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_NU &&
       salts_unicode_lb_is_alpha(ctx.right.cls)))
    return SALTS_UNICODE_LB_NO_BREAK;
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PR &&
       (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_ID ||
        ctx.right.cls == SALTS_UNICODE_LINE_BREAK_EB ||
        ctx.right.cls == SALTS_UNICODE_LINE_BREAK_EM)) ||
      ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_ID ||
        ctx.left.cls == SALTS_UNICODE_LINE_BREAK_EB ||
        ctx.left.cls == SALTS_UNICODE_LINE_BREAK_EM) &&
       ctx.right.cls == SALTS_UNICODE_LINE_BREAK_PO))
    return SALTS_UNICODE_LB_NO_BREAK;
  if (((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PR ||
        ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PO) &&
       salts_unicode_lb_is_alpha(ctx.right.cls)) ||
      (salts_unicode_lb_is_alpha(ctx.left.cls) &&
       (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_PR ||
        ctx.right.cls == SALTS_UNICODE_LINE_BREAK_PO)))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB25. */
  if ((ctx.right.cls == SALTS_UNICODE_LINE_BREAK_PO ||
       ctx.right.cls == SALTS_UNICODE_LINE_BREAK_PR)) {
    if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_CL ||
         ctx.left.cls == SALTS_UNICODE_LINE_BREAK_CP) &&
        ctx.suffix_nu_sy_is_before_left)
      return SALTS_UNICODE_LB_NO_BREAK;
    if (ctx.suffix_nu_sy_is)
      return SALTS_UNICODE_LB_NO_BREAK;
  }
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PO ||
       ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PR) &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_OP &&
      ctx.has_right2 &&
      (ctx.right2.cls == SALTS_UNICODE_LINE_BREAK_NU ||
       (ctx.right2.cls == SALTS_UNICODE_LINE_BREAK_IS &&
        ctx.has_right3 &&
        ctx.right3.cls == SALTS_UNICODE_LINE_BREAK_NU)))
    return SALTS_UNICODE_LB_NO_BREAK;
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PO ||
       ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PR) &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_NU)
    return SALTS_UNICODE_LB_NO_BREAK;
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_HY ||
       ctx.left.cls == SALTS_UNICODE_LINE_BREAK_IS) &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_NU)
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.suffix_nu_sy_is &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_NU)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB26 / LB27. */
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_JL &&
       (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_JL ||
        ctx.right.cls == SALTS_UNICODE_LINE_BREAK_JV ||
        ctx.right.cls == SALTS_UNICODE_LINE_BREAK_H2 ||
        ctx.right.cls == SALTS_UNICODE_LINE_BREAK_H3)) ||
      ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_JV ||
        ctx.left.cls == SALTS_UNICODE_LINE_BREAK_H2) &&
       (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_JV ||
        ctx.right.cls == SALTS_UNICODE_LINE_BREAK_JT)) ||
      ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_JT ||
        ctx.left.cls == SALTS_UNICODE_LINE_BREAK_H3) &&
       ctx.right.cls == SALTS_UNICODE_LINE_BREAK_JT))
    return SALTS_UNICODE_LB_NO_BREAK;
  if ((salts_unicode_lb_is_hangul(ctx.left.cls) &&
       ctx.right.cls == SALTS_UNICODE_LINE_BREAK_PO) ||
      (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_PR &&
       salts_unicode_lb_is_hangul(ctx.right.cls)))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB28. */
  if (salts_unicode_lb_is_alpha(ctx.left.cls) &&
      salts_unicode_lb_is_alpha(ctx.right.cls))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB28a. */
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_AP &&
      salts_unicode_lb_is_aksara(&ctx.right))
    return SALTS_UNICODE_LB_NO_BREAK;
  if (salts_unicode_lb_is_aksara(&ctx.left) &&
      (ctx.right.cls == SALTS_UNICODE_LINE_BREAK_VF ||
       ctx.right.cls == SALTS_UNICODE_LINE_BREAK_VI))
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_VI &&
      ctx.has_left2 && salts_unicode_lb_is_aksara(&ctx.left2) &&
      salts_unicode_lb_is_ak_or_dotted(&ctx.right))
    return SALTS_UNICODE_LB_NO_BREAK;
  if (salts_unicode_lb_is_aksara(&ctx.left) &&
      salts_unicode_lb_is_aksara(&ctx.right) &&
      ctx.has_right2 &&
      ctx.right2.cls == SALTS_UNICODE_LINE_BREAK_VF)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB29. */
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_IS &&
      salts_unicode_lb_is_alpha(ctx.right.cls))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB30. */
  if ((salts_unicode_lb_is_alpha(ctx.left.cls) ||
       ctx.left.cls == SALTS_UNICODE_LINE_BREAK_NU) &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_OP &&
      !ctx.right.east_asian)
    return SALTS_UNICODE_LB_NO_BREAK;
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_CP &&
      !ctx.left.east_asian &&
      (salts_unicode_lb_is_alpha(ctx.right.cls) ||
       ctx.right.cls == SALTS_UNICODE_LINE_BREAK_NU))
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB30a / LB30b. */
  if (ctx.left.cls == SALTS_UNICODE_LINE_BREAK_RI &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_RI &&
      (ctx.trailing_ri & 1u) != 0u)
    return SALTS_UNICODE_LB_NO_BREAK;
  if ((ctx.left.cls == SALTS_UNICODE_LINE_BREAK_EB ||
       ctx.left.potential_emoji) &&
      ctx.right.cls == SALTS_UNICODE_LINE_BREAK_EM)
    return SALTS_UNICODE_LB_NO_BREAK;

  /* LB31. */
  return SALTS_UNICODE_LB_ALLOW_BREAK;
}

salts_unicode_status salts_unicode_line_break_class(
    uint32_t scalar, salts_unicode_line_break *out_class) {
  if (out_class == NULL || scalar > 0x10ffffu ||
      (scalar >= 0xd800u && scalar <= 0xdfffu))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  *out_class = salts_unicode_lb_original(scalar);
  return SALTS_UNICODE_OK;
}

salts_unicode_status salts_unicode_line_break_next(
    vstr input, size_t *cursor, size_t *break_offset,
    salts_unicode_line_break_opportunity *opportunity) {
  salts_unicode_status status;
  size_t scan;

  if (cursor == NULL || break_offset == NULL || opportunity == NULL)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  scan = *cursor;
  status = salts_unicode_lb_validate(input, scan);
  if (status != SALTS_UNICODE_OK) return status;
  if (scan == input.len) return SALTS_UNICODE_END;

  /* Advance at least one scalar so an already-returned boundary is not
   * reported twice. The full input was validated above. */
  {
    salts_unicode_scalar scalar;
    status = salts_unicode_utf8_next(input, &scan, &scalar);
    if (status != SALTS_UNICODE_OK) return status;
  }

  for (;;) {
    salts_unicode_status decision_status = SALTS_UNICODE_OK;
    const salts_unicode_lb_decision decision =
        salts_unicode_lb_decide(input, scan, &decision_status);
    if (decision_status != SALTS_UNICODE_OK) return decision_status;
    if (decision != SALTS_UNICODE_LB_NO_BREAK) {
      *cursor = scan;
      *break_offset = scan;
      *opportunity =
          decision == SALTS_UNICODE_LB_MUST_BREAK
              ? SALTS_UNICODE_LINE_BREAK_MANDATORY
              : SALTS_UNICODE_LINE_BREAK_ALLOWED;
      return SALTS_UNICODE_OK;
    }

    if (scan == input.len) return SALTS_UNICODE_END;
    {
      salts_unicode_scalar scalar;
      status = salts_unicode_utf8_next(input, &scan, &scalar);
      if (status != SALTS_UNICODE_OK) return status;
    }
  }
}
