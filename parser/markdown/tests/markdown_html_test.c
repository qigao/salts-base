#include <salts/markdown.h>
#include <salts/markdown_html.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test_) do { \
    if (!(test_)) { \
        fprintf(stderr,"%s:%d: failed: %s\n",__FILE__,__LINE__,#test_); \
        return 1; \
    } \
} while (0)

#define TICK "\x60"
/* Explicit UTF-8 test bytes; production Unicode classification comes only
 * from Salts::Unicode 17, never from this fixture vocabulary. */
#define NBSP "\xc2\xa0"
#define EM_SPACE "\xe2\x80\x83"
#define NNBSP "\xe2\x80\xaf"
#define ZWSP "\xe2\x80\x8b"
typedef struct {
    const char *markdown;
    const char *html;
} html_fixture;

static const html_fixture cases[] = {
    { "## Hello\n", "<h2>Hello</h2>\n" },
    { "Normal **Bold** text\n",
      "<p>Normal <strong>Bold</strong> text</p>\n" },
    { "A *bright* day.\n",
      "<p>A <em>bright</em> day.</p>\n" },
    { "> quote\n", "<blockquote>\n<p>quote</p>\n</blockquote>\n" },
    { "1. alpha\n2. beta\n",
      "<ol>\n<li>alpha</li>\n<li>beta</li>\n</ol>\n" },
    { "- one\n- two\n",
      "<ul>\n<li>one</li>\n<li>two</li>\n</ul>\n" },
    { TICK TICK TICK "c\nint v;\n" TICK TICK TICK "\n",
      "<pre><code class=\"language-c\">int v;\n</code></pre>\n" },
    { "- foo\n\n\t\tbar\n",
      "<ul>\n<li>\n<p>foo</p>\n<pre><code>  bar\n</code></pre>\n</li>\n</ul>\n" }, /* official CommonMark #5 */
    { ">\t\tfoo\n",
      "<blockquote>\n<pre><code>  foo\n</code></pre>\n</blockquote>\n" }, /* official CommonMark #6 */
    { "-\t\tfoo\n",
      "<ul>\n<li>\n<pre><code>  foo\n</code></pre>\n</li>\n</ul>\n" }, /* official CommonMark #7 */
    { "> \tfoo\n",
      "<blockquote>\n<p>foo</p>\n</blockquote>\n" }, /* tab at source col 2 is only two content columns */
    { "-\tfoo\n",
      "<ul>\n<li>foo</li>\n</ul>\n" }, /* single TAB is marker padding, not code */


    { " " TICK TICK TICK "\n aaa\naaa\n" TICK TICK TICK "\n",
      "<pre><code>aaa\naaa\n</code></pre>\n" }, /* CommonMark #131 one-space opening indent */
    { "  " TICK TICK TICK "\naaa\n  aaa\naaa\n  " TICK TICK TICK "\n",
      "<pre><code>aaa\naaa\naaa\n</code></pre>\n" }, /* CommonMark #132 two-space opening indent */
    { "   " TICK TICK TICK "\n   aaa\n    aaa\n  aaa\n   "
      TICK TICK TICK "\n",
      "<pre><code>aaa\n aaa\naaa\n</code></pre>\n" }, /* CommonMark #133 preserve excess code indentation */
    { "  ~~~\n abc\n    def\n  ~~~\n",
      "<pre><code>abc\n  def\n</code></pre>\n" }, /* preserve residual spaces after limited dedent */
    { "  ~~~\n\n x\n  ~~~\n",
      "<pre><code>\nx\n</code></pre>\n" }, /* blank and partially indented payload */
    { "  ~~~\n&copy;\n  &amp;\n  ~~~\n",
      "<pre><code>&amp;copy;\n&amp;amp;\n</code></pre>\n" }, /* code content remains literal until HTML5 TEXT escaping */
    { "A < B & C > D \"yes\"\n",
      "<p>A &lt; B &amp; C &gt; D &quot;yes&quot;</p>\n" }, /* HTML text context escapes once */
    { "[x](/a&b \"A &amp; B\")\n",
      "<p><a href=\"/a&amp;b\" title=\"A &amp; B\">x</a></p>\n" }, /* URI + HTML5 attribute escaping, no double entity decode */
    { "<https://foo.bar.`baz>`\n",
      "<p><a href=\"https://foo.bar.%60baz\">https://foo.bar.`baz</a>`</p>\n" }, /* official #346 */
    { "[link](</foo`bar>)\n",
      "<p><a href=\"/foo%60bar\">link</a></p>\n" }, /* single backtick in angle destination */
    { "[![moon](moon.jpg)](/uri)\n",
      "<p><a href=\"/uri\"><img src=\"moon.jpg\" alt=\"moon\" /></a></p>\n" }, /* official IMAGE #517 */
    { "![[[foo](uri1)](uri2)](uri3)\n",
      "<p><img src=\"uri3\" alt=\"[foo](uri2)\" /></p>\n" }, /* official IMAGE #520 */
    { "[![moon](moon.jpg)][ref]\n\n[ref]: /uri\n",
      "<p><a href=\"/uri\"><img src=\"moon.jpg\" alt=\"moon\" /></a></p>\n" }, /* official IMAGE #531 */
    { "![foo](/url \"title\")\n",
      "<p><img src=\"/url\" alt=\"foo\" title=\"title\" /></p>\n" }, /* official IMAGE #572 */
    { "![foo *bar*]\n\n[foo *bar*]: train.jpg \"train & tracks\"\n",
      "<p><img src=\"train.jpg\" alt=\"foo bar\" title=\"train &amp; tracks\" /></p>\n" }, /* official IMAGE #573 */
    { "![foo ![bar](/url)](/url2)\n",
      "<p><img src=\"/url2\" alt=\"foo bar\" /></p>\n" }, /* official IMAGE #574 */
    { "![foo [bar](/url)](/url2)\n",
      "<p><img src=\"/url2\" alt=\"foo bar\" /></p>\n" }, /* official IMAGE #575 */
    { "![](/url)\n",
      "<p><img src=\"/url\" alt=\"\" /></p>\n" }, /* official IMAGE #581 */
    { "![foo][]\n\n[foo]: /url \"title\"\n",
      "<p><img src=\"/url\" alt=\"foo\" title=\"title\" /></p>\n" }, /* official IMAGE #584 */
    { "![foo]\n\n[foo]: /url \"title\"\n",
      "<p><img src=\"/url\" alt=\"foo\" title=\"title\" /></p>\n" }, /* official IMAGE #588 */
    { "[foo]\n\n> [foo]: /url\n",
      "<p><a href=\"/url\">foo</a></p>\n<blockquote>\n</blockquote>\n" }, /* official #218 */
    { "- a\n- b\n\n  [ref]: /url\n- d\n",
      "<ul>\n<li>\n<p>a</p>\n</li>\n<li>\n<p>b</p>\n</li>\n<li>\n<p>d</p>\n</li>\n</ul>\n" }, /* official #317 */
    { "A [B](dest)\n", "<p>A <a href=\"dest\">B</a></p>\n" },
    { "[link [foo [bar]]](/uri)\n",
      "<p><a href=\"/uri\">link [foo [bar]]</a></p>\n" }, /* official #512 */
    { "[link [bar](/uri)\n",
      "<p>[link <a href=\"/uri\">bar</a></p>\n" }, /* official #514 */
    { "[foo [bar](/uri)](/uri)\n",
      "<p>[foo <a href=\"/uri\">bar</a>](/uri)</p>\n" }, /* official #518 */
    { "[foo *[bar [baz](/uri)](/uri)*](/uri)\n",
      "<p>[foo <em>[bar <a href=\"/uri\">baz</a>](/uri)</em>](/uri)</p>\n" }, /* official #519 */
    { "[link [foo [bar]]][ref]\n\n[ref]: /uri\n",
      "<p><a href=\"/uri\">link [foo [bar]]</a></p>\n" }, /* official #528 */
    { "[foo <bar attr=\"](baz)\">\n",
      "<p>[foo &lt;bar attr=&quot;](baz)&quot;&gt;</p>\n" }, /* #524 safe production output, NOT normative HTML */
    { "[foo <bar attr=\"][ref]\">\n\n[ref]: /uri\n",
      "<p>[foo &lt;bar attr=&quot;][ref]&quot;&gt;</p>\n" }, /* #536 safe production output */
    { "[foo <bar attr=\"[ref]\">](/outer)\n\n[ref]: /uri\n",
      "<p><a href=\"/outer\">foo &lt;bar attr=&quot;[ref]&quot;&gt;</a></p>\n" },
    { "[foo <bar attr=\"](baz)\n",
      "<p><a href=\"baz\">foo &lt;bar attr=&quot;</a></p>\n" },
    { "[foo <bar attr='](baz)'>\n",
      "<p>[foo &lt;bar attr='](baz)'&gt;</p>\n" },
    { "[\nfoo\n]: /url\nbar\n", "<p>bar</p>\n" }, /* official #208 */
    { "[\nfoo\n]: /url\n\n[x][foo]\n",
      "<p><a href=\"/url\">x</a></p>\n" },
    { "[\r\n FoO\r\n ]: /uri 'T'\r\n\r\n[x][foo]\r\n",
      "<p><a href=\"/uri\" title=\"T\">x</a></p>\n" },
    { "[\nfoo\n]:\n /url\n\n[x][foo]\n",
      "<p><a href=\"/url\">x</a></p>\n" },
    { "[\nfoo\n]: /first\n[foo]: /second\n\n[x][foo]\n",
      "<p><a href=\"/first\">x</a></p>\n" },
    { "[Foo\n  bar]: /url\n\n[Baz][Foo bar]\n",
      "<p><a href=\"/url\">Baz</a></p>\n" }, /* official #541 */
    { "  [FoO\r\n   Bar]: /uri 'ok'\r\n\r\n[Baz][foo bar]\r\n",
      "<p><a href=\"/uri\" title=\"ok\">Baz</a></p>\n" },
    { "[Foo\n bar]: /first\n[foo bar]: /second\n\n[x][Foo bar]\n",
      "<p><a href=\"/first\">x</a></p>\n" },
    { "[foo\\!\n BAR]: /x\n\n[x][Foo\\! Bar]\n",
      "<p><a href=\"/x\">x</a></p>\n" },
    { "[foo<https://example.com/?search=](uri)>\n",
      "<p>[foo<a href=\"https://example.com/?search=%5D(uri)\">https://example.com/?search=](uri)</a></p>\n" }, /* official #526 */
    { "[foo<https://example.com/?search=][ref]>\n\n[ref]: /uri\n",
      "<p>[foo<a href=\"https://example.com/?search=%5D%5Bref%5D\">https://example.com/?search=][ref]</a></p>\n" }, /* official #538 */
    { "[x <https://example.com/path>](/outer)\n",
      "<p>[x <a href=\"https://example.com/path\">https://example.com/path</a>](/outer)</p>\n" }, /* nested angle autolink blocks outer LINK */

    { "[foo [bar](/uri)][ref]\n\n[ref]: /uri\n",
      "<p>[foo <a href=\"/uri\">bar</a>]<a href=\"/uri\">ref</a></p>\n" }, /* official #532 */
    { "[foo *bar [baz][ref]*][ref]\n\n[ref]: /uri\n",
      "<p>[foo <em>bar <a href=\"/uri\">baz</a></em>]<a href=\"/uri\">ref</a></p>\n" }, /* official #533 */

    { "a " TICK "code" TICK " b\n",
      "<p>a <code>code</code> b</p>\n" },
    { "- Foo\n- * * *\n",
      "<ul>\n<li>Foo</li>\n<li>\n<hr />\n</li>\n</ul>\n" }, /* official CommonMark #61 */
    { "- Foo\n\n  * * *\n",
      "<ul>\n<li>\n<p>Foo</p>\n<hr />\n</li>\n</ul>\n" }, /* loose list item owns RULE after blank */
    { "- outer\n  - inner\n  * * *\n",
      "<ul>\n<li>outer\n<ul>\n<li>inner</li>\n</ul>\n<hr />\n</li>\n</ul>\n" }, /* nearest matching outer item owns RULE */
    { "- Foo\n  * * *\n- Bar\n",
      "<ul>\n<li>Foo\n<hr />\n</li>\n<li>Bar</li>\n</ul>\n" }, /* sibling after item rule */
    { "> - Foo\n>   * * *\n",
      "<blockquote>\n<ul>\n<li>Foo\n<hr />\n</li>\n</ul>\n</blockquote>\n" }, /* quote-owned list continuation rule */

    { "- * * *\n",
      "<ul>\n<li>\n<hr />\n</li>\n</ul>\n" }, /* standalone item-owned HR */
    { "1. * * *\n2. text\n",
      "<ol>\n<li>\n<hr />\n</li>\n<li>text</li>\n</ol>\n" }, /* ordered item-owned HR */
    { "> - * * *\n",
      "<blockquote>\n<ul>\n<li>\n<hr />\n</li>\n</ul>\n</blockquote>\n" }, /* quote-owned list item HR */
    { "- Foo\n  * * *\n",
      "<ul>\n<li>Foo\n<hr />\n</li>\n</ul>\n" }, /* same item subsequent rule */
    { "10. Foo\n    * * *\n",
      "<ol start=\"10\">\n<li>Foo\n<hr />\n</li>\n</ol>\n" }, /* item-relative >3 root indent */
    { "- Foo\n  ---\n",
      "<ul>\n<li>\n<h2>Foo</h2>\n</li>\n</ul>\n" }, /* Setext before thematic rule */
    { "- Foo\n* * *\n",
      "<ul>\n<li>Foo</li>\n</ul>\n<hr />\n" }, /* dedented root HR closes list */
    { "- ---\n", "<hr />\n" }, /* entire line thematic rule beats list opener */
    { "- *a*\n", "<ul>\n<li><em>a</em></li>\n</ul>\n" }, /* inline emphasis, not rule */
    { "---\n", "<hr />\n" },
    { "first\n===\n", "<h1>first</h1>\n" },
    { "first\nsecond\n", "<p>first\nsecond</p>\n" },
    { "a \\*b\n", "<p>a *b</p>\n" },
    { "中😀 👩‍💻\n", "<p>中😀 👩‍💻</p>\n" },
    { "a ~~red~~ day\n", "<p>a <del>red</del> day</p>\n" },
    { "| A | B |\n| --- | --- |\n| one | two |\n",
      "<table>\n<thead>\n<tr>\n<th>A</th>\n<th>B</th>\n</tr>\n"
      "</thead>\n<tbody>\n<tr>\n<td>one</td>\n<td>two</td>\n</tr>\n"
      "</tbody>\n</table>\n" },
    /* #186: one document-owned column alignment vector; both header and
     * uneven body rows consume it, including escaped pipes/code spans. */
    { "| Left | Center | Right |\n| :--- | :---: | ---: |\n"
      "| a\\|b | " TICK "x\\|y" TICK " | z |\n",
      "<table>\n<thead>\n<tr>\n"
      "<th align=\"left\">Left</th>\n"
      "<th align=\"center\">Center</th>\n"
      "<th align=\"right\">Right</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n"
      "<td align=\"left\">a|b</td>\n"
      "<td align=\"center\"><code>x|y</code></td>\n"
      "<td align=\"right\">z</td>\n"
      "</tr>\n</tbody>\n</table>\n" },

    /* GFM 4.10 Example 200: escaped pipes remain literal even inside
     * code/strong spans; the TABLE parser removes only that pipe escape. */
    { "| f\\|oo |\n| ------ |\n"
      "| b " TICK "\\|" TICK " az |\n"
      "| b **\\|** im |\n",
      "<table>\n<thead>\n<tr>\n<th>f|oo</th>\n</tr>\n"
      "</thead>\n<tbody>\n<tr>\n"
      "<td>b <code>|</code> az</td>\n</tr>\n<tr>\n"
      "<td>b <strong>|</strong> im</td>\n</tr>\n"
      "</tbody>\n</table>\n" },
    /* Raw unescaped pipes split columns even inside source backtick runs. */
    { "| A | B |\n| --- | --- |\n| " TICK "x|y" TICK " | z |\n",
      "<table>\n<thead>\n<tr>\n<th>A</th>\n<th>B</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n"
      "<td>" TICK "x</td>\n<td>y" TICK "</td>\n"
      "</tr>\n</tbody>\n</table>\n" },
    { "| A | B | C |\n| :--- | :---: | ---: |\n| one |\n",
      "<table>\n<thead>\n<tr>\n"
      "<th align=\"left\">A</th>\n"
      "<th align=\"center\">B</th>\n"
      "<th align=\"right\">C</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n"
      "<td align=\"left\">one</td>\n"
      "<td align=\"center\"></td>\n"
      "<td align=\"right\"></td>\n"
      "</tr>\n</tbody>\n</table>\n" },
    /* #186 regression: a two-backslash run still shields the final pipe
     * at TABLE admission; normalized owned inline text drops the escape. */
    { "| Name | Value |\n| --- | --- |\n| a\\\\|b | tail |\n",
      "<table>\n<thead>\n<tr>\n<th>Name</th>\n<th>Value</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n"
      "<td>a|b</td>\n<td>tail</td>\n"
      "</tr>\n</tbody>\n</table>\n" },
    /* A bare task bracket sequence with NO trailing whitespace is ordinary
     * LIST_ITEM text, not a CHECKBOX; checked and unchecked both apply. */
    { "- [ ]\n- [x]\n",
      "<ul>\n<li>[ ]</li>\n<li>[x]</li>\n</ul>\n" },
    /* Loose-list paragraphs need the literal task-marker separator before
     * the newline, even though the checkbox itself is an inert element. */
    { "- [x] shipped\n\n- [ ] pending\n",
      "<ul>\n<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> \n"
      "<p>shipped</p>\n</li>\n"
      "<li><input type=\"checkbox\" disabled=\"\" /> \n"
      "<p>pending</p>\n</li>\n</ul>\n" },
    /* #186: only typed CHECKBOX nodes may emit inert fixed markup. */
    { "- [x] shipped\n- [ ] pending\n",
      "<ul>\n<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> shipped</li>\n"
      "<li><input type=\"checkbox\" disabled=\"\" /> pending</li>\n</ul>\n" },
    { "3. [X] upper\n4. [ ] lower\n",
      "<ol start=\"3\">\n"
      "<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> upper</li>\n"
      "<li><input type=\"checkbox\" disabled=\"\" /> lower</li>\n</ol>\n" },
    { "- [x]done\n", "<ul>\n<li>[x]done</li>\n</ul>\n" },

    /* #186: a marker-line TABLE is owned by the surrounding LIST_ITEM,
     * not by DOCUMENT or the list itself. This is a tight list. */
    { "- | Name | Qty |\n  | :--- | ---: |\n  | apple | 2 |\n",
      "<ul>\n<li>\n<table>\n<thead>\n<tr>\n"
      "<th align=\"left\">Name</th>\n"
      "<th align=\"right\">Qty</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td align=\"left\">apple</td>\n"
      "<td align=\"right\">2</td>\n</tr>\n</tbody>\n"
      "</table>\n</li>\n</ul>\n" },
    /* The same canonical TABLE admission is reachable in the active
     * QUOTE-owned list frame, with exact quote depth on all table rows. */
    { "> - | A | B |\n>   | --- | --- |\n>   | x | y |\n",
      "<blockquote>\n<ul>\n<li>\n<table>\n"
      "<thead>\n<tr>\n<th>A</th>\n<th>B</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n"
      "<td>x</td>\n<td>y</td>\n</tr>\n</tbody>\n"
      "</table>\n</li>\n</ul>\n</blockquote>\n" },
    /* GFM §5.3 still identifies the first paragraph's TASK marker in a
     * loose QUOTE-owned list. cmark-gfm 2025.10.22 leaves this marker literal;
     * the independent byte oracle reports the divergence separately. */
    { "> - [x] parent\n>\n>   | A | B |\n"
      ">   | --- | --- |\n>   | a | b |\n",
      "<blockquote>\n<ul>\n"
      "<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> \n"
      "<p>parent</p>\n<table>\n<thead>\n<tr>\n"
      "<th>A</th>\n<th>B</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>a</td>\n<td>b</td>\n"
      "</tr>\n</tbody>\n</table>\n</li>\n</ul>\n"
      "</blockquote>\n" },
    /* GFM §5.3 also recognizes a quoted loose ORDERED task at start 4,
     * despite cmark-gfm 2025.10.22 preserving literal '[x]' here.
     * Its independent reference output is reported as a known difference;
     * the normative fixed-disabled checkbox HTML stays a strict Golden. */
    { "> 4. [x] parent\n>\n>    | A | B |\n"
      ">    | --- | --- |\n>    | x | y |\n",
      "<blockquote>\n<ol start=\"4\">\n"
      "<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> \n"
      "<p>parent</p>\n<table>\n<thead>\n<tr>\n"
      "<th>A</th>\n<th>B</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>x</td>\n<td>y</td>\n"
      "</tr>\n</tbody>\n</table>\n</li>\n</ol>\n"
      "</blockquote>\n" },
    /* #186: a preceding tight LIST_ITEM paragraph remains a sibling
     * before the GFM TABLE, even without a blank separator. */
    { "- intro\n  | A | B |\n  | --- | --- |\n  | x | y |\n",
      "<ul>\n<li>intro\n<table>\n<thead>\n<tr>\n"
      "<th>A</th>\n<th>B</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>x</td>\n<td>y</td>\n"
      "</tr>\n</tbody>\n</table>\n</li>\n</ul>\n" },
    /* A valid list opener wins before a root TABLE header even when the
     * marker line itself contains an unescaped pipe. */
    { "- left | right\n  --- | ---\n  x | y\n",
      "<ul>\n<li>\n<table>\n<thead>\n<tr>\n"
      "<th>left</th>\n<th>right</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>x</td>\n<td>y</td>\n"
      "</tr>\n</tbody>\n</table>\n</li>\n</ul>\n" },
    /* GFM tables terminate before raw HTML blocks. The SAFE writer must
     * still escape raw markup; SPEC is a separately opt-in test policy. */
    { "| H | V |\n| --- | --- |\n| a | b |\n<div>\nraw\n</div>\n",
      "<table>\n<thead>\n<tr>\n<th>H</th>\n<th>V</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n"
      "<td>a</td>\n<td>b</td>\n"
      "</tr>\n</tbody>\n</table>\n"
      "&lt;div&gt;\nraw\n&lt;/div&gt;\n" },
    { "> | H | V |\n> | --- | --- |\n> | a | b |\n"
      "> <div>\n> raw\n> </div>\n",
      "<blockquote>\n<table>\n<thead>\n<tr>\n"
      "<th>H</th>\n<th>V</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>a</td>\n<td>b</td>\n"
      "</tr>\n</tbody>\n</table>\n"
      "&lt;div&gt;\nraw\n&lt;/div&gt;\n</blockquote>\n" },
    /* #186: exact-depth quote tables share root GFM alignment and escaped
     * pipe semantics while preserving their genuine QUOTE owner. */
    { "> | H | N |\n> | :--- | ---: |\n> | a\\|b | **bold** |\n",
      "<blockquote>\n<table>\n<thead>\n<tr>\n"
      "<th align=\"left\">H</th>\n<th align=\"right\">N</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n"
      "<td align=\"left\">a|b</td>\n"
      "<td align=\"right\"><strong>bold</strong></td>\n"
      "</tr>\n</tbody>\n</table>\n</blockquote>\n" },
    { "> > | A | B |\n> > | --- | --- |\n> > | x |\n",
      "<blockquote>\n<blockquote>\n<table>\n<thead>\n<tr>\n"
      "<th>A</th>\n<th>B</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>x</td>\n<td></td>\n"
      "</tr>\n</tbody>\n</table>\n</blockquote>\n</blockquote>\n" },
    { "> | Key | Value |\n> | --- | --- |\n> | a | b |\n>\n> After\n",
      "<blockquote>\n<table>\n<thead>\n<tr>\n"
      "<th>Key</th>\n<th>Value</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>a</td>\n<td>b</td>\n"
      "</tr>\n</tbody>\n</table>\n<p>After</p>\n</blockquote>\n" },
    /* CommonMark 0.31.2 #329–#331, #338–#340, #347; code-span
     * contents are literal UTF-8, with exact-run and edge-space rules. */
    { TICK TICK " foo " TICK " bar " TICK TICK "\n",
      "<p><code>foo " TICK " bar</code></p>\n" },          /* #329 */
    { TICK " " TICK TICK " " TICK "\n",
      "<p><code>" TICK TICK "</code></p>\n" },             /* #330 */
    { TICK "  " TICK TICK "  " TICK "\n",
      "<p><code> " TICK TICK " </code></p>\n" },           /* #331 */
    { TICK "\xc2\xa0" "b" "\xc2\xa0" TICK "\n",
      "<p><code>\xc2\xa0" "b" "\xc2\xa0</code></p>\n" },    /* #333 */
    { TICK "  " TICK "\n",
      "<p><code>  </code></p>\n" },                        /* #334 */
    { TICK "foo\\" TICK "bar" TICK "\n",
      "<p><code>foo\\</code>bar" TICK "</p>\n" },          /* #338 */
    { TICK TICK "foo" TICK "bar" TICK TICK "\n",
      "<p><code>foo" TICK "bar</code></p>\n" },             /* #339 */
    { TICK " foo " TICK TICK " bar " TICK "\n",
      "<p><code>foo " TICK TICK " bar</code></p>\n" },     /* #340 */
    { TICK TICK TICK "foo" TICK TICK "\n",
      "<p>" TICK TICK TICK "foo" TICK TICK "</p>\n" },    /* #347 */
    /* #182: a code span may cross root-paragraph physical lines; its
     * internal line endings become spaces, preserving all other bytes. */
    { TICK TICK "\nfoo\nbar  \nbaz\n" TICK TICK "\n",
      "<p><code>foo bar   baz</code></p>\n" },           /* #335 */
    { TICK TICK "\nfoo \n" TICK TICK "\n",
      "<p><code>foo </code></p>\n" },                    /* #336 */
    { TICK "foo   bar \nbaz" TICK "\n",
      "<p><code>foo   bar  baz</code></p>\n" },          /* #337 */
    { TICK TICK "\r\nfoo\r\nbar  \r\nbaz\r\n" TICK TICK "\r\n",
      "<p><code>foo bar   baz</code></p>\n" },
    { TICK "line\nnext" TICK " tail\n",
      "<p><code>line next</code> tail</p>\n" },
    { TICK "foo\nbar" TICK "\nafter\n",
      "<p><code>foo bar</code>\nafter</p>\n" },
    { TICK "foo\n\nbar" TICK "\n",
      "<p>" TICK "foo</p>\n<p>bar" TICK "</p>\n" },
    { TICK "foo\n# Heading\nbar" TICK "\n",
      "<p>" TICK "foo</p>\n<h1>Heading</h1>\n<p>bar" TICK "</p>\n" },
    { TICK TICK "a\n" TICK "b" TICK TICK "\n",
      "<p><code>a " TICK "b</code></p>\n" },
    { TICK " \n " TICK "\n",
      "<p><code>   </code></p>\n" },
    /* #182: emphasis/link closers inside a valid code span are opaque.
     * An unclosed backtick run remains literal and cannot hide a closer. */
    { "*foo" TICK "*" TICK "\n",
      "<p>*foo<code>*</code></p>\n" },                     /* #341 */
    { "[not a " TICK "link](/foo" TICK ")\n",
      "<p>[not a <code>link](/foo</code>)</p>\n" },        /* #342 */
    { "*hello " TICK "a*b" TICK " world*\n",
      "<p><em>hello <code>a*b</code> world</em></p>\n" },
    { "[hello " TICK "x]y" TICK "](url)\n",
      "<p><a href=\"url\">hello <code>x]y</code></a></p>\n" },
    { "*foo" TICK TICK "bar*\n",
      "<p><em>foo" TICK TICK "bar</em></p>\n" },
    { "**hi " TICK "x*y" TICK " done**\n",
      "<p><strong>hi <code>x*y</code> done</strong></p>\n" },
    /* ASCII CommonMark flanking: adjacent punctuation cannot open from a
     * word; underscores inside words cannot become emphasis delimiters. */
    { "a*\"foo\"*\n", "<p>a*&quot;foo&quot;*</p>\n" },         /* #352 */
    { "a_\"foo\"_\n", "<p>a_&quot;foo&quot;_</p>\n" },         /* #359 */
    { "foo_bar_\n", "<p>foo_bar_</p>\n" },                /* #360 */
    { "5_6_78\n", "<p>5_6_78</p>\n" },                   /* #361 */
    { "пристаням_стремятся_\n",
      "<p>пристаням_стремятся_</p>\n" },                 /* #362 */
    { "aa_\"bb\"_cc\n", "<p>aa_&quot;bb&quot;_cc</p>\n" },  /* #363 */
    { "*(*foo)\n", "<p>*(*foo)</p>\n" },                  /* #368 */
    { "_(_foo)\n", "<p>_(_foo)</p>\n" },                  /* #372 */
    { "_foo_bar\n", "<p>_foo_bar</p>\n" },                /* #374 */
    { "_пристаням_стремятся\n",
      "<p>_пристаням_стремятся</p>\n" },                 /* #375 */
    { "_foo_bar_baz_\n",
      "<p><em>foo_bar_baz</em></p>\n" },                 /* #376 */
    { "a**\"foo\"**\n",
      "<p>a**&quot;foo&quot;**</p>\n" },                         /* #380 */
    /* A later delimiter immediately following whitespace may be a nearer
     * opener; it must retain ownership of the final matching closer. */
    { "**foo **bar baz**\n",
      "<p>**foo <strong>bar baz</strong></p>\n" },       /* #471 */
    { "*foo *bar baz*\n",
      "<p>*foo <em>bar baz</em></p>\n" },                /* #472 */
    /* Same-length nested flanking runs claim their own nearer closers.
     * The outer delimiter remains valid only when another closer exists. */
    { "*(*foo*)*\n",
      "<p><em>(<em>foo</em>)</em></p>\n" },           /* #369 */
    { "_(_foo_)_\n",
      "<p><em>(<em>foo</em>)</em></p>\n" },           /* #373 */
    { "__foo, __bar__, baz__\n",
      "<p><strong>foo, <strong>bar</strong>, baz</strong></p>\n" }, /* #389 */
    { "*emph *with emph* in it*\n",
      "<p><em>emph <em>with emph</em> in it</em></p>\n" },
    { "**strong **with strong** in it**\n",
      "<p><strong>strong <strong>with strong</strong> in it</strong></p>\n" },
    { "*x (*y*) z*\n",
      "<p><em>x (<em>y</em>) z</em></p>\n" },
    { "**x (**y**) z**\n",
      "<p><strong>x (<strong>y</strong>) z</strong></p>\n" },
    /* Unicode 17 White_Space is resolved through the published provider.
     * #353 uses U+00A0; U+2003 and U+202F are extra positive controls,
     * whereas U+200B is NOT Unicode White_Space. */
    { "*" NBSP "a" NBSP "*\n",
      "<p>*" NBSP "a" NBSP "*</p>\n" },              /* #353 */
    { "a*" NBSP "b*\n",
      "<p>a*" NBSP "b*</p>\n" },
    { "*x" NBSP "*\n",
      "<p>*x" NBSP "*</p>\n" },
    { "*foo" NBSP "bar*\n",
      "<p><em>foo" NBSP "bar</em></p>\n" },
    { "**" NBSP "foo**\n",
      "<p>**" NBSP "foo**</p>\n" },
    { "_" NBSP "foo_\n",
      "<p>_" NBSP "foo_</p>\n" },
    { "*" EM_SPACE "a" EM_SPACE "*\n",
      "<p>*" EM_SPACE "a" EM_SPACE "*</p>\n" },
    { "*" NNBSP "a" NNBSP "*\n",
      "<p>*" NNBSP "a" NNBSP "*</p>\n" },
    { "*" ZWSP "a*\n",
      "<p><em>" ZWSP "a</em></p>\n" },
    { "*中*\n", "<p><em>中</em></p>\n" },
    /* Longer terminal delimiter runs can close a shorter outer span with
     * unused literal suffix, or exactly close inner+outer same-length spans.
     * Nonterminal and triple/mixed-length rules are NOT generalized here. */
    { "*foo**\n", "<p><em>foo</em>*</p>\n" },              /* #443 */
    { "**foo***\n", "<p><strong>foo</strong>*</p>\n" },    /* #446 */
    { "*foo****\n", "<p><em>foo</em>***</p>\n" },           /* #447 */
    { "_foo__\n", "<p><em>foo</em>_</p>\n" },               /* #455 */
    { "__foo___\n", "<p><strong>foo</strong>_</p>\n" },    /* #458 */
    { "_foo____\n", "<p><em>foo</em>___</p>\n" },           /* #459 */
    { "*foo *bar**\n", "<p><em>foo <em>bar</em></em></p>\n" }, /* #409 */
    { "**foo **bar****\n",
      "<p><strong>foo <strong>bar</strong></strong></p>\n" }, /* #427 */
    /* #182: maximal 1/2/3+ runs, nested strong/emphasis, and rule-of-three.
     * Reference bytes cross-checked against CommonMark 0.31.2 semantics.
     * These are independent strict HTML fixtures, not the full 652 gate. */
    { "***foo***\n", "<p><em><strong>foo</strong></em></p>\n" },
    { "___foo___\n", "<p><em><strong>foo</strong></em></p>\n" },
    { "***foo**\n", "<p>*<strong>foo</strong></p>\n" },
    { "***foo* bar**\n",
      "<p><strong><em>foo</em> bar</strong></p>\n" },
    { "*foo**bar*\n", "<p><em>foo**bar</em></p>\n" },
    { "**foo*, bar**\n",
      "<p><em><em>foo</em>, bar</em>*</p>\n" },
    { "*foo **bar***\n",
      "<p><em>foo <strong>bar</strong></em></p>\n" },
    { "***foo**bar*\n",
      "<p><em><strong>foo</strong>bar</em></p>\n" },
    { "*foo**bar**baz*\n",
      "<p><em>foo<strong>bar</strong>baz</em></p>\n" },
    { "a***b***c\n",
      "<p>a<em><strong>b</strong></em>c</p>\n" },
    { "***foo " TICK "a*b" TICK " bar***\n",
      "<p><em><strong>foo <code>a*b</code> bar</strong></em></p>\n" },
    /* Official CommonMark 0.31.2 exact expected HTML (previously failing):
     * #408/#413/#415–417/#431/#468/#470. Mixed units must neither
     * steal a closer from an inner run nor violate the odd-match rule. */
    { "__foo_ bar_\n",
      "<p><em><em>foo</em> bar</em></p>\n" },             /* #408 */
    { "***foo** bar*\n",
      "<p><em><strong>foo</strong> bar</em></p>\n" },      /* #413 */
    { "*foo**bar***\n",
      "<p><em>foo<strong>bar</strong></em></p>\n" },       /* #415 */
    { "foo***bar***baz\n",
      "<p>foo<em><strong>bar</strong></em>baz</p>\n" },    /* #416 */
    { "foo******bar*********baz\n",
      "<p>foo<strong><strong><strong>bar</strong></strong></strong>***baz</p>\n" }, /* #417 */
    { "**foo *bar***\n",
      "<p><strong>foo <em>bar</em></strong></p>\n" },      /* #431 */
    { "_____foo_____\n",
      "<p><em><strong><strong>foo</strong></strong></em></p>\n" }, /* #468 */
    { "*foo __bar *baz bim__ bam*\n",
      "<p><em>foo <strong>bar *baz bim</strong> bam</em></p>\n" }, /* #470 */
    /* Official #473/#474/#521: link labels cannot donate markers to
     * outside emphasis runs. */
    { "*[bar*](/url)\n",
      "<p>*<a href=\"/url\">bar*</a></p>\n" }, /* #473 */
    { "_foo [bar_](/url)\n",
      "<p>_foo <a href=\"/url\">bar_</a></p>\n" }, /* #474 */
    { "*[foo*](/uri)\n",
      "<p>*<a href=\"/uri\">foo*</a></p>\n" }, /* #521 */
    /* #182 root paragraph-scoped inline ownership: CommonMark 0.31.2
     * #394/#405/#423/#432 must preserve typed SOFT breaks *inside* their
     * nested emphasis/strong owners, never as paragraph-level siblings. */
    { "**Gomphocarpus (*Gomphocarpus physocarpus*, syn.\n"
      "*Asclepias physocarpa*)**\n",
      "<p><strong>Gomphocarpus (<em>Gomphocarpus physocarpus</em>, syn.\n"
      "<em>Asclepias physocarpa</em>)</strong></p>\n" }, /* #394 */
    { "*foo\nbar*\n",
      "<p><em>foo\nbar</em></p>\n" },                     /* #405 */
    { "**foo\nbar**\n",
      "<p><strong>foo\nbar</strong></p>\n" },             /* #423 */
    { "**foo *bar **baz**\nbim* bop**\n",
      "<p><strong>foo <em>bar <strong>baz</strong>\n"
      "bim</em> bop</strong></p>\n" },                   /* #432 */
    /* CRLF normalizes in the semantic inline source but stays unchanged in
     * the separately owned original Markdown byte buffer. */
    { "*foo\r\nbar*\r\n",
      "<p><em>foo\nbar</em></p>\n" },
    /* A blank line, heading or quote interrupts the paragraph: no marker
     * may match across these real block boundaries. */
    { "*foo\n\nbar*\n",
      "<p>*foo</p>\n<p>bar*</p>\n" },
    { "*foo\n# head\nbar*\n",
      "<p>*foo</p>\n<h1>head</h1>\n<p>bar*</p>\n" },
    { "*foo\n> quote*\n",
      "<p>*foo</p>\n<blockquote>\n<p>quote*</p>\n</blockquote>\n" },
    /* #81: paragraph spanning two lines can subsequently become an
     * inherited Setext heading without losing inner emphasis ownership. */
    { "Foo *bar\nbaz*\n====\n",
      "<h1>Foo <em>bar\nbaz</em></h1>\n" }, /* #81 */
    /* #185: real CommonMark hard/soft physical break origin. Only root
     * paragraph boundaries are admitted here; nested/list/quote still use
     * their previously qualified semantics until scoped later. */
    { "foo  \nbaz\n", "<p>foo<br />\nbaz</p>\n" },          /* #633 */
    { "foo\\\nbaz\n", "<p>foo<br />\nbaz</p>\n" },         /* #634 */
    { "foo       \nbaz\n", "<p>foo<br />\nbaz</p>\n" },     /* #635 */
    { "foo  \n     bar\n", "<p>foo<br />\nbar</p>\n" },    /* #636 */
    { "foo\\\n     bar\n", "<p>foo<br />\nbar</p>\n" },   /* #637 */
    { "foo  \n", "<p>foo</p>\n" },                          /* #645 */
    { "foo \n baz\n", "<p>foo\nbaz</p>\n" },                /* #649 */
    { "foo\\\\\nbaz\n", "<p>foo\\\nbaz</p>\n" },
    { "foo\\\n", "<p>foo\\</p>\n" },
    { "foo \nbaz\n", "<p>foo\nbaz</p>\n" },
    /* #185 stacked cross-line lifetime: physical HARD source must remain
     * inside the real inline owner; final Setext trailing tab is not text. */
    { "  Foo *bar\nbaz*\t\n====\n",
      "<h1>Foo <em>bar\nbaz</em></h1>\n" },                 /* #82 */
    { "*foo  \nbar*\n",
      "<p><em>foo<br />\nbar</em></p>\n" },                /* #638 */
    { "*foo\\\nbar*\n",
      "<p><em>foo<br />\nbar</em></p>\n" },                /* #639 */
    { "**foo  \nbar**\n",
      "<p><strong>foo<br />\nbar</strong></p>\n" },
    { "__foo\\\nbar__\n",
      "<p><strong>foo<br />\nbar</strong></p>\n" },
    { "*foo \nbar*\n",
      "<p><em>foo\nbar</em></p>\n" },
    { "*foo\\\r\nbar*\r\n",
      "<p><em>foo<br />\nbar</em></p>\n" },
    /* #185: a nested inline token can finish BEFORE trailing hard-break
     * spaces, while the outer emphasis/strong still owns the break. */
    { "*foo **bar**  \nbaz*\n",
      "<p><em>foo <strong>bar</strong><br />\nbaz</em></p>\n" },
    { "*foo [bar](url)  \nbaz*\n",
      "<p><em>foo <a href=\"url\">bar</a><br />\nbaz</em></p>\n" },
    { "**foo _bar_  \nbaz**\n",
      "<p><strong>foo <em>bar</em><br />\nbaz</strong></p>\n" },
    { "*foo **bar**\\\nbaz*\n",
      "<p><em>foo <strong>bar</strong><br />\nbaz</em></p>\n" },
    { "&nbsp; &amp; &copy; &AElig; &Dcaron;\n&frac34; &HilbertSpace; &DifferentialD;\n&ClockwiseContourIntegral; &ngE;\n", "<p>  &amp; © Æ Ď\n¾ ℋ ⅆ\n∲ ≧̸</p>\n" }, /* CommonMark #25 */
    { "&#35; &#1234; &#992; &#0;\n", "<p># Ӓ Ϡ �</p>\n" }, /* CommonMark #26 */
    { "&#X22; &#XD06; &#xcab;\n", "<p>&quot; ആ ಫ</p>\n" }, /* CommonMark #27 */
    { "&nbsp &x; &#; &#x;\n&#87654321;\n&#abcdef0;\n&ThisIsNotDefined; &hi?;\n", "<p>&amp;nbsp &amp;x; &amp;#; &amp;#x;\n&amp;#87654321;\n&amp;#abcdef0;\n&amp;ThisIsNotDefined; &amp;hi?;</p>\n" }, /* CommonMark #28 */
    { "&copy\n", "<p>&amp;copy</p>\n" }, /* CommonMark #29 */
    { "&MadeUpEntity;\n", "<p>&amp;MadeUpEntity;</p>\n" }, /* CommonMark #30 */
    { "`f&ouml;&ouml;`\n", "<p><code>f&amp;ouml;&amp;ouml;</code></p>\n" }, /* CommonMark #35 code span literal */
    { "&#42;foo&#42;\n*foo*\n", "<p>*foo*\n<em>foo</em></p>\n" }, /* CommonMark #37 entities not emphasis */
    { "foo&#10;&#10;bar\n", "<p>foo\n\nbar</p>\n" }, /* CommonMark #39 character refs preserve logical LF */
    { "&#9;foo\n", "<p>\tfoo</p>\n" }, /* CommonMark #40 character ref literal tab */
    { "&ngE;\n", "<p>≧̸</p>\n" }, /* HTML5 two scalar ngE */
    { "&NotEqualTilde;\n", "<p>≂̸</p>\n" }, /* HTML5 two scalar NotEqualTilde */
    { "&#x80; &#x9f;\n", "<p>€ Ÿ</p>\n" }, /* WHATWG C1 remap */
    { "&#xD800; &#x110000; &#9999999;\n", "<p>� � �</p>\n" }, /* Invalid scalar replacement */
    { "\\&copy; \\&#35;\n", "<p>&amp;copy; &amp;#35;</p>\n" }, /* Escaped ampersand not redecoded */
    { "*x &copy; y*\n", "<p><em>x © y</em></p>\n" }, /* Nested emphasis source entities */
    { "[&copy;](url)\n", "<p><a href=\"url\">©</a></p>\n" }, /* Link label semantic ownership */
    { "&#x26;lt;\n", "<p>&amp;lt;</p>\n" }, /* No recursive interpretation */
    { "``` foo\\+bar\nfoo\n```\n", "<pre><code class=\"language-foo+bar\">foo\n</code></pre>\n" }, /* #24 official escaped fence info */
    { "``` f&ouml;&ouml;\nfoo\n```\n", "<pre><code class=\"language-föö\">foo\n</code></pre>\n" }, /* #34 official named fence info */
    { "``` f&nGt;&nLt;\nbody\n```\n", "<pre><code class=\"language-f≫⃒≪⃒\">body\n</code></pre>\n" }, /* Two scalar named references in fence info */
    { "``` f&NotDefined;\nbody\n```\n", "<pre><code class=\"language-f&amp;NotDefined;\">body\n</code></pre>\n" }, /* Unknown fence info name not resolved */
    { "``` \\&copy;\nbody\n```\n", "<pre><code class=\"language-&amp;copy;\">body\n</code></pre>\n" }, /* Escaped amp not recursively decoded */
    { "``` code\n&copy;\n```\n", "<pre><code class=\"language-code\">&amp;copy;\n</code></pre>\n" }, /* Fenced body remains literal */
    { "``` lang&#x41;\nbody\n```\n", "<pre><code class=\"language-langA\">body\n</code></pre>\n" }, /* Numeric fence language */
    { "&nGt;&nGt;&nLt;&nLt;\n", "<p>≫⃒≫⃒≪⃒≪⃒</p>\n" }, /* Named reference expansion capacity */
    { "&nGt;", "<p>≫⃒</p>\n" }, /* Named reference expansion at EOF */
    { "[foo](/bar\\* \"ti\\*tle\")\n", "<p><a href=\"/bar*\" title=\"ti*tle\">foo</a></p>\n" }, /* #22 escaped punctuation destination/title */
    { "[foo](/f&ouml;&ouml; \"f&ouml;&ouml;\")\n", "<p><a href=\"/f%C3%B6%C3%B6\" title=\"föö\">foo</a></p>\n" }, /* #32 entities in destination/title */
    { "[a](url &quot;tit&quot;)\n", "<p>[a](url &quot;tit&quot;)</p>\n" }, /* #41 decoded quote cannot invent title syntax */
    { "[é](https://example.org/ö)\n", "<p><a href=\"https://example.org/%C3%B6\">é</a></p>\n" }, /* percent encode non-ASCII href */
    { "[&copy;](dest \"Title\")\n", "<p><a href=\"dest\" title=\"Title\">©</a></p>\n" }, /* decoded label and quoted title */
    { "[x](/f\\&copy; \"a\")\n", "<p><a href=\"/f&amp;copy;\" title=\"a\">x</a></p>\n" }, /* escaped ampersand cannot decode href */
    { "[x](/f%C3%B6)\n", "<p><a href=\"/f%C3%B6\">x</a></p>\n" }, /* already percent escaped path remains intact */
    { "[x](dest \"say \\\"hi\\\"\")\n", "<p><a href=\"dest\" title=\"say &quot;hi&quot;\">x</a></p>\n" }, /* escaped quote inside title */
    { "[x](dest 'title')\n", "<p><a href=\"dest\" title=\"title\">x</a></p>\n" }, /* single quoted title */
    { "[x](dest \"\")\n", "<p><a href=\"dest\" title=\"\">x</a></p>\n" }, /* empty title explicitly retained */
    { "[x](/f&#xF6; \"copy &copy;\")\n", "<p><a href=\"/f%C3%B6\" title=\"copy ©\">x</a></p>\n" }, /* numeric reference href UTF-8 percent */
    { "[x]()\n", "<p><a href=\"\">x</a></p>\n" }, /* empty destination */
    { "[link](<>)\n", "<p><a href=\"\">link</a></p>\n" }, /* #486 angle empty URI */
    { "[link](</my uri>)\n", "<p><a href=\"/my%20uri\">link</a></p>\n" }, /* #489 angle URI spaces */
    { "[a](<b)c>)\n", "<p><a href=\"b)c\">a</a></p>\n" }, /* #492 angle with literal right parenthesis */
    { "[link](foo(and(bar)))\n", "<p><a href=\"foo(and(bar))\">link</a></p>\n" }, /* #496 nested balanced destination */
    { "[link](foo(and(bar))\n", "<p>[link](foo(and(bar))</p>\n" }, /* #497 unbalanced destination remains literal */
    { "[link](<foo(and(bar)>)\n", "<p><a href=\"foo(and(bar)\">link</a></p>\n" }, /* #499 angle destination unbalanced punctuation is text */
    { "[link](foo\\)\\:)\n", "<p><a href=\"foo):\">link</a></p>\n" }, /* #500 escaped delimiter not grammar */
    { "[link](foo\\bar)\n", "<p><a href=\"foo%5Cbar\">link</a></p>\n" }, /* #502 raw backslash is percent encoded */
    { "[link](\"title\")\n", "<p><a href=\"%22title%22\">link</a></p>\n" }, /* #504 raw quote in URL is percent encoded */
    { "[link](   /uri\n  \"title\"  )\n",
      "<p><a href=\"/uri\" title=\"title\">link</a></p>\n" }, /* #510 */
    { "[link]( /uri\r\n 'title' )\r\n",
      "<p><a href=\"/uri\" title=\"title\">link</a></p>\n" },
    { "[x]( /uri\n (note) )\n",
      "<p><a href=\"/uri\" title=\"note\">x</a></p>\n" },
    { "[x]( /uri\n \"T\" ) tail\n",
      "<p><a href=\"/uri\" title=\"T\">x</a> tail</p>\n" },
    { "[link](/url \"title\")\n[link](/url 'title')\n[link](/url (title))\n", "<p><a href=\"/url\" title=\"title\">link</a>\n<a href=\"/url\" title=\"title\">link</a>\n<a href=\"/url\" title=\"title\">link</a></p>\n" }, /* #505 all 3 supported title delimiters */
    { "[link](/url \"title\")\n", "<p><a href=\"/url%C2%A0%22title%22\">link</a></p>\n" }, /* #507 Unicode NBSP not syntactic separator */
    { "[deep](((((((((((((((((((((((((((((((((x)))))))))))))))))))))))))))))))))\n", "<p><a href=\"((((((((((((((((((((((((((((((((x))))))))))))))))))))))))))))))))\">deep</a></p>\n" }, /* literal nested depth equals 32 */
    { "[deep]((((((((((((((((((((((((((((((((((x))))))))))))))))))))))))))))))))))\n", "<p>[deep]((((((((((((((((((((((((((((((((((x))))))))))))))))))))))))))))))))))</p>\n" }, /* no partial link with 33-deep nesting */
    { "[x](<foo bar>)\n", "<p><a href=\"foo%20bar\">x</a></p>\n" }, /* angle space decoded then percent encoded */
    { "[link](<foo\\>)\n", "<p>[link](&lt;foo&gt;)</p>\n" }, /* angle closing delimiter escaped invalid */
    { "[x](f(a(b)) \"info\")\n", "<p><a href=\"f(a(b))\" title=\"info\">x</a></p>\n" }, /* nested parentheses in unquoted href plus title */
    { "[x](dest (hello world))\n", "<p><a href=\"dest\" title=\"hello world\">x</a></p>\n" }, /* nested title parentheses literal title delimited */
    { "[x](<a<b>)\n", "<p>[x](&lt;a&lt;b&gt;)</p>\n" }, /* angle nested opening bracket invalid */
    { "[foo][bar]\n\n[bar]: /url \"title\"\n", "<p><a href=\"/url\" title=\"title\">foo</a></p>\n" }, /* #527 forward explicit binding */
    { "[foo][BaR]\n\n[bar]: /url \"title\"\n", "<p><a href=\"/url\" title=\"title\">foo</a></p>\n" }, /* #539 ASCII case-fold full reference label */
    { "[foo]: /url1\n\n[foo]: /url2\n\n[bar][foo]\n", "<p><a href=\"/url1\">bar</a></p>\n" }, /* #544 first definition wins */
    { "[foo][]\n\n[foo]: /url \"title\"\n", "<p><a href=\"/url\" title=\"title\">foo</a></p>\n" }, /* #553 collapsed reference */
    { "[Foo][]\n\n[foo]: /url \"title\"\n", "<p><a href=\"/url\" title=\"title\">Foo</a></p>\n" }, /* #555 normalized case collapsed */
    { "[foo]\n\n[foo]: /url \"title\"\n", "<p><a href=\"/url\" title=\"title\">foo</a></p>\n" }, /* #557 shortcut reference after forward declaration */
    { "[Foo]\n\n[foo]: /url \"title\"\n", "<p><a href=\"/url\" title=\"title\">Foo</a></p>\n" }, /* #561 normalized case shortcut */
    { "[foo] bar\n\n[foo]: /url\n", "<p><a href=\"/url\">foo</a> bar</p>\n" }, /* #562 shortcut with text following */
    { "[foo][bar]\n\n[foo]: /url1\n[bar]: /url2\n", "<p><a href=\"/url2\">foo</a></p>\n" }, /* #565 explicit label wins when both defined */
    { "[foo][]\n\n[foo]: /url1\n", "<p><a href=\"/url1\">foo</a></p>\n" }, /* #566 collapsed reference vs link target */
    { "[foo]()\n\n[foo]: /url1\n", "<p><a href=\"\">foo</a></p>\n" }, /* #567 inline link has precedence over shortcut */
    { "[foo](not a link)\n\n[foo]: /url1\n", "<p><a href=\"/url1\">foo</a>(not a link)</p>\n" }, /* #568 invalid inline link permits shortcut */
    { "[foo] [bar]\n\n[bar]: /url \"title\"\n", "<p>[foo] <a href=\"/url\" title=\"title\">bar</a></p>\n" }, /* #542 unknown adjacent full vs linked shortcut */
    { "[foo]\n[bar]\n\n[bar]: /url \"title\"\n", "<p>[foo]\n<a href=\"/url\" title=\"title\">bar</a></p>\n" }, /* #543 separate reference shortcuts across LF */
    { "# [foo][bar]\n\n[bar]: /url\n", "<h1><a href=\"/url\">foo</a></h1>\n" }, /* simple reference text resolves in heading */
    { "[x][id]\n\n[id]: /f&ouml; \"A &copy;\"\n", "<p><a href=\"/f%C3%B6\" title=\"A ©\">x</a></p>\n" }, /* root reference inline punctuation and title */
    { "Setext\n===\n[ref]: /url\n\n[ref]\n", "<h1>Setext</h1>\n<p><a href=\"/url\">ref</a></p>\n" }, /* Setext H1 closes root paragraph before adjacent definition */
    { "Setext\n---\n[ref]: /url\n\n[ref]\n", "<h2>Setext</h2>\n<p><a href=\"/url\">ref</a></p>\n" }, /* Setext H2 takes precedence over thematic break */
    { "Setext\r\n===\r\n[ref]: /url\r\n\r\n[ref]\r\n", "<h1>Setext</h1>\n<p><a href=\"/url\">ref</a></p>\n" }, /* CRLF line offset and definition ownership */
    { "* * *\n[key]: /uri\n\n[key]\n", "<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* root thematic star rule does not create list ownership */
    { "- - -\n[key]: /uri\n\n[key]\n", "<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* root thematic dash rule takes list precedence */
    { "  * * *\r\n[key]: /uri\r\n\r\n[key]\r\n", "<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* root indented thematic rule and CRLF */
    { "paragraph\n- - -\n[key]: /uri\n\n[key]\n", "<p>paragraph</p>\n<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* rule interrupts root paragraph */
    { "- item\n* * *\n[key]: /uri\n\n[key]\n", "<ul>\n<li>item</li>\n</ul>\n<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* root rule immediately after list */
    { "- item\n---\n[key]: /uri\n\n[key]\n", "<ul>\n<li>item</li>\n</ul>\n<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* dedented contiguous rule is not item Setext */
    { "> quote\n* * *\n[key]: /uri\n\n[key]\n", "<blockquote>\n<p>quote</p>\n</blockquote>\n<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* rule interrupts quote without blank */
    { "    code\n* * *\n[key]: /uri\n\n[key]\n", "<pre><code>code\n</code></pre>\n<hr />\n<p><a href=\"/uri\">key</a></p>\n" }, /* indented code ends before root rule */
    { "> quote\n# heading\n[ref]: /uri\n\n[ref]\n", "<blockquote>\n<p>quote</p>\n</blockquote>\n<h1>heading</h1>\n<p><a href=\"/uri\">ref</a></p>\n" }, /* root ATX after lazy-capable quote releases reference guard */
    { "    code\n# heading\n[ref]: /uri\n\n[ref]\n", "<pre><code>code\n</code></pre>\n<h1>heading</h1>\n<p><a href=\"/uri\">ref</a></p>\n" }, /* root ATX after indented code */
    { "> quote\r\n  ## heading\r\n[ref]: /uri\r\n\r\n[ref]\r\n", "<blockquote>\n<p>quote</p>\n</blockquote>\n<h2>heading</h2>\n<p><a href=\"/uri\">ref</a></p>\n" }, /* CRLF and <=3 root columns */
    { "> quote\n" TICK TICK TICK "c\n[shadow]: /blocked\n"
      TICK TICK TICK "\n[ref]: /uri\n\n[ref]\n",
      "<blockquote>\n<p>quote</p>\n</blockquote>\n"
      "<pre><code class=\"language-c\">[shadow]: /blocked\n</code></pre>\n"
      "<p><a href=\"/uri\">ref</a></p>\n" }, /* root fence after quote, fake definition inert */
    { "    code\n~~~\n[shadow]: /blocked\n~~~\n[ref]: /uri\n\n[ref]\n",
      "<pre><code>code\n</code></pre>\n"
      "<pre><code>[shadow]: /blocked\n</code></pre>\n"
      "<p><a href=\"/uri\">ref</a></p>\n" }, /* tilde fence after indented code */
    { "> quote\n~~~~\n[shadow]: /blocked\n~~~\n[shadow]: /blocked\n"
      "~~~~\n[ref]: /uri\n\n[ref]\n",
      "<blockquote>\n<p>quote</p>\n</blockquote>\n"
      "<pre><code>[shadow]: /blocked\n~~~\n[shadow]: /blocked\n</code></pre>\n"
      "<p><a href=\"/uri\">ref</a></p>\n" }, /* unequal shorter closer is opaque */
    { "Root\n2. text\n===\n[id]: /uri\n\n[id]\n",
      "<h1>Root\n2. text</h1>\n<p><a href=\"/uri\">id</a></p>\n" }, /* non-1 ordered marker cannot interrupt paragraph */
    { "Root\n+\n===\n[id]: /uri\n\n[id]\n",
      "<h1>Root\n+</h1>\n<p><a href=\"/uri\">id</a></p>\n" }, /* empty list marker stays paragraph text */
    { "Root\n-\n[id]: /uri\n\n[id]\n",
      "<h2>Root</h2>\n<p><a href=\"/uri\">id</a></p>\n" }, /* single '-' is Setext H2, not list */
    { "Root\r\n2. text\r\n===\r\n[id]: /uri\r\n\r\n[id]\r\n",
      "<h1>Root\n2. text</h1>\n<p><a href=\"/uri\">id</a></p>\n" }, /* CRLF ownership and source preservation */
    { "| Column |\n| --- |\n[shadow]: /blocked\n\n[ref]: /uri \"T\"\n\n[shadow] [ref]\n",
      "<table>\n<thead>\n<tr>\n<th>Column</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>[shadow]: /blocked</td>\n</tr>\n</tbody>\n</table>\n"
      "<p>[shadow] <a href=\"/uri\" title=\"T\">ref</a></p>\n" }, /* no-pipe body cell cannot donate a definition */
    { "| Item | Link |\n| --- | --- |\n| A | [go][ref] |\n\n"
      "[ref]: /uri\n\n[go][ref]\n",
      "<table>\n<thead>\n<tr>\n<th>Item</th>\n<th>Link</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n<td>A</td>\n"
      "<td><a href=\"/uri\">go</a></td>\n</tr>\n</tbody>\n</table>\n"
      "<p><a href=\"/uri\">go</a></p>\n" }, /* forward ref metadata is copied into table cell and root owners */
    { "| Column |\n| --- |\n[shadow]: /blocked\n"
      "# Root\n[ref]: /uri\n\n[shadow] [ref]\n",
      "<table>\n<thead>\n<tr>\n<th>Column</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>[shadow]: /blocked</td>\n</tr>\n</tbody>\n"
      "</table>\n<h1>Root</h1>\n"
      "<p>[shadow] <a href=\"/uri\">ref</a></p>\n" }, /* ATX ends table and restores root-definition admission */
    { "- Name | Link\n| --- | --- |\n[shadow]: /blocked\n"
      "| A | [go][ref] |\n# Root\n[ref]: /uri\n\n[shadow] [go][ref]\n",
      "<table>\n<thead>\n<tr>\n<th>- Name</th>\n<th>Link</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n<td>[shadow]: /blocked</td>\n"
      "<td></td>\n</tr>\n<tr>\n<td>A</td>\n"
      "<td><a href=\"/uri\">go</a></td>\n</tr>\n</tbody>\n</table>\n"
      "<h1>Root</h1>\n<p>[shadow] <a href=\"/uri\">go</a></p>\n" }, /* list-looking GFM header beats list admission */
    { "1. Item | Link\n| --- | --- |\n| A | [go][ref] |\n"
      "# Root\n[ref]: /uri\n\n[go][ref]\n",
      "<table>\n<thead>\n<tr>\n<th>1. Item</th>\n<th>Link</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n<td>A</td>\n"
      "<td><a href=\"/uri\">go</a></td>\n</tr>\n</tbody>\n</table>\n"
      "<h1>Root</h1>\n<p><a href=\"/uri\">go</a></p>\n" }, /* ordered-looking header stays a table */
    { "| Key | Value |\n| --- | --- |\n| A | B |\n"
      "1. first\n2. second\n\n[ref]: /uri\n\n[ref]\n",
      "<table>\n<thead>\n<tr>\n<th>Key</th>\n<th>Value</th>\n"
      "</tr>\n</thead>\n<tbody>\n<tr>\n<td>A</td>\n<td>B</td>\n"
      "</tr>\n</tbody>\n</table>\n<ol>\n<li>first</li>\n"
      "<li>second</li>\n</ol>\n<p><a href=\"/uri\">ref</a></p>\n" }, /* true table terminator becomes root list */
    { "<https://example.com?find=\\*>\n",
      "<p><a href=\"https://example.com?find=%5C*\">https://example.com?find=\\*</a></p>\n" }, /* official #20 */
    { "**a<https://foo.bar/?q=**>\n",
      "<p>**a<a href=\"https://foo.bar/?q=**\">https://foo.bar/?q=**</a></p>\n" }, /* official #480: autolink shields ** from emphasis */
    { "__a<https://foo.bar/?q=__>\n",
      "<p>__a<a href=\"https://foo.bar/?q=__\">https://foo.bar/?q=__</a></p>\n" }, /* official #481 */
    { "<http://foo.bar.baz>\n",
      "<p><a href=\"http://foo.bar.baz\">http://foo.bar.baz</a></p>\n" }, /* official #594 */
    { "<https://foo.bar.baz/test?q=hello&id=22&boolean>\n",
      "<p><a href=\"https://foo.bar.baz/test?q=hello&amp;id=22&amp;boolean\">https://foo.bar.baz/test?q=hello&amp;id=22&amp;boolean</a></p>\n" }, /* official #595 */
    { "<MAILTO:FOO@BAR.BAZ>\n",
      "<p><a href=\"MAILTO:FOO@BAR.BAZ\">MAILTO:FOO@BAR.BAZ</a></p>\n" }, /* official #597 */
    { "<https://../>\n",
      "<p><a href=\"https://../\">https://../</a></p>\n" }, /* official #600 */
    { "<https://example.com/\\[\\>\n",
      "<p><a href=\"https://example.com/%5C%5B%5C\">https://example.com/\\[\\</a></p>\n" }, /* official #603 */
    { "<https://foo.bar/baz bim>\n",
      "<p>&lt;https://foo.bar/baz bim&gt;</p>\n" }, /* official #602 is NOT an autolink */
    { "<irc://foo.bar:2233/baz>\n",
      "<p>&lt;irc://foo.bar:2233/baz&gt;</p>\n" }, /* non-allowlisted scheme stays safe literal */
    { "<javascript:alert>\n",
      "<p>&lt;javascript:alert&gt;</p>\n" }, /* no javascript URI admission */
    { "<script>alert(1)</script>\n",
      "&lt;script&gt;alert(1)&lt;/script&gt;\n" }, /* Type-1 RAW_HTML_BLOCK, production still escaped; no <p> */
    { "<foo@bar.example.com>\n",
      "<p><a href=\"mailto:foo@bar.example.com\">foo@bar.example.com</a></p>\n" }, /* official #604 safe email */
    { "<foo+special@Bar.baz-bar0.com>\n",
      "<p><a href=\"mailto:foo+special@Bar.baz-bar0.com\">foo+special@Bar.baz-bar0.com</a></p>\n" }, /* official #605 plus/hyphen */
    { "<a*b@Bar.example.com>\n",
      "<p><a href=\"mailto:a*b@Bar.example.com\">a*b@Bar.example.com</a></p>\n" }, /* literal '*' belongs to email, not emphasis */
    { "**<x_y@bar.example.com>**\n",
      "<p><strong><a href=\"mailto:x_y@bar.example.com\">x_y@bar.example.com</a></strong></p>\n" }, /* only external '**' is a delimiter */
    { "[foo]:\n/url\n\n[foo]\n",
      "<p><a href=\"/url\">foo</a></p>\n" }, /* official CommonMark #198 */
    { "[foo]:  \r\n  /uri \"A &amp; B\"\r\n\r\n[foo]\r\n",
      "<p><a href=\"/uri\" title=\"A &amp; B\">foo</a></p>\n" }, /* CRLF and decoded HTML5 title */
    { "[a][ref]\n\n[ref]:\n/uri\n",
      "<p><a href=\"/uri\">a</a></p>\n" }, /* forward full reference */
    { "[foo]:\n\n/url\n\n[foo]\n",
      "<p>[foo]:</p>\n<p>/url</p>\n<p>[foo]</p>\n" }, /* blank cannot join a definition */
    { "paragraph\n[foo]:\n/url\n\n[foo]\n",
      "<p>paragraph\n[foo]:\n/url</p>\n<p>[foo]</p>\n" }, /* cannot interrupt a paragraph */
    { "[foo]:\n" TICK TICK TICK "\ncode\n" TICK TICK TICK
      "\n\n[foo]\n",
      "<p>[foo]:</p>\n<pre><code>code\n</code></pre>\n<p>[foo]</p>\n" }, /* fence is not a reference target */
    { "[foo]:\n===\n\n[foo]\n",
      "<h1>[foo]:</h1>\n<p>[foo]</p>\n" }, /* Setext is not a target */
    { "[foo]:\n> /uri\n\n[foo]\n",
      "<p>[foo]:</p>\n<blockquote>\n<p>/uri</p>\n</blockquote>\n"
      "<p>[foo]</p>\n" }, /* quote cannot donate root destination */
    { "[Foo bar]:\n<my url>\n'title'\n\n[Foo bar]\n",
      "<p><a href=\"my%20url\" title=\"title\">Foo bar</a></p>\n" }, /* official #195 */
    { "[foo]: /foo-url \"foo\"\n[bar]: /bar-url\n  \"bar\"\n"
      "[baz]: /baz-url\n\n[foo],\n[bar],\n[baz]\n",
      "<p><a href=\"/foo-url\" title=\"foo\">foo</a>,\n"
      "<a href=\"/bar-url\" title=\"bar\">bar</a>,\n"
      "<a href=\"/baz-url\">baz</a></p>\n" }, /* official #217 */
    { "[foo]: /uri\r\n  \"A &amp; B\"\r\n\r\n[foo]\r\n",
      "<p><a href=\"/uri\" title=\"A &amp; B\">foo</a></p>\n" }, /* CRLF and decoded HTML5 title */
    { "[foo]: /uri\n   (title)\n\n[foo]\n",
      "<p><a href=\"/uri\" title=\"title\">foo</a></p>\n" }, /* root 3-column parentheses */
    { "[foo]: /uri\n\"title\"\n\n[foo]\n",
      "<p><a href=\"/uri\" title=\"title\">foo</a></p>\n" }, /* unindented quoted title */
    { "[foo]: /uri\n\"unterminated\n\n[foo]\n",
      "<p>&quot;unterminated</p>\n<p><a href=\"/uri\">foo</a></p>\n" }, /* malformed title remains paragraph */
    { "[foo]: /uri\n- item\n\n[foo]\n",
      "<ul>\n<li>item</li>\n</ul>\n"
      "<p><a href=\"/uri\">foo</a></p>\n" }, /* list opener cannot be title */
    { "[foo]: /uri\n\n\"title\"\n\n[foo]\n",
      "<p>&quot;title&quot;</p>\n<p><a href=\"/uri\">foo</a></p>\n" }, /* blank forbids title lookahead */

    { "[foo]: /uri \"first\"\n\"second\"\n\n[foo]\n",
      "<p>&quot;second&quot;</p>\n"
      "<p><a href=\"/uri\" title=\"first\">foo</a></p>\n" }, /* inline title already owns its definition */
    /* #183: official #193/#194/#196 and independent boundary controls. */
    { "   [foo]: \n      /url  \n           'the title'  \n\n[foo]\n",
      "<p><a href=\"/url\" title=\"the title\">foo</a></p>\n" }, /* official-193 */
    { "[Foo*bar\\]]:my_(url) 'title (with parens)'\n\n[Foo*bar\\]]\n",
      "<p><a href=\"my_(url)\" title=\"title (with parens)\">Foo*bar]</a></p>\n" }, /* official-194 */
    { "[foo]: /url '\ntitle\nline1\nline2\n'\n\n[foo]\n",
      "<p><a href=\"/url\" title=\"\ntitle\nline1\nline2\n\">foo</a></p>\n" }, /* official-196 */
    { "   [foo]:\r\n\t /url \t\r\n\t\t'title' \t\r\n\r\n[foo]\r\n",
      "<p><a href=\"/url\" title=\"title\">foo</a></p>\n" }, /* deep-CRLF-tab-owner */
    { "[foo]: /uri 'A &amp; B  \r\n    x\ty\r\n  z'\r\n\r\n[foo]\r\n",
      "<p><a href=\"/uri\" title=\"A &amp; B  \nx\ty\nz\">foo</a></p>\n" }, /* multiline-CRLF-payload */
    { "[foo]: /uri\n    'first\n     second'\n\n[foo]\n",
      "<p><a href=\"/uri\" title=\"first\nsecond\">foo</a></p>\n" }, /* optional-deep-multiline-title */
    { "[foo]: /uri (a\\(\n b\\))\n\n[foo]\n",
      "<p><a href=\"/uri\" title=\"a(\nb)\">foo</a></p>\n" }, /* parenthesized-multiline-title */
    { "[visible][ FOO*BAR\\] ]\n\n[foo*bar\\]]: /uri 'ok'\n",
      "<p><a href=\"/uri\" title=\"ok\">visible</a></p>\n" }, /* escaped-explicit-casefold */
    { "[Foo*bar\\]][]\n\n[foo*bar\\]]: /uri\n",
      "<p><a href=\"/uri\">Foo*bar]</a></p>\n" }, /* escaped-collapsed */
    { "**[Foo*bar\\]]**\n\n[Foo*bar\\]]: /uri\n",
      "<p><strong><a href=\"/uri\">Foo*bar]</a></strong></p>\n" }, /* escaped-delimiter-opacity */
    { "[bar][foo\\!]\n\n[foo!]: /url\n",
      "<p>[bar][foo!]</p>\n" }, /* official-545 */
    { "[foo][ref\\[]\n\n[ref\\[]: /uri\n",
      "<p><a href=\"/uri\">foo</a></p>\n" }, /* official-549 */
    { "[bar\\\\]: /uri\n\n[bar\\\\]\n",
      "<p><a href=\"/uri\">bar\\</a></p>\n" }, /* official-550 */
    { "[visible][a&b]\n\n[a&amp;b]: /wrong\n",
      "<p>[visible][a&amp;b]</p>\n" }, /* raw-entity-key-not-decoded */
    { "[a\\!]: /first 'one'\n[A\\!]: /second 'two'\n\n[a\\!]\n",
      "<p><a href=\"/first\" title=\"one\">a!</a></p>\n" }, /* escaped-first-definition-wins */
    { "[foo]: /url 'title\n\nwith blank line'\n\n[foo]\n",
      "<p>[foo]: /url 'title</p>\n<p>with blank line'</p>\n<p>[foo]</p>\n" }, /* official-197 */
    { "[foo]: /url \"title\" ok\n",
      "<p>[foo]: /url &quot;title&quot; ok</p>\n" }, /* official-209 */
    { "[foo]: /url 'never\nends\n\n[foo]\n",
      "<p>[foo]: /url 'never\nends</p>\n<p>[foo]</p>\n" }, /* same-line-unclosed */
    { "[foo]: /url\n\"title\" junk\n\n[foo]\n",
      "<p>&quot;title&quot; junk</p>\n<p><a href=\"/url\">foo</a></p>\n" }, /* optional-title-garbage-keeps-url */
    { "[foo]: /url\n'never\n\n[foo]\n",
      "<p>'never</p>\n<p><a href=\"/url\">foo</a></p>\n" }, /* optional-unclosed-keeps-url */
    { "[foo]: /url 'never\n# boundary\nclose'\n\n[foo]\n",
      "<p>[foo]: /url 'never</p>\n<h1>boundary</h1>\n<p>close'</p>\n<p>[foo]</p>\n" }, /* title-root-heading-barrier */
    { "[foo]: /url 'never\n```\nclose'\n```\n\n[foo]\n",
      "<p>[foo]: /url 'never</p>\n<pre><code>close'\n</code></pre>\n<p>[foo]</p>\n" }, /* title-root-fence-barrier */
    { "    [foo]: /url 'title'\n\n[foo]\n",
      "<pre><code>[foo]: /url 'title'\n</code></pre>\n<p>[foo]</p>\n" }, /* opening-indented-code-not-reference */
    { "[foo]:\n\n    /url\n\n[foo]\n",
      "<p>[foo]:</p>\n<pre><code>/url\n</code></pre>\n<p>[foo]</p>\n" }, /* blank-destination-barrier */
    { "[a[b]]: /wrong\n\n[a[b]]\n",
      "<p>[a[b]]: /wrong</p>\n<p>[a[b]]</p>\n" }, /* unescaped-nested-label-rejected */
    { "[foo]: /uri 'a\n&quot; onmouseover=&quot;alert(1)'\n\n[foo]\n",
      "<p><a href=\"/uri\" title=\"a\n&quot; onmouseover=&quot;alert(1)\">foo</a></p>\n" }, /* title-escaped-attribute-injection */
    { "[unused]: /url\n", "" }, /* unused root definition does not emit paragraph */
    { "", "" },
};

_Static_assert(sizeof(cases)/sizeof(cases[0])==383,
               "Explicit HTML fixture denominator changed");

static int test_golden(const html_fixture *f) {
    salts_md_document *doc=NULL;
    size_t source_bytes=strlen(f->markdown);
    CHECK(salts_md_parse(f->markdown,source_bytes,256u,
                        source_bytes+1u,&doc)==SALTS_MD_OK);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    size_t need=SIZE_MAX;
    CHECK(salts_md_render_html(&v,256u,NULL,0u,&need)==SALTS_MD_OK);
    const size_t expect=strlen(f->html);
    CHECK(need==expect);
    char *out=(char *)malloc(need+2u);
    CHECK(out!=NULL);
    memset(out,0xa5,need+2u);

    /* Prove atomic rejection of insufficient output capacity. */
    size_t required=SIZE_MAX;
    if(need) {
        CHECK(salts_md_render_html(&v,256u,out,need,&required)==
              SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(required==need);
        for(size_t k=0;k<need+2u;++k)
            CHECK((unsigned char)out[k]==0xa5);
    }
    CHECK(salts_md_render_html(&v,256u,out,need+1u,&required)==SALTS_MD_OK);
    CHECK(required==need);
    CHECK(!memcmp(out,f->html,need+1u));
    CHECK((unsigned char)out[need+1u]==0xa5);
    free(out);
    salts_md_document_destroy(doc);
    return 0;
}

/* #186: malformed public RichDoc metadata is rejected BEFORE writing any
 * HTML. The parser owns the bytes; these copies exercise an untrusted view,
 * without corrupting the successfully parsed document itself. */
static int test_gfm_metadata_fails_closed(void) {
    const char *inputs[] = {
        "| A | B |\n| :--- | ---: |\n| a | b |\n",
        "- [x] ready\n"
    };
    const salts_md_block_kind targets[] = {
        SALTS_MD_BLOCK_TABLE, SALTS_MD_BLOCK_CHECKBOX
    };
    for(size_t which=0u;which<2u;++which) {
        salts_md_document *doc=NULL;
        const size_t n=strlen(inputs[which]);
        CHECK(salts_md_parse(inputs[which],n,128u,n+1u,&doc)==SALTS_MD_OK);
        salts_md_rich_document_view view=salts_md_get_document_view(doc);
        size_t target=SIZE_MAX;
        for(size_t i=0u;i<view.document.block_count;++i)
            if(view.document.blocks[i].kind==targets[which])target=i;
        CHECK(target!=SIZE_MAX);
        const salts_md_block_metadata span=view.block_metadata[target];
        CHECK(span.byte_begin<span.byte_end &&
              span.byte_end<=view.metadata_bytes);
        char *owned=(char*)malloc(view.metadata_bytes);
        CHECK(owned!=NULL);
        memcpy(owned,view.metadata,view.metadata_bytes);
        owned[span.byte_begin]=which==0u?(char)0xff:'?';
        view.metadata=owned;
        unsigned char out[512];memset(out,0xa5,sizeof(out));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&view,128u,(char*)out,sizeof(out),
                                  &required)==SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(out);++i)CHECK(out[i]==0xa5);
        free(owned);
        salts_md_document_destroy(doc);
    }
    return 0;
}


/* #186: the public RichDoc validator checks generic preorder, while the
 * renderer must ALSO reject forged TABLE_ROW/CELL and TASK_ITEM/CHECKBOX
 * parent-child relationships before touching caller output. The copied
 * block array deliberately retains valid IDs and parent<index so only the
 * typed semantic ownership check can catch these malicious views. */
/* #186: a TABLE vector is exactly one alignment byte per physical
 * column. A caller can borrow/forge RichDoc spans without modifying parser
 * storage; neither a truncated vector, an expanded vector nor a detached
 * TABLE_CELL can produce partial HTML. Every rejection must occur in the
 * dry-run, with an untouched buffer and untouched length output. */
static int test_gfm_forged_table_shape_fails_closed(void) {
    static const char source[]=
        "| A | B |\n| :--- | ---: |\n| a | b |\n";
    salts_md_document *owned=NULL;
    CHECK(salts_md_parse(source,sizeof(source)-1u,128u,sizeof(source),
                        &owned)==SALTS_MD_OK);
    const salts_md_rich_document_view original=salts_md_get_document_view(owned);
    CHECK(salts_md_rich_document_validate(&original,128u)==SALTS_MD_OK);
    size_t table=SIZE_MAX,first_cell=SIZE_MAX;
    for(size_t i=1u;i<original.document.block_count;++i) {
        if(original.document.blocks[i].kind==SALTS_MD_BLOCK_TABLE)table=i;
        if(original.document.blocks[i].kind==SALTS_MD_BLOCK_TABLE_CELL &&
           first_cell==SIZE_MAX)first_cell=i;
    }
    CHECK(table!=SIZE_MAX && first_cell!=SIZE_MAX);
    const salts_md_block_metadata span=original.block_metadata[table];
    CHECK(span.byte_end-span.byte_begin==2u);
    size_t need=SIZE_MAX;
    CHECK(salts_md_render_html(&original,128u,NULL,0u,&need)==SALTS_MD_OK);
    CHECK(need>0u && need<512u);

    /* Shortened and expanded metadata vectors must each fail the same
     * two-pass renderer; no alignment is inferred from cell contents. */
    for(size_t variant=0u;variant<2u;++variant) {
        salts_md_rich_document_view forged=original;
        salts_md_block_metadata *meta=(salts_md_block_metadata*)malloc(
            original.block_metadata_count*sizeof(*meta));
        CHECK(meta!=NULL);
        memcpy(meta,original.block_metadata,
               original.block_metadata_count*sizeof(*meta));
        char *arena=(char*)malloc(original.metadata_bytes+1u);
        CHECK(arena!=NULL);
        if(original.metadata_bytes)
            memcpy(arena,original.metadata,original.metadata_bytes);
        arena[original.metadata_bytes]=0;
        forged.block_metadata=meta;
        forged.metadata=arena;
        if(variant==0u) {
            meta[table].byte_end=span.byte_begin+1u;
        } else {
            meta[table].byte_end=span.byte_end+1u;
            forged.metadata_bytes=original.metadata_bytes+1u;
        }
        unsigned char bytes[512];memset(bytes,0x92,sizeof(bytes));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&forged,128u,(char*)bytes,
                                  sizeof(bytes),&required)==
              SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(bytes);++i)CHECK(bytes[i]==0x92);
        free(arena);free(meta);
    }
    /* Reparenting a TABLE_CELL into TABLE preserves its generic preorder,
     * but the typed TABLE/ROW/CELL topology must be rejected. */
    {
        salts_md_rich_document_view forged=original;
        salts_md_block *nodes=(salts_md_block*)malloc(
            original.document.block_count*sizeof(*nodes));
        CHECK(nodes!=NULL);
        memcpy(nodes,original.document.blocks,
               original.document.block_count*sizeof(*nodes));
        nodes[first_cell].parent_index=(uint32_t)table;
        forged.document.blocks=nodes;
        unsigned char bytes[512];memset(bytes,0x92,sizeof(bytes));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&forged,128u,(char*)bytes,
                                  sizeof(bytes),&required)==
              SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(bytes);++i)CHECK(bytes[i]==0x92);
        free(nodes);
    }
    salts_md_document_destroy(owned);
    return 0;
}

/* #186: a projected RichDoc may forge fewer/more TABLE_CELL children
 * than the parser's per-column alignment vector. The HTML writer has a
 * strict row-close equality check and a bounds check before each cell.
 * Either malformed shape is rejected before any caller-visible byte is
 * produced, including within the measured-output query path. */
static int test_gfm_forged_row_cardinality_fails_closed(void) {
    static const char source[]=
        "| A | B |\n| --- | --- |\n| x | y |\n";
    salts_md_document *owned=NULL;
    CHECK(salts_md_parse(source,sizeof(source)-1u,128u,sizeof(source),
                        &owned)==SALTS_MD_OK);
    const salts_md_rich_document_view canonical=salts_md_get_document_view(owned);
    CHECK(salts_md_rich_document_validate(&canonical,128u)==SALTS_MD_OK);
    const size_t count=canonical.document.block_count;
    size_t row=SIZE_MAX,first_cell=SIZE_MAX,second_cell=SIZE_MAX;
    size_t first_text=SIZE_MAX;
    for(size_t i=1u;i<count;++i) {
        const salts_md_block *node=&canonical.document.blocks[i];
        if(node->kind==SALTS_MD_BLOCK_TABLE_ROW && row==SIZE_MAX)row=i;
        if(row!=SIZE_MAX && node->kind==SALTS_MD_BLOCK_TABLE_CELL &&
           node->parent_index==row) {
            if(first_cell==SIZE_MAX)first_cell=i;
            else if(second_cell==SIZE_MAX)second_cell=i;
        }
        if(first_cell!=SIZE_MAX && first_text==SIZE_MAX &&
           node->kind==SALTS_MD_BLOCK_TEXT &&
           node->parent_index==first_cell)first_text=i;
    }
    CHECK(row!=SIZE_MAX && first_cell!=SIZE_MAX &&
          second_cell!=SIZE_MAX && first_text!=SIZE_MAX);
    salts_md_block *copy=(salts_md_block*)malloc(count*sizeof(*copy));
    CHECK(copy!=NULL);
    for(size_t mode=0u;mode<2u;++mode) {
        memcpy(copy,canonical.document.blocks,count*sizeof(*copy));
        if(mode==0u) {
            /* Too few TABLE_CELL children: ROW close must reject. */
            copy[second_cell].kind=SALTS_MD_BLOCK_TEXT;
        } else {
            /* Too many TABLE_CELL children: index overflow must reject. */
            copy[first_text].kind=SALTS_MD_BLOCK_TABLE_CELL;
            copy[first_text].parent_index=(uint32_t)row;
        }
        salts_md_rich_document_view forged=canonical;
        forged.document.blocks=copy;
        unsigned char output[2048];memset(output,0xe5,sizeof(output));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&forged,128u,(char*)output,
                                  sizeof(output),&required)==
              SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(output);++i)
            CHECK(output[i]==0xe5);
        CHECK(salts_md_render_html(&forged,128u,NULL,0u,&required)==
              SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
    }
    free(copy);
    salts_md_document_destroy(owned);
    return 0;
}

static int test_gfm_forged_ancestry_fails_closed(void) {
    typedef struct {
        const char *markdown;
        salts_md_block_kind forged_kind;
    } case_t;
    static const case_t cases[] = {
        {"| A | B |\n| --- | --- |\n| a | b |\n", SALTS_MD_BLOCK_TABLE_ROW},
        {"| A | B |\n| --- | --- |\n| a | b |\n", SALTS_MD_BLOCK_TABLE_CELL},
        {"- [x] checked\n", SALTS_MD_BLOCK_TASK_ITEM},
        {"- [ ] pending\n", SALTS_MD_BLOCK_CHECKBOX}
    };
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        const size_t bytes=strlen(cases[k].markdown);
        salts_md_document *owned=NULL;
        CHECK(salts_md_parse(cases[k].markdown,bytes,128u,bytes+1u,
                            &owned)==SALTS_MD_OK);
        salts_md_rich_document_view view=salts_md_get_document_view(owned);
        const size_t count=view.document.block_count;
        salts_md_block *copy=(salts_md_block*)malloc(
            count*sizeof(salts_md_block));
        CHECK(copy!=NULL);
        memcpy(copy,view.document.blocks,count*sizeof(salts_md_block));
        size_t target=SIZE_MAX;
        for(size_t i=1u;i<count;++i)
            if(copy[i].kind==cases[k].forged_kind) {
                target=i;break;
            }
        CHECK(target!=SIZE_MAX && target>1u);
        copy[target].parent_index=0u;
        view.document.blocks=copy;
        CHECK(salts_md_rich_document_validate(&view,128u)==SALTS_MD_OK);
        unsigned char out[512];
        memset(out,0xc7,sizeof(out));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&view,128u,(char*)out,sizeof(out),
                                  &required)==SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(out);++i)
            CHECK(out[i]==0xc7);
        free(copy);
        salts_md_document_destroy(owned);
    }
    /* A ROOT TABLE may be forged beneath an unrelated PARAGRAPH even when
     * the generic RichDoc preorder remains valid; refuse such owners. */
    /* A caller may forge
     * an otherwise-valid generic RichDoc preorder by making it a direct
     * PARAGRAPH child; the renderer must reject it before output. */
    {
        static const char source[]=
            "intro\n\n| A | B |\n| --- | --- |\n| a | b |\n";
        salts_md_document *owned=NULL;
        CHECK(salts_md_parse(source,sizeof(source)-1u,128u,sizeof(source),
                            &owned)==SALTS_MD_OK);
        salts_md_rich_document_view view=salts_md_get_document_view(owned);
        const size_t count=view.document.block_count;
        salts_md_block *copy=(salts_md_block*)malloc(count*sizeof(*copy));
        CHECK(copy!=NULL);
        memcpy(copy,view.document.blocks,count*sizeof(*copy));
        size_t paragraph=SIZE_MAX,table=SIZE_MAX;
        for(size_t i=1u;i<count;++i) {
            if(copy[i].kind==SALTS_MD_BLOCK_PARAGRAPH && paragraph==SIZE_MAX)
                paragraph=i;
            if(copy[i].kind==SALTS_MD_BLOCK_TABLE && table==SIZE_MAX)
                table=i;
        }
        CHECK(paragraph!=SIZE_MAX && table>paragraph && table<count);
        copy[table].parent_index=(uint32_t)paragraph;
        view.document.blocks=copy;
        CHECK(salts_md_rich_document_validate(&view,128u)==SALTS_MD_OK);
        unsigned char out[512];
        memset(out,0xc7,sizeof(out));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&view,128u,(char*)out,sizeof(out),
                                  &required)==SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(out);++i)
            CHECK(out[i]==0xc7);
        free(copy);
        salts_md_document_destroy(owned);
    }
    /* Canonical list TABLE owner is itself in a typed LIST; forging its
     * ITEM parent to DOCUMENT must fail without a caller-visible write. */
    {
        static const char source[]=
            "- | A | B |\n  | --- | --- |\n  | a | b |\n";
        salts_md_document *owned=NULL;
        CHECK(salts_md_parse(source,sizeof(source)-1u,128u,sizeof(source),
                            &owned)==SALTS_MD_OK);
        salts_md_rich_document_view view=salts_md_get_document_view(owned);
        const size_t count=view.document.block_count;
        salts_md_block *copy=(salts_md_block*)malloc(count*sizeof(*copy));
        CHECK(copy!=NULL);
        memcpy(copy,view.document.blocks,count*sizeof(*copy));
        size_t item=SIZE_MAX,table=SIZE_MAX;
        for(size_t i=1u;i<count;++i) {
            if(copy[i].kind==SALTS_MD_BLOCK_LIST_ITEM && item==SIZE_MAX)
                item=i;
            if(copy[i].kind==SALTS_MD_BLOCK_TABLE)table=i;
        }
        CHECK(item!=SIZE_MAX && table>item);
        CHECK(copy[table].parent_index==item);
        copy[item].parent_index=0u;
        view.document.blocks=copy;
        CHECK(salts_md_rich_document_validate(&view,128u)==SALTS_MD_OK);
        unsigned char out[512];memset(out,0xc7,sizeof(out));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&view,128u,(char*)out,sizeof(out),
                                  &required)==SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(out);++i)CHECK(out[i]==0xc7);
        free(copy);
        salts_md_document_destroy(owned);
    }
    return 0;
}

