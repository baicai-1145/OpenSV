# OpenSV

基于 C++20 / JUCE 的SV歌声合成引擎实现

## 构建与运行

Windows 需要 Visual Studio C++ 工具、CMake、Ninja 和 Git。首次构建会下载固定版本的 JUCE 9.0.3。

```powershell
./tools/build.ps1 -Run
```

程序输出到 `build/Release/OpenSV_artefacts/Release/OpenSV.exe`，可传入 `.svp` 路径打开工程。调试构建使用 `-Configuration Debug`；macOS 尚未验证。

## 功能

- SVP 工程读写、MIDI 导入导出、撤销与重做。
- 音符编辑、批量填入歌词、音高偏移编辑、钢琴键试听。
- 自动音高预测、音高线与波形显示、短语级增量合成、WAV 导出。
- 多轨声库与混音设置、可折叠和调整高度的编辑区域、播放自动翻页。

## 使用

1. 点击音轨左侧的声库名称，选择 `voice.nofs`；在属性面板设置演唱语言和 `clf-data` 词典目录。
2. 双击网格添加音符，双击音符编辑歌词；选中多个音符后，右键“填入歌词…”或按 Ctrl+L 批量填写。
3. 按空格播放或暂停，使用 WAV 导出保存歌声。中键拖动视野，Ctrl + 滚轮缩放，Shift + 滚轮横向滚动。

## 命令行

```powershell
./tools/build.ps1 -Target OpenSVVoice
& './build/Release/OpenSVVoice_artefacts/Release/opensv-voice.exe' --help
```

使用 `render-project <project.svp> <output.wav>` 渲染已配置声库的工程；其他模型分析与诊断命令见 `--help`。
