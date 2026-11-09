// re2c --lang c
#include "uri_parser.h"
#include "uri_parser_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

int uri_parse_internal(const char *url_str, uri_t *uri)
{
    const char *marker;
    const char *src = url_str;
    size_t pos = 0u;
    /*!re2c
      re2c:define:YYCTYPE = "unsigned char";
      re2c:define:YYCURSOR = url_str;
      re2c:define:YYMARKER = marker;
      re2c:yyfill:enable = 0;

      EOF = "\x00";
      ALPHA = [a-zA-Z];
      DIGIT = [0-9];
      HEXDIG = [0-9a-fA-F];

      SUB_DELIMS = [!$&'()*+,;=] ;
      GEN_DELIMS = [:/?#\[\]@]   ;

      RESERVED = GEN_DELIMS | SUB_DELIMS ;
      UNRESERVED = ALPHA | DIGIT | [-._~] ;

      PCT_ENCODED = "%" HEXDIG HEXDIG ;

      PCHAR = UNRESERVED | PCT_ENCODED | SUB_DELIMS | ":" | "@" ;

      SEGMENT_NZ_NC = ( UNRESERVED | PCT_ENCODED | SUB_DELIMS | "@")+;
      SEGMENT_NZ = PCHAR+;
      SEGMENT = PCHAR*;

      USERINFO = UNRESERVED | PCT_ENCODED | SUB_DELIMS | ":";

      DEC_OCT = DIGIT | [1-9] DIGIT | "1" DIGIT{2} | "2" [0-4] DIGIT | "25"
      [0-5] ;

      IPV4ADDR = DEC_OCT "." DEC_OCT "." DEC_OCT "." DEC_OCT;

      H16 = HEXDIG{1,4};

      REGNAME = UNRESERVED | PCT_ENCODED | SUB_DELIMS ;

      // scheme
      * { return 0; }

      ALPHA (ALPHA | DIGIT | [+.-])* ":" {
        size_t len = (size_t)(url_str - src - 1);
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->scheme, sizeof(uri->scheme)))
          return 0;
        pos += len + 1; // skip ":"
        goto hier_part;
      }
    */

hier_part:
    /*!re2c
      * { goto hier_part_path; }
      EOF { return 0;}
      "//"  { pos +=2; goto hier_part_authority; }
    */

hier_part_authority:
    /*!re2c
      EOF { return 0;}
      USERINFO* "@" {
        size_t len = (size_t)(url_str - src) - pos - 1u; // unshift 1 char for "@"
        uri->component_flags |= URI_COMPONENT_USERINFO;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->userinfo, sizeof(uri->userinfo)))
          return 0;
        pos += len + 1; // shift for "@" char
        goto hier_part_host;
      }
      [^] { url_str--; goto hier_part_host; }
    */

hier_part_host:
    /*!re2c
      EOF { return 0;}
      * { return 0; }
      "" / "/" {
        uri->host_type = URI_HOST_UNKNOWN;
        goto hier_part_path;
      }
      IPV4ADDR {
        size_t len = (size_t)(url_str - src) - pos;
        uri->host_type = URI_HOST_IPV4ADDR;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->host, sizeof(uri->host))) return 0;
        pos += len;
        goto hier_part_port;
      }
      "[v" [^\]\x00]+ "]" {
        pos++; // shift "["
        size_t len = (size_t)(url_str - src) - pos - 1u;
        uri->host_type = URI_HOST_IPVFUTURE;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->host, sizeof(uri->host))) return 0;
        pos += len + 1;
        goto hier_part_port;
      }
      "[" [^\]\x00]+ "]" {
        pos++; // shift "["
        size_t len = (size_t)(url_str - src) - pos - 1u; // skip "]"
        uri->host_type = URI_HOST_IPV6ADDR;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->host, sizeof(uri->host))) return 0;
        pos += len + 1;
        goto hier_part_port;
      }
      REGNAME+ {
        size_t len = (size_t)(url_str - src) - pos;
        uri->host_type = URI_HOST_REGNAME;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->host, sizeof(uri->host))) return 0;
        pos += len;
        goto hier_part_port;
      }
    */

hier_part_port:
    /*!re2c
      EOF {
        uri->valid = 1;
        return 1;
      }
      ":" { return 0; }
      "" { goto hier_part_path; }
      ":" DIGIT+ {
        pos++; // shift ":"
        size_t len = (size_t)(url_str - src) - pos;
        int port = 0;
        for (size_t index = 0u; index < len; ++index) {
          const int digit = src[pos + index] - '0';
          if (!(uri->overflow_flags & URI_OVERFLOW_PORT) && port > (INT_MAX - digit) / 10) {
            uri->overflow_flags |= URI_OVERFLOW_PORT;
            port = INT_MAX;
          } else if (!(uri->overflow_flags & URI_OVERFLOW_PORT)) {
            port = port * 10 + digit;
          }
        }
        uri->port = port; // Upper layers validate their protocol range.
        uri->component_flags |= URI_COMPONENT_PORT;

        pos += len;
        goto hier_part_path;
      }
    */

hier_part_path:
    /*!re2c
      EOF {
        uri->valid = 1;
        return 1;
      }
      * { url_str--; goto query_frag; }
      ("/" SEGMENT)+ {
        size_t len = (size_t)(url_str - src) - pos;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->path, sizeof(uri->path))) return 0;
        pos += len;
        goto query_frag;
      }
    */

query_frag:
    /*!re2c
      EOF {
        uri->valid = 1;
        return 1;
      }
      "?" (PCHAR | [/?])* {
        pos++; // shift "?"
        size_t len = (size_t)(url_str - src) - pos;
        uri->component_flags |= URI_COMPONENT_QUERY;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->query, sizeof(uri->query))) return 0;
        pos += len;
        goto query_frag;
      }
      "#" (PCHAR | [/?])* {
        pos++; // shift "#"
        size_t len = (size_t)(url_str - src) - pos;
        uri->component_flags |= URI_COMPONENT_FRAGMENT;
        if (!uri_copy_substring_checked(uri, src, pos, len, uri->fragment, sizeof(uri->fragment)))
          return 0;
        uri->valid = 1;
        return 1;
      }
      * { return 0; }
    */
}