/* #186: unsafe URL admission must fail the entire transactional HTML
 * render even when the LINK sits in a TABLE_CELL beneath a LIST/QUOTE or
 * after a preceding fixed disabled CHECKBOX. No HTML already measured can
 * leak to the caller and no link scheme can bypass the common URL policy. */
static int test_gfm_nested_unsafe_url_atomicity(void) {
    static const char *const inputs[] = {
        "| A | B |\n| --- | --- |\n"
        "| x | [bad](javascript:alert) |\n",
        "- | A | B |\n  | --- | --- |\n"
        "  | x | [bad](javascript:alert) |\n",
        "> - | A | B |\n>   | --- | --- |\n"
        ">   | x | [bad](data:text/html) |\n",
        "- [x] [bad](javascript:alert)\n",
        "- [x] safe\n\n  | A | B |\n  | --- | --- |\n"
        "  | x | [bad](javascript:alert) |\n"
    };
    _Static_assert(sizeof(inputs)/sizeof(inputs[0])==5u,
                   "GFM unsafe nested URL fixture denominator");
    for(size_t k=0u;k<sizeof(inputs)/sizeof(inputs[0]);++k) {
        const size_t bytes=strlen(inputs[k]);
        salts_md_document *owned=NULL;
        CHECK(salts_md_parse(inputs[k],bytes,128u,bytes+1u,
                            &owned)==SALTS_MD_OK);
        const salts_md_rich_document_view v=salts_md_get_document_view(owned);
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        bool link_found=false;
        for(size_t i=1u;i<v.document.block_count;++i)
            if(v.document.blocks[i].kind==SALTS_MD_BLOCK_LINK)
                link_found=true;
        CHECK(link_found); /* Exercise URL rejection, not literal fallback. */
        if(k==0u || k==1u || k==2u || k==4u) {
            bool table_found=false;
            for(size_t i=1u;i<v.document.block_count;++i)
                if(v.document.blocks[i].kind==SALTS_MD_BLOCK_TABLE)
                    table_found=true;
            CHECK(table_found);
        }
        unsigned char output[4096];
        memset(output,0x8d,sizeof(output));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&v,128u,(char*)output,
                                  sizeof(output),&required)==
              SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(output);++i)
            CHECK(output[i]==0x8d);
        CHECK(salts_md_render_html(&v,128u,NULL,0u,&required)==
              SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        salts_md_document_destroy(owned);
    }
    return 0;
}

