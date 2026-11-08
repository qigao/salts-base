# 编译型 JSONPath Program

## 背景

原 JSONPath 查询在每次 `json_path_query()` 调用中解析表达式，构造由
`down`/`sibling` 指针连接的 AST，递归执行后立即释放。重复查询会重复承担词法、
语法、小对象分配、字符串复制和 AST 指针追逐成本。

JSON 文档仍以树为事实源。大对象成员拥有可重建的连续顺序索引和 key hash
索引；JSONPath program 只保存查询逻辑，不保存任何 JSON 节点指针。

## 方案比较

1. 保留 AST 并缓存：迁移成本低，但继续保留分散分配和指针追逐，且 program
   布局不连续。
2. 完整栈式 VM：可把所有布尔表达式降为跳转字节码，但一次迁移会同时改变
   parser、表达式求值和路径遍历，回归面较大。
3. 连续指令数组与索引边：解析阶段仍使用现有 AST，随后将可达节点一次性降为
   固定大小指令；`down`/`sibling` 改为数组索引，字符串进入连续常量池。

当前选择方案 3。它移除了执行期 AST 所有权和指针关系，同时完整复用现有
JSONPath 语义。过滤/选择器表达式已进一步降为平坦跳转字节码（filter VM，见
「执行路径」），不改变公开 program API。

## 语法与标准差异

本库 JSONPath 是自洽方言：目标是行为清晰、可验证，不承诺与 IETF JSONPath
draft 或 Goessner 语法完全一致。

支持的语法：

- 根 `$`、当前节点 `@`、点号属性 `.name`、通配 `.*`；
- 方括号 union `[...]`：属性名（单/双引号）、非负与负数组下标、通配；结果按
  selector 书写序拼接，同一节点被多个 selector 命中时保留重复（RFC 9535
  §2.5.1.2）；
- 递归下降 `..`（RFC 9535 §2.5.2）：`..name`、`..*`、`..[sel,...]`；对输入节点自身及每个后代按文档序先序遍历，并对每个访问节点应用一次 child segment（即按 selector 序求值），各结果拼接；`..name` 是 `..['name']` 的简写；
- filter：标准 `[?...]` 与括号 `[(...)]` 两种方括号形式，例如
  `[?@.price < 10]`、`[?(@.price < 10 && @.stock > 0)]`、`[(@.price < 10)]`；
  当前节点用 `@`；`[?@.isbn]` 按成员存在性过滤（成员存在即真）；
  表达式在编译期被降为平坦跳转字节码（filter VM），执行期为单次指令循环；
  行为与旧递归求值一致，且为后续专用指令优化留出位置；
- 函数 `length()` / `count()` / `match()` / `search()`（RFC 9535 §2.3.6
  子集）：`length(@.x)` 返回单个被选节点的长度（数组元素数、对象成员数、
  字符串 Unicode 码点数），`length()` 无参时作用于当前节点；`count(@.x)`
  返回路径选中的节点数（如 `count(@.authors[*])` 统计数组元素数）；
  `match(@.x, 'pattern')` 要求整个字符串匹配正则，`search(@.x, 'pattern')`
  只需模式出现在任意位置（复用内置 `re` 引擎与预算）；`contains_ci(@.x, 's')`
  为 ASCII 大小写不敏感的子串包含（非标准扩展，字节级、仅 A-Z/a-z 折叠）；
  函数名后跟 `(` 时按函数识别，否则保留为普通属性名（`$.length`、
  `$.match`、`$.contains_ci` 不受影响）；
- slice `[start:end:step]`（RFC 9535 §2.3.4）：三个分量均可省略，start/end
  支持负数（相对数组尾部），step 为 0 时结果为空列表；step > 0 升序、step < 0
  降序取元素；只作用于数组，对对象返回空；
- 比较 `== != < <= > >=`，逻辑 `&& || !`，字面量：布尔、数字、字符串、`/.../ `；
- `~` 为有界字节级正则匹配（内置 `re` 引擎，`utils/include/re.h`）：支持
  `.` `^` `$` `*` `+` `?`、区间量词 `{m}` `{m,}` `{m,n}`、惰性量词 `*?` `+?` `??`、
  字符类 `[...]`、取反类 `[^...]`、范围 `[a-z]`、分支 `|`、分组 `()`、
  非捕获组 `(?:...)`、词边界 `\b \B`、正/负 lookahead `(?=...)` `(?!...)`、
  转义 `\s \S \w \W \d \D`；`/.../ ` 与 `'...'` 均可作 pattern；
