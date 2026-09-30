# 技术说明

## 声道布局故障

原始 AAC 可以通过系统“文件”App 播放。1.7.4 诊断确认，旧音频引擎在 AAC 初始化时查询声道布局返回 `!siz`（561211770），在创建解码队列前停止。提供标准单声道布局后，诊断 v2 经用户设备确认恢复声音。

`src/PVZ2AudioFix.c` 只替换主程序两个 API 的可写导入槽。先调用原始系统 API，只有指定错误、单声道 AAC、8000/11025 Hz 的格式匹配时才补充布局。Get 接口还要求非空指针和足够容量，并保留输出前后边界。

1.1.0 删除了 1.7.4 的固定返回地址检查。主程序通过被替换导入槽发出的符合条件的查询都可触发修复；没有进程级全局系统 API hook，也没有依赖 Substrate 的 hook 函数。主程序中需要存在传统符号／间接导入表，模块通过 `MH_EXECUTE` 寻找它，不假定它在 dyld 列表索引 0。

## Impactor 2.6.5

官方 2.6.5 的 [inject_dylib](https://github.com/claration/Impactor/blob/v2.6.5/crates/plume_utils/src/tweak.rs#L271) 将加载路径固定构造为 `@rpath/文件名`，框架采用 `@rpath/框架名/可执行文件名`。[Mach-O 注入实现](https://github.com/claration/Impactor/blob/v2.6.5/crates/plume_core/src/utils/macho.rs#L146) 写入 `LC_LOAD_WEAK_DYLIB`，没有同时添加 Frameworks 的 `LC_RPATH`。

已检查的一次 1.7.4 注入缓存没有 `LC_RPATH`，而修复 dylib 位于 Frameworks。该缓存包含旧版 1.0 模块，因此它只证明那次缓存输出的加载命令结构，不能代表后来设备里所有安装副本。

选项实现见 [signer.rs](https://github.com/claration/Impactor/blob/v2.6.5/crates/plume_utils/src/signer.rs)：最低系统兼容选项仅修改 `MinimumOSVersion`；添加任何 tweak 时本来就会安装 ElleKit；这些选项没有补充 dylib 搜索路径。更改修复 dylib 自身的 install name 也不会改变该工具固定写入的加载路径。

可解析的直接相对路径，或带正确 Frameworks 搜索目录的 `@rpath`，才满足加载条件。本项目没有修改 Impactor，也不声称取消游戏版本限制就能修复注入路径问题。

## 验证边界

本机测试验证了错误／格式筛选、输出边界、原始 AAC 解码以及打包／签名结构。用户反馈记录了设备恢复声音。严格 ad-hoc 签名检查通过并不代表已获得 iOS 设备安装信任；侧载工具需要重新签名，越狱环境需要可用的注入器。