/* #186: forged TABLE grandparent ownership must fail even when generic
 * preorder is valid and TABLE's immediate QUOTE or ITEM->LIST is legal.
 * The dry-run rejects before writing any caller bytes or output count. */
static int test_gfm_forged_container_chain_fails_closed(void) {
    static const struct {
        const char *source;
        salts_md_block_kind container;
    } samples[] = {
        {"intro\n\n> | A | B |\n> | --- | --- |\n> | a | b |\n",
         SALTS_MD_BLOCK_QUOTE},
        {"intro\n\n- | A | B |\n  | --- | --- |\n  | a | b |\n",
         SALTS_MD_BLOCK_LIST}
    };
    for(size_t k=0u;k<sizeof(samples)/sizeof(samples[0]);++k) {
        const size_t n=strlen(samples[k].source);
        salts_md_document *owned=NULL;
        CHECK(salts_md_parse(samples[k].source,n,128u,n+1u,
                            &owned)==SALTS_MD_OK);
        salts_md_rich_document_view v=salts_md_get_document_view(owned);
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        size_t baseline=SIZE_MAX;
        CHECK(salts_md_render_html(&v,128u,NULL,0u,&baseline)==SALTS_MD_OK);
        CHECK(baseline>0u && baseline<512u);
        const size_t count=v.document.block_count;
        salts_md_block *copy=(salts_md_block*)malloc(count*sizeof(*copy));
        CHECK(copy!=NULL);
        memcpy(copy,v.document.blocks,count*sizeof(*copy));
        size_t para=SIZE_MAX,container=SIZE_MAX,table=SIZE_MAX;
        for(size_t i=1u;i<count;++i) {
            if(copy[i].kind==SALTS_MD_BLOCK_PARAGRAPH && para==SIZE_MAX)
                para=i;
            if(copy[i].kind==samples[k].container &&
               container==SIZE_MAX)container=i;
            if(copy[i].kind==SALTS_MD_BLOCK_TABLE)table=i;
        }
        CHECK(para!=SIZE_MAX && container>para &&
              table>container && table<count);
        CHECK(copy[container].parent_index==0u);
        copy[container].parent_index=(uint32_t)para;
        v.document.blocks=copy;
        CHECK(salts_md_rich_document_validate(&v,128u)==SALTS_MD_OK);
        unsigned char out[512];
        memset(out,0x9b,sizeof(out));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&v,128u,(char*)out,sizeof(out),
                                  &required)==SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(out);++i)CHECK(out[i]==0x9b);
        CHECK(salts_md_render_html(&v,128u,NULL,0u,&required)==
              SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        free(copy);
        salts_md_document_destroy(owned);
    }
    return 0;
}

