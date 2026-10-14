# OneKeyPotion

适配 S4A21 客户端的一键喝药插件，使用游戏原生界面配置每个角色的药剂方案。

## 功能

- 每个角色保存药剂选择、顺序和喝药快捷键。
- 每页显示32个方案槽位，编号随页码切换。
- 持续药剂检查自身药效，有效时跳过，效果结束后再补；次数型效果沿用原生记录。
- 没有持续效果的恢复、解除异常或即时重置药剂，每次触发尝试一次。
- 复合药的属性不会代替另一瓶药的自身状态；明确禁止叠加的组遵循原生规则。
- 结果提示列出已使用和使用失败的药剂，冷却或已有药效跳过不列为失败。
- 药剂目录及状态机制内置于插件。

## 安装

从仓库 Releases 下载玩家包，将 `OneKeyPotion.dll`、`OneKeyPotion.xui`、`OneKeyPotion.ini` 放入适配客户端目录。

客户端需要提供 `GameNative.dll` 的 `ClientPatchGetGameNativeApi` 接口。在 `GameGaurd.ini` 的 `[Plugins]` 下使用空闲插件槽登记 `OneKeyPotion.dll`，并保证它在 `GameNative.dll` 后加载。例如：

```ini
[Plugins]
Plugin8=OneKeyPotion.dll
```

不要覆盖已有的其他插件登记。原生接口地址只适用于配套客户端布局。

## 配置

```ini
[General]
Enabled=1
GuiKey=F9
```

`Enabled` 为启用开关，`0` 停用，`1` 启用。

`GuiKey` 为界面打开/关闭单键。可填写 `A`、`9`、`Space`、`Enter`、`Esc`、`Up`、`PageUp`、`F1` 至 `F24`、`Numpad0` 至 `Numpad9` 等键名，也可使用 `VK:192` 或 `0xC0` 等虚拟键码。

支持单独的 Shift、Ctrl、Alt 和左右键名，不支持 `Ctrl+A` 等组合键。系统保留键需能被游戏接收；文字输入时不触发。修改INI后重启客户端。

喝药键在GUI内设置，不能与窗口键相同。默认喝药键为F8；窗口键选择F8时使用F7作为备用喝药键。

## 界面操作

右侧 `+` 添加药剂。点击左侧编号选中，再点击另一格换位，也可以删除或清空。点击保存后按喝药键执行已保存方案。

新角色没有预设药剂。角色方案保存在自动生成的 `OneKeyPotionData` 目录中，替换插件文件时保留该目录。

## 构建

需要 Visual Studio 2022、MSVC v143、Windows SDK 和C++桌面开发组件。

打开 `source/OneKeyPotion.vcxproj`，选择 `Release / Win32`；或在VS开发者命令提示符执行：

```bat
MSBuild source/OneKeyPotion.vcxproj /p:Configuration=Release /p:Platform=Win32
```

DLL输出到 `build/bin/OneKeyPotion.dll`，中间文件位于 `build/obj`。配套XUI和INI位于仓库根目录。

## 源码

| 文件 | 用途 |
| --- | --- |
| `OneKeyPotion.cpp` | 插件加载、执行调度和结果提示 |
| `PotionController.inl` | 角色保存、分页及GUI交互 |
| `BuiltinPotions.h` | 内置药剂目录和机制映射 |
| `NativeApi.h` | 原生药效、冷却、背包及使用接口 |
| `PotionUi.cpp`、`PotionUi.h` | 原生界面控件绑定和事件处理 |
| `GameNativeApi.h` | 客户端插件接口声明 |
| `KeyboardKey.h` | 键名解析和键盘消息匹配 |

## 许可与来源

此仓库未授予开源许可证。界面桥接代码改编自 [RewIO S4A21ClientPatch](https://gitgud.io/rewio/S4A21ClientPatch) 的 `EquipmentSwapNativeUi.cpp`，代码使用及分发应遵循适用的上游授权条件。
