# UEEditorAutomation Agent Instructions

## 本地项目硬约束

- 本项目的所有编译行为都必须由用户人工完成。
- AI 不允许主动执行任何编译、构建、UBT、UAT、GenerateProjectFiles、启动编辑器触发编译、Live Coding 编译等命令。
- AI 可以读取源码、分析错误、修改源码、解释编译报错、给出建议。
- 当需要验证编译时，AI 必须提示用户人工编译，并等待用户贴出新的编译结果。
- 禁止为了“验证一下”自行运行 `Build.bat`、`UnrealBuildTool.exe`、`RunUAT.bat`、`GenerateProjectFiles.bat`、`UE4Editor.exe` 或任何等价构建/启动命令。
