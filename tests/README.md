# 测试

## 单元检查

```sh
python3 tests/run_host_checks.py
```

26 项检查覆盖 Mono AAC 布局、两种采样率、其他格式／错误的透传、无效指针、输出容量和缓冲区前后哨兵。

## 本机 CoreAudio 集成

需要 macOS 音频服务及自行提取的两个原始 AAC 样本。样本不在项目中，也不作为构建或单元测试的前提。

样本目录下的文件名：

- `01_原始AAC_8000Hz_约8kbps.m4a`
- `02_原始AAC_11025Hz_约8kbps.m4a`

```sh
python3 tests/run_host_checks.py --native-audio --sample-dir /path/to/local/samples
```

测试先确认未修复查询返回 `!siz`，再向专用测试进程加载使用同一源码编译的 macOS 模块，核对 Mono 布局并离线解码出非零 PCM。没有调用地址或 UUID 绕过宏。环境原生已不再返回该错误时，此复现测试会失败，需要进一步确认系统差异。

这不替代游戏设备测试，也不证明模块在所有签名／注入方式下都可加载。测试报告自动写入 `测试结果.json`，构建脚本将它纳入本地校验清单和完整发布包。
