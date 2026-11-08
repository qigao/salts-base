# Salts IDNA

`Salts::IDNA` 在应用边界把 UTF-8 域名转换成经过校验的 ASCII 连接身份。
它只依赖 `Salts::Unicode`（及其 `Salts::Core` 依赖），不包含 DNS 客户端、TLS、
URL 解析、缓存或后台线程，不依赖 ICU 或 SaltsUtils。开发事项：Salts #1091。

## 契约与使用

包含 `<idna.h>`，链接 `Salts::IDNA`。两个显式 profile 均固定
Unicode 17.0.0 / UTS #46 revision 35：
nontransitional；开启 STD3、CheckHyphens、CheckBidi、CheckJoiners、VerifyDnsLength，
拒绝无效 Punycode；另外执行 RFC 5892 ContextO。未知 profile 明确失败。
它不是完整严格 IDNA2008，也不检测混合文字、视觉相似或同形欺骗。

- `SALTS_IDNA_UNICODE17_UTS46_35_STRICT` 保留原契约，拒绝尾点。
- `SALTS_IDNA_UNICODE17_UTS46_35_DNS` 允许一个最终根分隔符，在 mapping/NFC
  之后识别并保留为 ASCII 点。因此 `example.com.` 后跟 U+00AD 等 ignored
  字符仍得到 `example.com.`；只有根节点、重复分隔符和空内部标签仍失败。

DNS profile 沿用同一函数签名、workspace 与错误码，不改变 STRICT 的接纳范围。
域名主体上限仍是 253 字节；DNS 的绝对名称最多 254 字节，输出含 NUL 需 255 字节。
例如将下例 profile 换成 DNS、输出容量改为 255，并传入 `bücher.de.\xC2\xAD`，
成功结果为 `xn--bcher-kva.de.`。该 profile 是 rc.2 之后的新增能力；消费端必须
使用包含它的同一套 SDK 头文件和库。

```c
#include <idna.h>

char mapped[4096], normalized[4096], ascii[254];
uint32_t scalars[4096];
salts_idna_workspace work = {
    mapped, sizeof mapped, normalized, sizeof normalized, scalars, 4096
};
size_t ascii_size = 0;
salts_idna_status status = salts_idna_to_ascii(
    vstr_from_cstr("bücher.de"), SALTS_IDNA_UNICODE17_UTS46_35_STRICT,
    &work, ascii, sizeof ascii, &ascii_size);
/* Only SALTS_IDNA_OK permits using ascii: "xn--bcher-kva.de", 16 bytes.
 * Submit ascii/ascii_size to cnet_name_lookup_submit, and retain this SAME
 * identity for TLS hostname verification, SNI and the appropriate authority.
 * On error, propagate status; ascii/ascii_size do not contain a new result. */
```

示例容量是应用选择的预算，不保证任意输入都能容纳。输入按 `vstr.len` 提供字节预算，
应用应在调用前限制输入长度。三个 scratch 区域分别容纳 mapping 后 UTF-8、NFC 后
UTF-8（含末尾 NUL）、规范分解的 uint32_t scalar；capacity 单位由字段名和头文件定义。
中间结果超出预算返回 `SALTS_IDNA_WORKSPACE`，不能用最终 DNS 长度替代中间存储预算。

输入仅在同步调用期间借用，不保留。输出、workspace、描述符和成功长度指针必须互不
重叠，调用期间由调用者独占；所有者与释放责任始终属于调用者。库不分配堆内存，
不同线程可共享只读输入和数据表，但必须提供各自的 workspace/output。
任意失败都保持整个 destination 与 `*out_size` 不变；scratch 内容不保证保留。
成功输出以 NUL 终止，报告长度不含末尾 NUL；不会截断或返回部分成功标签。

错误区分参数、profile、UTF-8、禁止字符、标签/A-label、Bidi、ContextJ、ContextO、
DNS 长度、workspace、输出容量和算术溢出。不提供源字节 offset 或部分标签结果。
遇错停止，调用方不得把失败输出交给 DNS/TLS。无重试、隐式回退或日志副作用。

## 算法与兼容边界

顺序为严格 UTF-8 → UTS #46 mapping → NFC → 标签划分 → A-label 解码/校验 →
Punycode 编码 → 域名整体 Bidi 与长度校验 → 一次性提交。
`xn--` 输入先解码，必须已是 NFC、含非 ASCII 且满足完整标签规则；不会通过再次
mapping/NFC 修复。编码回查保证规范 A-label。Bidi 的启用条件检查整个解码后域名，
包括前置 ASCII 标签；`123.א` 和 `123.xn--4db` 都失败。