- `contains(@.x, 'needle')`：大小写敏感的子串包含（字面量，无正则元字符语义）；
  用于需要旧 `~` 子串语义的场景。`contains` 后跟 `(` 时作为运算符，否则保留
  为普通属性名（`$.contains` 不受影响）；
- 正则预算限制（`re.h` 默认值）：pattern ≤ 255 字节、text ≤ 1 MiB、递归深度
  ≤ 256、执行步数 ≤ 1,000,000；超限或非法 pattern 在 filter 中视为不匹配。
- 过滤表达式中数组、对象与 null 节点不可作为比较操作数，类型不匹配时比较
  结果为 false。

不支持（语法错误，或在创建 stream matcher 时明确失败）：

- 完整脚本语言与其他标准函数（如 `match()`、`search()`、`value()`）；
- 完整 ECMA-262 / PCRE 风格正则（捕获组与 backreference、`\p{...}` Unicode
  property、内联标志 `(?i)` 等）不受支持；组只用于分组与联合，不保留捕获。

## 数据与生命周期协议

- `json_path_compile()` 复制表达式中的常量并返回唯一拥有的 program。
- program 由连续指令数组和连续常量池组成，不引用输入表达式或 JSON 树。
- `json_path_get_compiled()` 和 `json_path_query_compiled()` 在调用期间借用 program
  与 JSON 根；返回的 JSON 节点继续由 JSON 根拥有。
- query result 拥有结果指针数组，但不拥有匹配节点。
- `json_path_program_free()` 释放 program；调用方必须保证此时没有执行正在借用它。
- program 构造后不可变，但当前 JSONPath API 使用模块级错误字符串，因此线程
  拓扑仍定义为单线程或外部同步；JSON 树也不得在执行期间被并发修改。
- `json_path_get_compiled()` / `json_path_query_compiled()` 返回 `NULL` 时无法
  区分"无匹配"、"参数无效"与"OOM"；调用方应通过 `json_path_get_error()` 判断：
  错误串非空表示失败，为空表示正常无匹配。

编译最多接受 1,048,576 条可达指令和 64 MiB 常量数据。计数、乘法和累加均在
分配前检查；超过限制、语法错误或 OOM 均返回 `NULL` 并设置
`json_path_get_error()`。

以上上限均为编译期 `#define`（`JSONPATH_MAX_INSTRUCTIONS`、
`JSONPATH_MAX_CONSTANT_BYTES` 等），调整需重新编译；当前无运行期配置入口。

## 执行路径

普通属性指令在编译期保存 key bytes、长度和 hash；过滤/选择器表达式（比较、
布尔逻辑、regex/contains、路径存在性、length/count）在 lowering 阶段被降为
平坦跳转字节码（filter VM）：寄存器式 `LOAD_PATH/LOAD_CONST/LOAD_KEY/`
`LOAD_INDEX`、`CMP/CMP_LEAF`、`MATCH/CONTAINS`、`MATCH_FUNC/SEARCH_FUNC`、`NOT/NOT_EXISTS`、`AND/OR` 短路
跳转（直接判定槽位真值，不物化布尔）、`EXISTS/LENGTH/COUNT`。执行期对每个
候选元素跑一次指令循环，槽位索引由编译器界定、无逐条边界检查；
`@.member op 字面量` 与反写 `字面量 op @.member` 常见形态有专用叶子指令，
`length(@.x) op 字面量` / `count(@.x) op 字面量` 折叠为 `CMP_LEN_LEAF` /
`CMP_COUNT_LEAF`，
数值/字符串字面量走 `CMP_LEAF_NUM`/`CMP_LEAF_STR`（跳过值类型分派），
`[?!@.x]` 合成单条 `NOT_EXISTS`；单指令程序走直通路径（免循环/分派）。
只对 selector 根（方括号
filter、单选择器、union 子项）生成 VM 程序，嵌套操作数内联进父程序，不做
冗余逐节点编译；表达式过深（超过 64 槽位）回退递归求值。`jsonpath_program_expr()`
先走 VM，缺省回退到递归求值。`count()` 走非分配计数模式（`json_path_result_t`
的 count-only 路径），不再为每个候选元素分配节点数组。运行时值为 tagged
union（16 字节，BOOL/NUMBER/STRING 各存一种载荷），寄存器文件 64 槽 = 1 KiB。
VM 布局是 `json_path_program_t` 私有，公开 program API 不变。

VM 与 JSON DOM 的解耦边界：字节码通用，DOM 依赖集中在少数取值函数
（`resolve`/`eval_path`/`count_path`/节点长度）。曾尝试以函数指针 vtable 抽象为
通用表达式引擎，本机实测对最热门的单比较 filter 带来约 10% 回归（间接调用阻断
resolve 内联 + 每元素 env 拷贝），且当前无 YAML/XML 等消费方，故回退为直接静态
调用，仅保留上述边界注释；未来确有第二后端时再按 profile 结果引入。

