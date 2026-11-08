# Unicode 17.0.0 名称数据

事实源是 Unicode 官方发布文件，原文件未经修改：

| 文件 | 来源 | SHA-256 |
| --- | --- | --- |
| `17.0.0/DerivedName.txt` | https://www.unicode.org/Public/17.0.0/ucd/extracted/DerivedName.txt | `019758bbe6c756c40fca6d505187ea660c5e195533e2ff2c841963a212c9d369` |
| `17.0.0/NameAliases.txt` | https://www.unicode.org/Public/17.0.0/ucd/NameAliases.txt | `793f6f1e4d15fd90f05ae66460191dc4d75d1fea90136a25f30dd6a4cb950eac` |
| `LICENSE.txt` | https://www.unicode.org/license.txt | `e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96` |

许可为 Unicode License V3；完整许可随源码保留，并安装到 `share/licenses/salts_unicode/`。
名称和别名来源仅为上述两个版本固定的文件，不使用宿主 Python 的 Unicode 数据。

从仓库根目录运行（Python 3.10+，仅开发期需要）：

```powershell
python unicode/tools/generate_names.py
python unicode/tools/generate_names.py --check
```

生成器先校验源文件哈希，名称排序、重复校验后生成 `unicode/src/unicode_names_data.h`。
该头文件纳入源码分发，普通构建不执行 Python。升级必须同时更换版本、哈希和数据，
重新生成并执行全量验证；不要直接修改生成头。名称查询不需要运行时文件、网络或 ICU。
re2c 仍负责 UTF-8 分类和 Jinja 字符串词法，名称表负责正则不能提供的名称到 scalar 映射。

普通名称及别名 ASCII 大小写不敏感，但不折叠空白、不忽略连字符；Hangul/CJK unified
名称大小写严格。CJK unified 允许 4–5 位大写十六进制（可以有前导零），其他派生名称采用
规范十六进制宽度。只返回单个 scalar，不接受 named sequences。以上是服务字符串转义的
显式查询契约，不是 Unicode loose name matching。Unicode 17 新增名称可能不被旧 Python
数据库识别，例如固定 Jinja oracle 环境不识别部分 Tangut 名称，不能据此删去官方数据。

构建后独立校验每个规范名称、别名及小写变体（Windows 的依赖 DLL 目录按本机 SDK 配置）：

```powershell
python unicode/test/verify_names.py build/Clang-Release/bin/salts_unicode.dll --dll-directory C:/projects/cpp/external/pkgs/salts/release/bin
```

本批事实：全量校验 320,564 次查询通过。MSVC Release DLL 从 59,904 增至 2,214,400 字节，
增量为 2,154,496 字节；不声称压缩或性能优化收益。

## NFC / IDNA 数据（Unicode 17.0.0）

原始文件未经修改，沿用 Unicode License v3。普通构建使用已提交的只读生成表，无运行时数据文件依赖。

| 文件 | 官方来源 | SHA-256 |
| --- | --- | --- |
| `DerivedNormalizationProps.txt` | https://www.unicode.org/Public/17.0.0/ucd/DerivedNormalizationProps.txt | `71fd6a206a2c0cdd41feb6b7f656aa31091db45e9cedc926985d718397f9e488` |
| `NormalizationTest.txt` | https://www.unicode.org/Public/17.0.0/ucd/NormalizationTest.txt | `5019ffd530751a741900c849c0e010332f142a3612234639bd200b82138a87db` |
| `DerivedJoiningType.txt` | https://www.unicode.org/Public/17.0.0/ucd/extracted/DerivedJoiningType.txt | `f39ebe974825d6736aee15582250307aa532b2cfab3caf3f86bd23fddc9c5c4d` |
| `Scripts.txt` | https://www.unicode.org/Public/17.0.0/ucd/Scripts.txt | `9f5e50d3abaee7d6ce09480f325c706f485ae3240912527e651954d2d6b035bf` |
| `IdnaMappingTable.txt` | https://www.unicode.org/Public/17.0.0/idna/IdnaMappingTable.txt | `87f05505dc026fdb2bff16132bdc68a8014675836882a9a2b1844540ad3be382` |
| `IdnaTestV2.txt` | https://www.unicode.org/Public/17.0.0/idna/IdnaTestV2.txt | `beb5d0be20e896189b03209a82fdc34f06351502bbd4b8e2523583fc2954d9cf` |

`generate_normalization_data.py` 还使用现有 `UnicodeData.txt`，哈希为
`2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c`，
来源 https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt。
Full_Composition_Exclusion 从 DerivedNormalizationProps 导出，覆盖完整排除集合。
Joining_Type 和 Script 仅在 IDNA 中生成 RFC 5892 所需的上下文判定表；CCC、Mark、NFC
与 Bidi 的运行时事实源归 Unicode，不复制进 IDNA。

```powershell
python unicode/tools/generate_normalization_data.py
python idna/tools/generate_idna_data.py
python unicode/tools/generate_normalization_data.py --check
python idna/tools/generate_idna_data.py --check
```

两个生成器首先校验上述固定输入哈希，生成 LF/ASCII 表，不查询 Python 的 Unicode
数据库。`--check` 核对提交的表与重建结果一致。源数据/版本升级须同时更新哈希、公开
版本/profile、生成表和一致性测试。完整 NFC/IDNA 测试归正式 CTest，不在生成器中运行库。