空域名、空非根标签、embedded NUL 拒绝；ASCII 标签 1–63 字节，域名主体 1–253 字节。
STRICT 拒绝尾点；DNS 仅在完整映射后允许并保留一个根分隔符。
例如 `faß.de` 保留 nontransitional 语义，得到 `xn--fa-hia.de`，不会变成 `fass.de`。
点的兼容变体按 mapping 表统一。NFC 不执行 NFKC，也不是一般 case-folding 接口。

数字 IP、URI authority、端口、percent-decoding 在应用侧独立处理；不要向此接口传入
完整 URL 或把 IPv6 当作域名。原 Unicode 文本可另存用于显示，但不是连接验证身份。
既有 CNet ASCII normalizer、尾点及 numeric-IP 行为不变，CNet runtime 不新增 IDNA
依赖。现有正式本地 DNS 测试覆盖显式转换后的 A/AAAA 查询，并核对 wire QNAME。
本模块不自动修改现有应用的 DNS、TLS 或 SNI 调用点。

NFC 的分解表已展开，Hangul 使用算法分解/组合；CCC 稳定重排最坏为组合序列长度的
平方。每个可成功 DNS 标签的 scalar 数受 63 字节输出约束，Punycode 在该有界范围内
按 RFC 3492 处理；乘加在使用前检查。数据均不可变，没有初始化/关闭协议。

## 数据、测试与交付

原始 Unicode 17 文件、官方 URL、SHA-256 和许可证位于
[`unicode/data`](../unicode/data/README.md)。生成器仅在开发期运行，读取固定 UCD，
不依赖 Python 或系统 ICU 的 Unicode 版本；正常编译与运行不下载数据。

```powershell
python unicode/tools/generate_normalization_data.py --check
python idna/tools/generate_idna_data.py --check
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure -R '^(test_salts_(unicode|idna)($|_)|cnet_name_lookup(_dns)?_test$)'
```

CTest 中的正式覆盖：

- `test_salts_unicode_normalize`：全部 20,034 条官方 NFC 向量的五列不变量，以及
  Part 1 未列出的合法 scalar 恒等性；NUL、版本、容量及失败输出不变。
- `test_salts_idna_conformance`：全部 6,391 条 IdnaTestV2 nontransitional 向量，
  正确处理空字段继承；549 条成功精确比对及幂等回查，5,842 条失败核对输出不变。
  无跳过项；不声明 ToUnicode/transitional 覆盖，ContextO 由独立测试覆盖。
- `test_salts_idna`：ContextJ/O、全域 Bidi、NFC、伪 A-label、非法 UTF-8/NUL、
  四种分隔符及 ignored 后缀、STRICT 与 DNS 的根分隔符差异、63/64 与
  253/254 字节界限、绝对名称 255 字节输出、恰限/不足容量、alias 与溢出。
- `cnet_name_lookup_dns_test`：本地 UDP DNS fixture，核对转换后名称和 A/AAAA 结果。
- 现有 `test_salts_unicode_installed_sdk`：安装后仅通过导出 target 编译并运行
  C11/C++17 NFC/IDNA 调用，复用原安装测试，未新增临时消费工程。

这些条目描述测试内容，不代表每个平台已经执行。原生 CI 运行上述集合，Android/iOS
仅交叉构建；平台结果、sanitizer 与未验证范围以 PR 的实际运行记录为准。

## 选择与迁移

在 CNet 热路径隐式转换会改变既有错误语义和 A-label 接纳，还可能让 DNS 与 TLS
采用不同身份；引入 ICU 会增加另一套 Unicode 版本来源及运行时依赖。因此采用
独立 IDNA target + 显式 NFC + 调用者有界 workspace，代价是调用方需要准备中间存储。
该新增 API 不改变已有 Unicode struct、枚举值或 CNet 接口；旧应用行为保持稳定。

消费端应从同一 SDK 获取 Unicode/IDNA，先完成显式转换再接入连接流程；不存在
新头文件搭配旧库的兼容路径。回滚时一并撤回新调用点与 IDNA/NFC 导出，保留旧 CNet
行为。Unicode 所有权从 SaltsUtils 迁移的配对发布要求仍见
[`unicode/README.md`](../unicode/README.md)；本模块不是既有 2.3 RC 发布的前置条件。

标准依据：[UTS #46 rev 35](https://www.unicode.org/reports/tr46/tr46-35.html)、
[UAX #15 rev 57](https://www.unicode.org/reports/tr15/tr15-57.html)、
[RFC 3492](https://www.rfc-editor.org/rfc/rfc3492.html)、
[RFC 5892 Appendix A](https://www.rfc-editor.org/rfc/rfc5892.html#appendix-A)、
[RFC 5893](https://www.rfc-editor.org/rfc/rfc5893.html)。
