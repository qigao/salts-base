/*
 * Copyright © 2021 Jeremiah Ikosin
 * Distributed under the terms of the MIT license.
 */

#ifndef CXML_CXXPEVAL_H
#define CXML_CXXPEVAL_H

#include "core/cxgrptable.h"
#include "xml/cxprinter.h"
#include "cxxpresolver.h"
#include "cxxplib.h"
#include "query_vm.h"

/**Debug**/
void cxml_xp_debug_expr();

/** public api **/
cxml_set* cxml_xpath(void *root, const char *expr);
int cxml_xpath_ex(void *root, const char *expr, cxml_set **out,
                  const qvm_limits_t *limits,
                  qvm_diagnostic_t *diagnostic);
#endif