/* #186: a borrowed RichDoc can be forged without touching the canonical
 * parser's owned arena. Exercise malformed metadata AND valid generic
 * preorder with invalid typed TASK/TABLE ownership across root, QUOTE and
 * ordered QUOTE. Every failure must leave BOTH output and required untouched.
 * Re-rendering the original after 16 corruptions proves caller isolation. */
static int test_gfm_mixed_metadata_adversaries(void) {
    static const char *const sources[] = {
        "- [x] parent\n\n"
        "  | A | B |\n  | :--- | ---: |\n  | left | right |\n",
        "> - [x] parent\n>\n"
        ">   | A | B |\n>   | :--- | ---: |\n>   | left | right |\n",
        "> 4. [ ] parent\n>\n"
        ">    | A | B |\n>    | :--- | ---: |\n"
        ">    | left | right |\n"
    };
    enum { MODES=16, MAX_BLOCKS=256, MAX_METADATA=256, MAX_HTML=4096 };
    _Static_assert(sizeof(sources)/sizeof(sources[0])==3u,
                   "GFM mixed metadata source denominator");
    for(size_t k=0u;k<3u;++k) {
        const size_t n=strlen(sources[k]);
        salts_md_document *doc=NULL;
        CHECK(salts_md_parse(sources[k],n,MAX_BLOCKS,n+1u,&doc)==SALTS_MD_OK);
        const salts_md_rich_document_view original=salts_md_get_document_view(doc);
        CHECK(salts_md_rich_document_validate(&original,MAX_BLOCKS)==SALTS_MD_OK);
        const size_t count=original.document.block_count;
        CHECK(count>5u && count<MAX_BLOCKS);
        CHECK(original.metadata_bytes>2u &&
              original.metadata_bytes<MAX_METADATA);
        CHECK(original.raw_markdown_bytes==n &&
              memcmp(original.raw_markdown,sources[k],n)==0);
        size_t item=SIZE_MAX,check=SIZE_MAX,table=SIZE_MAX;
        size_t row=SIZE_MAX,cell=SIZE_MAX,paragraph=SIZE_MAX;
        for(size_t i=1u;i<count;++i) {
            const salts_md_block_kind kind=original.document.blocks[i].kind;
            if(kind==SALTS_MD_BLOCK_TASK_ITEM)item=i;
            if(kind==SALTS_MD_BLOCK_CHECKBOX)check=i;
            if(kind==SALTS_MD_BLOCK_TABLE)table=i;
            if(kind==SALTS_MD_BLOCK_TABLE_ROW && row==SIZE_MAX)row=i;
            if(kind==SALTS_MD_BLOCK_TABLE_CELL && cell==SIZE_MAX)cell=i;
            if(kind==SALTS_MD_BLOCK_PARAGRAPH && paragraph==SIZE_MAX)
                paragraph=i;
        }
        CHECK(item!=SIZE_MAX && check!=SIZE_MAX && table!=SIZE_MAX);
        CHECK(row!=SIZE_MAX && cell!=SIZE_MAX && paragraph!=SIZE_MAX);
        CHECK(check==item+1u && paragraph<table);
        const salts_md_block_metadata tm=original.block_metadata[table];
        const salts_md_block_metadata cm=original.block_metadata[check];
        CHECK(tm.byte_end-tm.byte_begin==2u);
        CHECK(cm.byte_end-cm.byte_begin==1u);
        CHECK(tm.byte_end<=original.metadata_bytes &&
              cm.byte_end<=original.metadata_bytes);
        size_t expected=SIZE_MAX;
        CHECK(salts_md_render_html(&original,MAX_BLOCKS,NULL,0u,
                                  &expected)==SALTS_MD_OK);
        CHECK(expected>0u && expected<MAX_HTML);
        unsigned char baseline[MAX_HTML], after[MAX_HTML];
        memset(baseline,0xb1,sizeof(baseline));
        size_t rendered=SIZE_MAX;
        CHECK(salts_md_render_html(&original,MAX_BLOCKS,(char*)baseline,
                                  sizeof(baseline),&rendered)==SALTS_MD_OK);
        CHECK(rendered==expected);

        for(size_t mode=0u;mode<MODES;++mode) {
            salts_md_block nodes[MAX_BLOCKS];
            salts_md_block_metadata spans[MAX_BLOCKS];
            char metadata[MAX_METADATA];
            memcpy(nodes,original.document.blocks,count*sizeof(*nodes));
            memcpy(spans,original.block_metadata,count*sizeof(*spans));
            memcpy(metadata,original.metadata,original.metadata_bytes);
            salts_md_rich_document_view forged=original;
            forged.document.blocks=nodes;
            forged.block_metadata=spans;
            forged.metadata=metadata;
            switch(mode) {
            case 0u:
                metadata[tm.byte_begin]=(char)0xff;break;
            case 1u:
                spans[table].byte_end=spans[table].byte_begin;break;
            case 2u:
                spans[table].byte_end=spans[table].byte_begin+1u;break;
            case 3u:
                metadata[cm.byte_begin]='?';break;
            case 4u:
                spans[check].byte_end=spans[check].byte_begin;break;
            case 5u:
                spans[check]=spans[table];break;
            case 6u:
                nodes[check].parent_index=nodes[item].parent_index;break;
            case 7u:
                nodes[item].parent_index=0u;break;
            case 8u:
                nodes[table].parent_index=(uint32_t)paragraph;break;
            case 9u:
                nodes[cell].parent_index=(uint32_t)table;break;
            case 10u:
                spans[table].byte_end=SIZE_MAX;break;
            case 11u:
                forged.block_metadata_count=count-1u;break;
            case 12u:
                forged.metadata=NULL;break;
            case 13u:
                forged.raw_markdown=NULL;break;
            case 14u:
                nodes[table].parent_index=nodes[item].parent_index;break;
            case 15u:
                nodes[check].kind=SALTS_MD_BLOCK_TEXT;break;
            default: return 1;
            }
            unsigned char guarded[MAX_HTML];
            memset(guarded,0xa6,sizeof(guarded));
            size_t need=SIZE_MAX;
            const salts_md_status status=salts_md_render_html(
                &forged,MAX_BLOCKS,(char*)guarded,sizeof(guarded),&need);
            if(status!=SALTS_MD_INVALID_ARGUMENT) {
                fprintf(stderr,"mixed GFM adversary source=%zu mode=%zu status=%d\n",
                        k,mode,(int)status);
                salts_md_document_destroy(doc);
                return 1;
            }
            CHECK(need==SIZE_MAX);
            for(size_t j=0u;j<sizeof(guarded);++j)
                CHECK(guarded[j]==0xa6);
            need=SIZE_MAX;
            CHECK(salts_md_render_html(&forged,MAX_BLOCKS,NULL,0u,
                                      &need)==SALTS_MD_INVALID_ARGUMENT);
            CHECK(need==SIZE_MAX);
            CHECK(original.raw_markdown_bytes==n &&
                  memcmp(original.raw_markdown,sources[k],n)==0);
        }
        unsigned char guarded[MAX_HTML];
        memset(guarded,0xa6,sizeof(guarded));
        size_t required=SIZE_MAX;
        CHECK(salts_md_render_html(&original,count-1u,(char*)guarded,
                                  sizeof(guarded),&required)==
              SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(required==SIZE_MAX);
        for(size_t j=0u;j<sizeof(guarded);++j)CHECK(guarded[j]==0xa6);

        memset(after,0xb1,sizeof(after));
        size_t again=SIZE_MAX;
        CHECK(salts_md_render_html(&original,MAX_BLOCKS,(char*)after,
                                  sizeof(after),&again)==SALTS_MD_OK);
        CHECK(again==expected && !memcmp(after,baseline,expected+1u));
        salts_md_document_destroy(doc);
    }
    puts("GFM mixed RichDoc adversaries: 48/48 invalid views refused atomically");
    return 0;
}

/* #186: adversarial deep owner chains must not pass merely because all
 * indices, kinds and span boundaries satisfy the generic public RichDoc
 * validation. The canonical parse is immutable and remains renderable after
 * all eight borrowed-graph mutations. No second Markdown parse is used to
 * decide whether a forged ancestor is legal. */
static int test_gfm_deep_illegal_owners_fail_closed(void) {
    static const char source[]=
        "intro\n\n"
        "> - [x] shipped\n>\n"
        ">   | A | B |\n>   | :--- | ---: |\n"
        ">   | left | right |\n";
    enum { MAX_NODES=256, MODES=8, GUARD=2048 };
    salts_md_document *owned=NULL;
    CHECK(salts_md_parse(source,sizeof(source)-1u,MAX_NODES,
                        sizeof(source),&owned)==SALTS_MD_OK);
    const salts_md_rich_document_view original=salts_md_get_document_view(owned);
    CHECK(salts_md_rich_document_validate(&original,MAX_NODES)==SALTS_MD_OK);
    size_t paragraph=SIZE_MAX,quote=SIZE_MAX,list=SIZE_MAX;
    size_t item=SIZE_MAX,checkbox=SIZE_MAX,table=SIZE_MAX;
    size_t row=SIZE_MAX,cell=SIZE_MAX;
    for(size_t i=1u;i<original.document.block_count;++i) {
        const salts_md_block_kind kind=original.document.blocks[i].kind;
        if(kind==SALTS_MD_BLOCK_PARAGRAPH && paragraph==SIZE_MAX)
            paragraph=i;
        if(kind==SALTS_MD_BLOCK_QUOTE && quote==SIZE_MAX)quote=i;
        if(kind==SALTS_MD_BLOCK_LIST && list==SIZE_MAX)list=i;
        if(kind==SALTS_MD_BLOCK_TASK_ITEM && item==SIZE_MAX)item=i;
        if(kind==SALTS_MD_BLOCK_CHECKBOX && checkbox==SIZE_MAX)
            checkbox=i;
        if(kind==SALTS_MD_BLOCK_TABLE && table==SIZE_MAX)table=i;
        if(kind==SALTS_MD_BLOCK_TABLE_ROW && row==SIZE_MAX)row=i;
        if(kind==SALTS_MD_BLOCK_TABLE_CELL && cell==SIZE_MAX)cell=i;
    }
    CHECK(paragraph<quote && quote<list && list<item &&
          item<checkbox && checkbox<table && table<row && row<cell);
    CHECK(original.document.blocks[quote].parent_index==0u);
    CHECK(original.document.blocks[list].parent_index==quote);
    CHECK(original.document.blocks[item].parent_index==list);
    CHECK(original.document.blocks[checkbox].parent_index==item);
    CHECK(original.document.blocks[table].parent_index==item);
    CHECK(original.document.blocks[row].parent_index==table);
    CHECK(original.document.blocks[cell].parent_index==row);

    size_t needed=SIZE_MAX;
    CHECK(salts_md_render_html(&original,MAX_NODES,NULL,0u,
                              &needed)==SALTS_MD_OK);
    CHECK(needed>0u && needed<GUARD);
    unsigned char baseline[GUARD];
    memset(baseline,0xcd,sizeof(baseline));
    size_t full=SIZE_MAX;
    CHECK(salts_md_render_html(&original,MAX_NODES,(char*)baseline,
                              sizeof(baseline),&full)==SALTS_MD_OK);
    CHECK(full==needed);
    const size_t count=original.document.block_count;
    CHECK(count<MAX_NODES);
    for(size_t mode=0u;mode<MODES;++mode) {
        salts_md_block copy[MAX_NODES];
        memcpy(copy,original.document.blocks,count*sizeof(*copy));
        switch(mode) {
        case 0u: copy[quote].parent_index=(uint32_t)paragraph;break;
        case 1u: copy[list].parent_index=(uint32_t)paragraph;break;
        case 2u: copy[item].parent_index=(uint32_t)quote;break;
        case 3u: copy[checkbox].parent_index=(uint32_t)list;break;
        case 4u: copy[table].parent_index=(uint32_t)list;break;
        case 5u: copy[table].parent_index=(uint32_t)paragraph;break;
        case 6u: copy[row].parent_index=(uint32_t)item;break;
        case 7u: copy[cell].parent_index=(uint32_t)item;break;
        default:return 1;
        }
        salts_md_rich_document_view forged=original;
        forged.document.blocks=copy;
        CHECK(salts_md_rich_document_validate(&forged,MAX_NODES)==SALTS_MD_OK);
        unsigned char out[GUARD];
        memset(out,0x94,sizeof(out));
        size_t required=SIZE_MAX;
        const salts_md_status status=salts_md_render_html(
            &forged,MAX_NODES,(char*)out,sizeof(out),&required);
        if(status!=SALTS_MD_INVALID_ARGUMENT) {
            fprintf(stderr,"GFM deep owner mutation=%zu status=%d\n",
                    mode,(int)status);
            salts_md_document_destroy(owned);
            return 1;
        }
        CHECK(required==SIZE_MAX);
        for(size_t i=0u;i<sizeof(out);++i)CHECK(out[i]==0x94u);
        CHECK(salts_md_render_html(&forged,MAX_NODES,NULL,0u,
                                  &required)==SALTS_MD_INVALID_ARGUMENT);
        CHECK(required==SIZE_MAX);
        CHECK(original.raw_markdown_bytes==sizeof(source)-1u);
        CHECK(!memcmp(original.raw_markdown,source,sizeof(source)-1u));
    }
    unsigned char unchanged[GUARD];
    memset(unchanged,0xcd,sizeof(unchanged));
    size_t again=SIZE_MAX;
    CHECK(salts_md_render_html(&original,MAX_NODES,(char*)unchanged,
                              sizeof(unchanged),&again)==SALTS_MD_OK);
    CHECK(again==needed && !memcmp(unchanged,baseline,needed+1u));
    salts_md_document_destroy(owned);
    puts("GFM forged deep quote/list/task/table ancestors: 8/8 rejected");
    return 0;
}

static int test_unsafe_unsupported(void) {
    /* Balanced-parenthesis destinations are not implemented by the C11 parser
     * yet. Use a syntactically admitted link to test the actual HTML URL gate. */
    const char bad_url[]="[bad](javascript:alert)\n";
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse(bad_url,sizeof(bad_url)-1u,64,256,&doc)==SALTS_MD_OK);
    salts_md_rich_document_view v=salts_md_get_document_view(doc);
    bool found_link=false;
    for(size_t i=0;i<v.document.block_count;++i)
        if(v.document.blocks[i].kind==SALTS_MD_BLOCK_LINK) found_link=true;
    CHECK(found_link);  /* Must exercise URL admission, not plain-text fallback. */
    char out[128];
    memset(out,0x4d,sizeof(out));
    size_t needed=0;
    CHECK(salts_md_render_html(&v,64u,out,sizeof(out),&needed)==
          SALTS_MD_INVALID_ARGUMENT);
    for(size_t i=0;i<sizeof(out);++i)
        CHECK((unsigned char)out[i]==0x4d);
    salts_md_document_destroy(doc);

    /* A second supported link scheme rejection keeps data: fail-closed. */
    const char data_url[]="[bad](data:text/html)\n";
    CHECK(salts_md_parse(data_url,sizeof(data_url)-1u,64,256,&doc)==SALTS_MD_OK);
    v=salts_md_get_document_view(doc);
    found_link=false;
    for(size_t i=0;i<v.document.block_count;++i)
        if(v.document.blocks[i].kind==SALTS_MD_BLOCK_LINK) found_link=true;
    CHECK(found_link);
    memset(out,0x4d,sizeof(out));
    CHECK(salts_md_render_html(&v,64u,out,sizeof(out),&needed)==
          SALTS_MD_INVALID_ARGUMENT);
    for(size_t i=0;i<sizeof(out);++i)
        CHECK((unsigned char)out[i]==0x4d);
    salts_md_document_destroy(doc);

    const char image[]="![alt](https://example.org/x.png)\n";
    CHECK(salts_md_parse(image,sizeof(image)-1u,64,256,&doc)==SALTS_MD_OK);
    v=salts_md_get_document_view(doc);
    CHECK(salts_md_render_html(&v,64u,NULL,0u,&needed)==SALTS_MD_OK);
    const char image_html[]=
        "<p><img src=\"https://example.org/x.png\" alt=\"alt\" /></p>\n";
    CHECK(needed==sizeof(image_html)-1u);
    salts_md_document_destroy(doc);

    CHECK(salts_md_render_html(NULL,64u,NULL,0u,&needed)==
          SALTS_MD_INVALID_ARGUMENT);
    return 0;
}

int main(void) {
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        if(test_golden(&cases[i])) {
            fprintf(stderr,"HTML fixture %zu failed: %s\n",i,cases[i].markdown);
            return 1;
        }
    }
    if(test_gfm_metadata_fails_closed()) return 1;
    if(test_gfm_forged_table_shape_fails_closed()) return 1;
    if(test_gfm_forged_ancestry_fails_closed()) return 1;
    if(test_gfm_forged_row_cardinality_fails_closed()) return 1;
    if(test_gfm_forged_container_chain_fails_closed()) return 1;
    if(test_gfm_nested_unsafe_url_atomicity()) return 1;
    if(test_gfm_mixed_metadata_adversaries()) return 1;
    if(test_gfm_deep_illegal_owners_fail_closed()) return 1;
    if(test_unsafe_unsupported()) return 1;
    printf("Stun C11 RichDoc HTML: %zu/%zu bounded subset fixtures passed\n",
           sizeof(cases)/sizeof(cases[0]),sizeof(cases)/sizeof(cases[0]));
    return 0;
}
