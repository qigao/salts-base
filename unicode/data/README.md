# Unicode 17.0.0 原始数据

原始 UCD、Emoji 和 IDNA 文件按需从 [Unicode 官方版本目录](https://www.unicode.org/Public/17.0.0/)
下载，不纳入源码仓库。[sources.lock.json](sources.lock.json) 记录每个文件的固定 URL 和
SHA-256，升级需同时审查版本、哈希、生成表、公开契约及一致性测试。不得使用 `latest`。
`emoji-data.txt` 的官方文件比旧仓库副本多两处注释行尾空格；锁定官方原始字节，属性数据相同。

默认缓存为仓库根目录下的 `build/unicode-data/17.0.0/`，多个编译器共用同一份数据。
`SALTS_UNICODE_DATA_DIR` 可指定已有缓存；每次使用前校验哈希，损坏的缓存明确失败，
需移除对应文件后重新下载。下载使用 TLS 校验、文件锁和校验后的原子发布，失败不会
留下可被当作有效数据使用的文件。有效缓存可离线使用。

普通库构建直接编译已提交的 `unicode/src/*_data.h`，不下载原始数据，也不需要 Python。
`BUILD_TESTS=ON` 时，configure 自动恢复 Unicode 一致性测试需要的七个官方文件。
`BidiParagraphTest.txt` 是项目维护的 28 条派生用例，保留于 `unicode/test/data/`。
生成表和运行时行为不因本次数据存放方式调整而改变；运行时无网络或外部数据文件依赖。

## 恢复全部生成器输入

从仓库根目录运行（CMake 3.25+）：

```powershell
cmake -DSALTS_UNICODE_DATA_DIR=build/unicode-data/17.0.0 -P unicode/data/Download.cmake
```

下载失败会终止，诊断中保留文件 URL 和错误。受限网络需配置所在环境的 HTTPS 代理及
可信 CA；CMake 支持 `CMAKE_TLS_CAINFO`。不得关闭 TLS 校验来绕过证书错误。
离线构建前可在联网环境恢复后，将完整缓存复制到目标环境并通过该变量指定。

## 重新生成

恢复后，从仓库根目录运行（Python 3.10+）：

```powershell
python unicode/tools/generate_names.py --check
python unicode/tools/generate_casefold_data.py --check
python unicode/tools/generate_normalization_data.py --check
```

以上生成器省略 `--check` 时更新已提交的表；使用自定义缓存时传入 `--data-dir <目录>`。
`generate_case_data.py` 同样接受 `--data-dir`。其余生成器继续通过既有的
`--word-break`、`--grapheme-break`、`--derived-core`、`--derived-bidi-class`、
`--line-break`、`--unicode-data`、`--east-asian-width` 和 `--emoji-data` 参数指定缓存内
输入文件，并用 `--output` 指定生成头。生成器不查询宿主 Python 的 Unicode 数据库。
名称全量检查工具 `unicode/test/verify_names.py` 也接受 `--data-dir`。

名称查询契约保持不变：普通名称和别名 ASCII 大小写不敏感，但不折叠空白或忽略连字符；
Hangul/CJK unified 名称大小写严格，只返回单个 scalar，不接受 named sequences。

Unicode License V3 完整保留在 [LICENSE.txt](LICENSE.txt)，随库安装到
`share/licenses/SaltsCore/unicode/`。生成表和下载数据均遵循该许可。
