#include "cmeta_simd_scan.h"
#include <string.h>

/* Canonical dependency-free C11 scan implementation. Preserves the exact
 * byte-range/search semantics of the original utils SIMD scan API; measured
 * SIMD performance parity remains a separate acceptance requirement. */
const char *cmeta_scan_char(const char *p,const char *end,char needle) {
  if (!p || !end || p>end) return NULL;
  while (p<end) {
    if (*p==needle) return p;
    ++p;
  }
  return NULL;
}
const char *cmeta_scan_to_char(const char *p,const char *end,char needle) {
  const char *hit=cmeta_scan_char(p,end,needle);
  return hit ? hit : end;
}
const char *cmeta_scan_to_any2(const char *p,const char *end,char a,char b) {
  if (!p || !end || p>end) return end;
  while(p<end && *p!=a && *p!=b)++p;
  return p;
}
const char *cmeta_scan_to_any3(const char *p,const char *end,char a,char b,char c) {
  if (!p || !end || p>end) return end;
  while(p<end && *p!=a && *p!=b && *p!=c)++p;
  return p;
}
const char *cmeta_scan_skip_sp_tab(const char *p,const char *end) {
  if (!p || !end || p>end) return end;
  while(p<end && (*p==' ' || *p=='\t'))++p;
  return p;
}
const char *cmeta_scan_skip_sp_tab_cr_lf(const char *p,const char *end) {
  if (!p || !end || p>end) return end;
  while(p<end && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n'))++p;
  return p;
}
const char *cmeta_scan_mem(const char *haystack,size_t haystack_len,
                           const char *needle,size_t needle_len) {
  if (!haystack || !needle || needle_len==0u) return haystack;
  if (haystack_len<needle_len) return NULL;
  const size_t last=haystack_len-needle_len;
  for (size_t i=0;i<=last;++i)
    if(haystack[i]==needle[0] &&
       memcmp(haystack+i,needle,needle_len)==0)
      return haystack+i;
  return NULL;
}
