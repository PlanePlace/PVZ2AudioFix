# PVZ2AudioFix

修复旧版 iOS《植物大战僵尸 2》部分背景音乐、戴夫语音无法播放的问题。提供 **ARM64 独立 dylib**、**rootful / rootless 越狱 tweak**，源码采用 MIT 许可证。

An ARM64 audio compatibility patch for legacy Plants vs. Zombies 2 on iOS. It supplies a standard mono channel layout when a specific CoreAudio metadata query fails. Standalone dylib and rootful/rootless tweak packages share the same source.

## 下载与已报告兼容性

前往 [Releases](https://github.com/PlanePlace/PVZ2AudioFix/releases) 下载。普通注入使用 `PVZ2AudioFix.dylib`；越狱用户按环境选择对应的 deb。完整 zip 包含源码、构建说明和三种交付文件。

用户于 2026-09-30 报告 **iOS 中文版 1.7.1、1.7.4、2.5.0** 测试通过，测试涉及越狱、自签名及 Apple 账户自购的游戏副本。各版本与安装方式的具体对应关系尚未提供，详见 [兼容性记录](docs/COMPATIBILITY.md)。

当前 1.1.0 不限制游戏版本、UUID 或 1.7.4 的固定调用地址。二进制仍仅支持 ARM64 原生进程，其他版本能否生效取决于是否遇到相同故障、导入表是否兼容、模块是否成功加载。

## 安装

### IPA 注入

1. 备份存档，从未叠加旧音频补丁的游戏副本开始。
2. 用有 dylib 注入功能的签名工具选择 `PVZ2AudioFix.dylib`，将它注入游戏主程序并重新签名。替换旧模块，避免重复注入。
3. 尽量沿用原签名账号及 bundle ID，安装后彻底退出并重新启动游戏。
4. 测试背景音乐、戴夫语音和普通音效。自购或自签名属于游戏来源／安装方式，不能单独证明补丁已经注入。

注入工具需要同时复制文件并写入主程序加载命令。如果放在应用的 `Frameworks` 目录，可用 `@executable_path/Frameworks/PVZ2AudioFix.dylib`。使用 `@rpath/PVZ2AudioFix.dylib` 时，主程序必须有能解析到该目录的 `LC_RPATH`。

**Impactor 2.6.5 已发现兼容性限制**：已检查的一次 1.7.4 注入缓存包含弱加载的 `@rpath/PVZ2AudioFix.dylib`，却没有所需的 `LC_RPATH`，不能保证插件被加载。其他 IPA 应分别核对加载路径。勾选“支持旧版本系统”或“用 ElleKit 替换 Substrate”没有补上该路径；此项目未修改 Impactor。详见 [技术说明](docs/TECHNICAL.md)。

### 越狱 tweak

| 文件 | 环境 | 安装目录 |
|---|---|---|
| `PVZ2AudioFix_1.1.0_rootful.deb` | rootful | `/Library/MobileSubstrate/DynamicLibraries/` |
| `PVZ2AudioFix_1.1.0_rootless.deb` | 标准 rootless | `/var/jb/Library/MobileSubstrate/DynamicLibraries/` |

通过包管理器安装其中一个包，彻底退出并重新启动游戏；按注入器要求执行 respring。两个包实际都只有 ARM64 二进制，依赖支持标准过滤 plist 的 `mobilesubstrate | ellekit` 注入器。过滤进程名为 `PvZ2`，若其他版本采用不同主程序名称，需调整过滤配置。

全局 tweak 与 IPA 内注入版本应只保留一种。若同时存在诊断模块 `PVZAudioProbe.dylib`，本模块会停用自身。

## 修复原理与范围

此次排查中，原始低码率 AAC 在同一 iPad 的系统“文件”App 中能正常播放。游戏通过 `AudioFileGetPropertyInfo` 查询 `kAudioFilePropertyChannelLayout` 却返回 `!siz`（561211770），旧音频引擎因而停止初始化。证据支持此处的元数据兼容性问题，并不能证明“iOS 14.4 以后全面禁止低码率 AAC”。

补丁按 API 名称替换游戏主程序可写 `__DATA` 内的 `AudioFileGetPropertyInfo`、`AudioFileGetProperty` 导入指针。只有原始错误为 `!siz`、音频为 **单声道 AAC，采样率 8000 或 11025 Hz**，且输出缓冲区符合要求时，才提供标准 32 字节 Mono 声道布局。其他错误、格式和系统原本成功的结果保持原样。

模块没有音频重编码、资源替换、AudioQueue hook、定时器或 PCM 采样。每次启动最多记录四条兼容处理日志。它不处理功夫世界 Boss 战原版已有的闪退。

## 日志与撤销

设备控制台搜索 `PVZ2AudioFix 1.1.0`：

- `enabled (... import slots; version/UUID unrestricted)`：导入指针替换成功，还需验证实际声音。
- `mono layout fallback`：已触发兼容处理。
- `disabled (missing Mach-O metadata)` / `disabled (unexpected import slots)`：当前主程序导入信息不兼容。
- `disabled (PVZAudioProbe already present)` / `disabled (fix already installed)`：检测到重复模块。

卸载 tweak 后重新启动游戏可撤销全局注入。IPA 注入需重新签名安装未注入副本；重装前备份存档。

## 构建与测试

直接构建需要 macOS、Python 3、完整 Xcode（含 iPhoneOS SDK）和 `dpkg-deb`。脚本默认使用 `xcode-select -p` 指向的 Developer 目录；如果当前是 Command Line Tools 或需要指定另一份 Xcode，可设置 `PVZ2_XCODE_DEVELOPER`。

```sh
export PVZ2_XCODE_DEVELOPER=/Applications/Xcode.app/Contents/Developer
python3 tests/run_host_checks.py
python3 build_release.py
```

产物写入项目根目录，临时文件在 `build/`。独立 / rootful 构建目标为 iOS 14.0，rootless 为 15.0；本次设备故障最初报告从 iOS 14.4 开始。无需游戏 IPA 即可构建。

可选 Theos 构建入口：

```sh
make clean package FINALPACKAGE=1
make clean package FINALPACKAGE=1 THEOS_PACKAGE_SCHEME=rootless
```

直接构建已在本机验证；Theos 入口尚未实际运行。26 项修复条件／缓冲区检查通过，本机 CoreAudio 测试使用同一源码、没有测试专用调用地址绕过，两个原始采样率的 AAC 均得到非零 PCM。原始音频样本不随项目分发；集成测试方法见 [tests/README.md](tests/README.md)。

## 反馈与许可证

提交 Issue 时说明游戏地区与版本、设备和系统、安装／注入方式，以及背景音乐、戴夫语音、普通音效的实际表现。贡献代码请保留格式、错误和缓冲区检查。

项目源码采用 [MIT](LICENSE) 许可证。项目不包含游戏本体、游戏资源或设备日志，与 PopCap / EA 无隶属关系；游戏及相关商标的权利属于其各自权利人。