执行时：

```text
program GET_KEY -> json_object_get_hashed_v()
                -> 大对象 hash index
                -> 小对象短链扫描
```

数组下标直接使用已有连续数组索引。wildcard、union、过滤和布尔短路关系使用
指令数组中的索引边执行，不再访问临时 AST。

执行期复杂度（N = 容器成员数，m = 短链长度，n = 字符串长度）：

- 单 key 属性：大对象开放寻址 hash 平均 O(1)、最坏 O(capacity)；小对象短链
  O(m)；
- 数组下标：O(1)（连续 index）；
- wildcard / filter：O(N)（逐成员求值）；union：O(S × N)（S 个 selector 各自逐成员求值，结果按 selector 序拼接、保留重复）；
- 递归下降 `..`：对每个输入节点及其全部后代各应用一次 selector，最坏 O(文档节点总数)；
- `~` 正则匹配：首原子为单个必选字面量字节且无顶层分支时，先用 SIMDe
  扫描候选起始字节，只在候选位置尝试匹配；其余模式逐位置尝试，`^` 锚定后
  仅从 index 0 开始；单次匹配在 step 预算（默认 1,000,000 步）内回溯，
  超限视为不匹配；
- 编译：O(tokens + constant bytes) 时间与空间。

## SIMD 使用

SIMD 统一走 vcpkg 的 SIMDe 头（`simde/x86/sse2.h`），不写平台 intrinsic；
默认 fail fast，SIMDe 函数在非 x86 构建下降为可移植 C，尾部未对齐字节回退到
同语义标量函数，两者都有等价性测试。

现有使用点：

- `parser/json_lexer_whitespace.h`：`json_skip_rfc_whitespace_simde` 跳过空白、
  `json_find_plain_ascii_string_end` 定位纯 ASCII 字符串结束引号；
- `parser/re_scan.h`：`re_scan_first_byte_simde`（16 字节 cmpeq + movemask）
  与 `re_scan_first_byte_scalar`，供 re 引擎与 contains 复用；
- `parser/jsonpath_contains.h`：`jsonpath_contains_simde`（首字节向量扫描 +
  memcmp 校验）与标量等价实现，供 JSONPath `contains()` 使用；
- `parser/jsonpath_utf8.h`：`jsonpath_utf8_length_simde`（16 字节 AND+CMPEQ+
  movemask 统计非续字节）与标量等价实现，供 `length()` 字符串码点计数使用；
- `src/re.c`：`re_match_borrowed` 对“首原子为单个必选字面量字节且无顶层分支”
  的模式先用 SIMDe 扫描候选起始字节，只在候选位置进入回溯匹配。

`src/re.c` 是从 `databind/runtime/re.c` 复制的本地副本，上述前缀跳过为本地新增
优化；`databind/runtime/re.c` 保持原样。前缀跳过的唯一可观察差异：大文本无命中时
返回 `RE_STATUS_NO_MATCH` 而非耗尽 step 预算返回 `RE_STATUS_STEP_LIMIT`，
JSONPath 层对两者都视为“不匹配”。

## 兼容、迁移与回滚

原有 `json_path_get()` / `json_path_query()` 签名和结果语义不变，它们现在是
compile-execute-free 的兼容包装。重复执行方可迁移到 compile-once API；一次性
调用方无需修改。

TurboParser facade 提供对应 opaque program API，不向调用方暴露内部指令布局。
如需回滚，可删除新增 facade/program API，并让旧入口恢复直接 AST 执行；JSON
树、对象索引和序列化格式不需要迁移。

## 流式执行

`json_path_stream_create()` 在同一个 immutable program 上建立 SAX matcher，
不会创建 `json_value_t` 或 object hash index。当前 streamable 子集包括：

- root/key 路径；
- 非负数组下标；
- wildcard；
- key/index union。

匹配到的值通过 `on_match_start`、raw SAX value events 和
`on_match_end` 交付。字符串、object key 和数字 token 都是 callback 期间的
borrowed view，流式接口不会返回可跨 callback 保存的 `json_value_t *`。

slice、递归下降 `..`、负下标和依赖完整对象状态的表达式在创建 matcher 时明确
失败，并应改用 DOM API；不会隐式退化为构建完整 DOM。

