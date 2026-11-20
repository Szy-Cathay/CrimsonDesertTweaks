[English](README.md) | [简体中文](README.zh-CN.md)

# Crimson Desert Tweaks / 红色沙漠功能微调

[![Language](https://img.shields.io/badge/language-C%2B%2B23-%23f34b7d)](https://en.cppreference.com/w/cpp/23)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078d7)](https://www.microsoft.com/windows)
[![Game](https://img.shields.io/badge/game-Crimson%20Desert-orange)](https://crimsondesert.pearlabyss.com/)

**Crimson Desert** 的 ASI 模组，用于双持武器时隐藏背盾。

当前暂停开发。盾牌隐藏代码适配游戏 v1.13.00，后续版本的兼容性仍待验证。详见[项目进度](PROGRESS.md)和[构建与测试约定](AGENTS.md)。

---

## 功能列表

| 功能 | 说明 |
|------|------|
| **盾牌自动隐藏** | 双持武器时自动隐藏背后的盾牌。已适配 v1.13.00 的 ReserveSlot/FakeEquip key-load 路径。 |

## 安装方法

通过 [DMM](https://www.nexusmods.com/crimsondesert/mods/633) 安装：

1. 将下载的压缩包直接拖入 **DMM** 窗口
2. 切换到 **ASI 模组** 标签页
3. 勾选 **crimson-desert-tweaks** 即可启用
4. 点击 **配置** 按钮可自定义调整各项设置

## 配置文件

编辑 `crimson-desert-tweaks.ini`。日志设置保存后自动重载。启停盾牌隐藏功能需重启游戏。

```ini
[General]
Enabled=1                   # 总开关
LogEnabled=1                # 日志开关
Verbose=0                   # 详细日志（调试时设为 1）
MaxLogLines=2000
InitDelayMs=3000

[Shield]
Enable=1                    # 盾牌自动隐藏
```

## 构建

**环境要求**：Visual Studio 2022+、CMake 3.20+

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
```

Release 输出只保留在 `build/Release/`：

```text
build/Release/crimson-desert-tweaks.asi
build/Release/crimson-desert-tweaks.ini
```

INI 会从仓库根目录默认模板复制，确保 ASI 和默认配置文件始终同层生成。安装到游戏目录或 DMM 模组目录由用户手动完成。

## 技术栈

- C++23 / MSVC x64
- [SafetyHook](https://github.com/cursey/safetyhook) — Hook 框架
- [Zydis](https://github.com/zyantific/zydis) — 反汇编引擎

## 致谢

### 开发
- 本项目由 **Szy-Cathay** 主导，代码与逆向记录由 AI 编程协助产出。

### 上游代码
- [Orcax-1399/CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) — 原始模组
- [sreerajjayaraj/player-status-modifier](https://github.com/sreerajjayaraj/player-status-modifier) — 中间分支，本项目直接来源

### 技术库
- [SafetyHook](https://github.com/cursey/safetyhook) — Hook 框架
- [Zydis](https://github.com/zyantific/zydis) — 反汇编引擎

### 逆向参考
- [tkhquang/CrimsonDesertTools](https://github.com/tkhquang/CrimsonDesertTools)，本地源码副本 `D:\AI Agent Resources\GitHub\CrimsonDesertTools-main` — ASI 项目结构与 Crimson Desert 运行时 Hook 参考
- [Orcax-1399/CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier)，本地源码副本 `D:\AI Agent Resources\GitHub\CrimsonDesert-player-status-modifier-master` — player-status ASI 源码与 SafetyHook/CMake 模式参考
- [Orcax-1399/CrimsonDesert-parry-evade](https://github.com/Orcax-1399/CrimsonDesert-parry-evade)，本地源码副本 `D:\AI Agent Resources\GitHub\CrimsonDesert-parry-evade-master` — Crimson Desert hook/scanner 参考
- [Ronibuilds/CrimsonDesertMods](https://github.com/Ronibuilds/CrimsonDesertMods)，本地源码副本 `D:\AI Agent Resources\GitHub\CrimsonDesertMods-main` — Crimson Desert 模组源码参考
- [FLiNG Trainer（风灵月影）](https://flingtrainer.com/trainer/crimson-desert-trainer/) — 修改器参考
- [bbfox0703](https://www.nexusmods.com/crimsondesert/mods/64) — Cheat Engine 表格 (1.12.02)

[盾牌隐藏技术记录](docs/reverse-engineering/shield-hide.md)
