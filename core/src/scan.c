#include <salts/scan.h>
#include "scan_internal.h"
#include <string.h>

/* One public scan owner. SIMDe kernels and their scalar tails are private;
 * all functions borrow the input only for the duration of the call. */
const char *scan_char(const char *p,const char *end,char needle) {
  if (!p || !end || p>end) return NULL;
  while (p<end) {
    if (*p==needle) return p;
    ++p;
  }
  return NULL;
}
const char *scan_to_char(const char *p,const char *end,char needle) {
  const char *hit=scan_char(p,end,needle);
  return hit ? hit : end;
}
const char *scan_to_any2(const char *p,const char *end,char a,char b) {
  if (!p || !end || p>end) return end;
  while(p<end && *p!=a && *p!=b)++p;
  return p;
}
const char *scan_to_any3(const char *p,const char *end,char a,char b,char c) {
  if (!p || !end || p>end) return end;
  while(p<end && *p!=a && *p!=b && *p!=c)++p;
  return p;
}
const char *scan_skip_sp_tab(const char *p,const char *end) {
  return scan_simde_skip_sp_tab(p, end);
}
const char *scan_to_any4(const char *p, const char *end,
                        char a, char b, char c, char d) {
  return scan_simde_to_any4(p, end, a, b, c, d);
}
const char *scan_to_any5(const char *p, const char *end,
                        char a, char b, char c, char d, char e) {
  return scan_simde_to_any5(p, end, a, b, c, d, e);
}
const char *scan_skip_sp_tab_cr_lf(const char *p,const char *end) {
  if (!p || !end || p>end) return end;
  while(p<end && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n'))++p;
  return p;
}
const char *scan_mem(const char *haystack,size_t haystack_len,
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