stream 支持「标量子元素 filter」子集：`[?@ <op> <const>]`（含反写
`[?<const> <op> @]`），谓词在候选标量值的结束事件处判定，零缓冲、不构建
DOM；适用于数组/对象的标量元素过滤（如 `$.nums[?@ > 2]`、
`$.cfg[?@ == 'on']`）。需要扫描候选对象成员或布尔组合的谓词（如
`[?@.port >= 8000]`、`[?@ > 2 && @ < 5]`）在单遍 SAX 下无法于对象结束前
判定，仍走 DOM API（创建 matcher 时明确失败）；filter 必须位于路径末尾，
filter 后接剩余 segment 同样失败。union 的 fan-out
按 RFC 9535 §2.5.1.2 保留重复：`['a','a']` 或 `[0,0]` 使 match_count 计为
2（与 DOM nodelist 一致），但被选中的值本身仍只向事件回调交付一次。stream matcher 固定限制 64 个 path segment、64 条 union alternative 和 256 层输入深度，超过
限制直接返回错误。

注意：stream 仍会完整扫描输入（内部为 SAX 全量解析），只是不构建 DOM 与
对象索引；对超大文档节省的是内存而非扫描时间。

## 验证范围

- 简单属性、负数组下标、Unicode 属性名
- wildcard 多结果、对象 union
- 当前节点过滤、比较和布尔表达式；标准 `[?...]` filter 与括号形式等价；
  filter VM 与递归求值结果一致（比较/AND/OR/regex/contains/length）；
  `match()`/`search()` 全串/任意位置语义与成员名消歧；`contains_ci()` 大小写不敏感
  与成员名消歧；length/count 叶子折叠；单指令直通路径
- program 脱离表达式缓冲区后复用，并跨多个 JSON 根执行
- 旧 API 与 TurboParser facade 兼容性
- stream wildcard、完整 subtree 事件、key/index union、任意 chunk 边界
- stream 对非标量 filter、slice、负下标和 callback 失败的 fail-fast 行为；
  stream 标量 filter 的数组/对象、数值/字符串/布尔、反写比较、长字符串暂存
- 递归下降 `..`：member/通配/下标/union、`..name` 与 `..['name']` 等价、单值首匹配、stream 创建 fail-fast
- `~` 字面量正则的编译期预编译与 program 生命周期释放
- contains 子串匹配，以及 contains 作为普通属性名时的消歧；SIMDe contains
  与标量/strstr 等价（空 needle、首尾命中、缺位）
- union 按 selector 序拼接：对象/数组逆序、重复 selector 保留、descendant
  union 每节点按 selector 序、union 后接剩余 segment、stream 重复 union fan-out 计数
- slice：start/end/step 各种省略组合、正负步长、负下标、零步长空结果、
  倒序全量、slice 后接 segment、descendant + slice、非数组返回空
- re 字面量前缀：SIMD/标量 first-byte 扫描等价、前缀命中偏移、失败候选后继续
- Release benchmark 以及 Debug/AddressSanitizer 测试

## 性能复现

compile-once API 的收益前提是同一表达式被重复执行多次：词法、语法与 lowering
的编译成本只发生一次，之后每次执行只做指令数组跳转与索引查找。对一次性查询，
`json_path_get()` / `json_path_query()` 的 compile-execute-free 包装即可，无需迁移。

复现命令：Release 构建后运行 `benchmark_query`、`benchmark_json_parser`、
`benchmark_json_whitespace`、`benchmark_regex` 与 `benchmark_filter`（由
`parser/json_parser/benchmarks/CMakeLists.txt` 生成）。

SIMD 相关基准（本机 MSVC x64 Release，simde 0.8.2，64 KiB 缓冲、无命中最坏
场景；仓库当前未提交跨机器基线，数字仅用于本机相对对比）：

| 项目 | 标量 | SIMDe | 加速比 |
|---|---|---|---|
| first-byte 扫描（64 KiB） | 4,582 MiB/s | 31,865 MiB/s | ≈7.0x |
| re 无命中扫描（64 KiB，`[a-z]lpha` 逐位置 vs `alpha` 前缀跳过） |
  36.6 MiB/s | 33,584 MiB/s | ≈918x |
| contains 无命中（64 KiB，strstr 对比） | 22,624 MiB/s | 28,862 MiB/s | ≈1.28x |

端到端（2,000 项 × 64 字节 name，编译型 program，无命中）：
`~ 'alpha'` ≈ 7.96M 项/s，`contains` ≈ 11.66M 项/s。

说明：`~` 加速来自 re.c 的字面量前缀跳过（本地优化，见下）；contains 原为
libc `strstr`，SIMDe 版在无命中最坏场景仍略快，故保留 SIMDe 路径。
