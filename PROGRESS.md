# Crimson Desert Tweaks — 当前状态

## 开发范围

- 当前本地项目仅包含盾牌隐藏，开发暂停。
- 2026-11-20 清理已停用的功能和相关资料。专用文件及本次删改的共享文件原副本已移入 D 盘回收站。
- 本地 `main` 已接入 [GitHub 仓库](https://github.com/Szy-Cathay/CrimsonDesertTweaks)并跟踪 `origin/main`。

## 功能状态与证据

| 功能 | 当前状态 | 技术依据 |
|------|----------|----------|
| Shield Hide | 已发布。本地保留 v1.13.00 的 ReserveSlot/FakeEquip 扫描路径，较新版本尚未复验 | [盾牌技术记录](docs/reverse-engineering/shield-hide.md) |

## 本次验证（2026-11-20）

- MSVC x64 Release 构建成功。CTest 1/1 通过，覆盖默认 INI 的 `[Shield] Enable=1` 及总开关和盾牌开关的启停判断。
- ASI 与默认 INI 同层生成于 `build/Release/`，输出 INI 与项目模板一致。未部署或进行游戏内测试。

## 工具与分析资料（2026-09-05）

- 当时安装的是 Ghidra 12.1.3 + Ghidra MCP 6.0.0，历史入口见 [项目规则](AGENTS.md#逆向工具与证据)。
- 扩展已针对 Ghidra 12.1.3 构建；新 bridge 依赖检查、MCP 协议连接、x64 DLL 元数据/函数读取和函数反编译验证通过。验证不涉及游戏功能验收。
- `D:\ghidra project\CrimsonDesertv1.13.00.gpr` 和同名 `.rep` 目录保留，约 5.61 GiB，供旧版对照；该数据库未因工具升级而变成新版游戏分析。
- 旧版 Ghidra 和 MCP 清理后，核验了新版工具与启动配置的 SHA256，以及原分析工程文件清单、大小和修改时间。

## 待确认事项

- 当前游戏版本的盾牌隐藏效果仍待实测。
- 如用户恢复维护，先取得当前版本证据并重新核对签名/结构，再安排有针对性的游戏验证。
