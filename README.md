# 串口与 SSH 日志记录工具

独立的 Qt Widgets / C++ 桌面程序，目标平台为 Windows。串口和 SSH 使用独立页签，可同时记录。

## 功能

- 串口：选择串口号、波特率、数据位、校验位、停止位和流控，持续接收文本。
- SSH：使用密码认证，打开交互式 Shell 和 PTY；直接在终端输入命令，支持回车、退格、方向键、Tab、Ctrl+C 和粘贴。
- 每个连接独立选择日志目录、分文件间隔（秒 / 分钟 / 小时）和时间戳开关。
- 间隔到达时创建下一个文件，期间持续接收。即使没有数据，也会按间隔创建文件。
- 分文件间隔可在记录过程中修改，修改后重新计时；时间戳开关从下一行生效。
- 设备编码可选择 UTF-8 或 GB18030 / GBK，日志统一保存为 UTF-8。
- 停止和关闭窗口时完成剩余文本写入，再释放连接。

## 日志内容

SSH 日志保留远端提示符、命令回显和输出，不添加 `INPUT`、`OUTPUT` 或其他方向标签：

```text
device# ./xxx
程序启动
设备连接成功
device# ls
config.ini  xxx  logs
device#
```

开启时间戳时，在每行开头添加电脑本地时间，例如：

```text
[2026-10-23 14:30:01.125] device# ./xxx
[2026-10-23 14:30:01.162] 程序启动
```

时间取该行首次接收到可显示字符的时刻，精确到毫秒。终端界面保持原有文本，时间戳仅写入文件。

命令由远端终端回显后写入，程序不做本地重复回显。SSH 登录密码不会写入日志；远端关闭回显时（例如密码输入），对应按键也不会进入日志。

文件名包含来源和创建时间，例如 `ssh_192.168.1.10_22_20261023_143000_125.log`。已存在的同名文件不会被覆盖。

文件切换允许将同一行拆到相邻文件，保证接收连续性；不为切换文件额外插入换行或重复提示符。关闭时间戳时，按创建顺序拼接文件可以恢复连续的终端文本。

## 使用

1. 打开串口或 SSH 页签，填写连接参数。
2. 选择保存目录、文本编码、分文件间隔和时间戳选项。
3. 点击“连接并记录”。SSH 首次连接时核对主机指纹，再选择是否信任。
4. SSH 连接成功后点击黑色终端区域，直接输入命令。Ctrl+C 在选中文字时执行复制，否则发送中断；Ctrl+V 或右键菜单可粘贴。
5. 点击“停止”结束本次记录。日志文件路径显示在终端上方，写入或连接失败会显示具体错误并停止记录。

默认日志位于程序目录的 `logs/serial` 和 `logs/ssh`。主机密钥保存到程序目录的 `known_hosts`，密码不保存。请将整个程序目录放在当前用户可写的位置。

## Windows 构建与部署

需要：

- Qt 5.15.x 的 MinGW 64 位套件，包含 Qt Widgets、Qt Serial Port；使用与该 Qt Kit 配套的 MinGW 工具链。Qt 6 还需安装 Core5Compat。
- 项目已附带 Windows x64 MinGW 动态 libssh，默认使用 `lib/windows-x64`，其中包括头文件、导入库和运行依赖 DLL。

Windows 预编译包来自 [MSYS2 官方仓库](https://packages.msys2.org/packages/mingw-w64-x86_64-libssh)，SHA256 已按官方包页面核对。两个平台的布局和依赖来源见 [lib/README.md](lib/README.md)。

在 PowerShell 中进入本项目目录并执行（路径替换为实际 Qt 安装位置）：

```powershell
.\build-windows.ps1 -QtBin "C:\Qt\5.15.2\mingw81_64\bin" -CompilerBin "C:\Qt\Tools\mingw810_64\bin"
```

构建中间文件保存在项目父目录的 `.codex-tmp/serialssh-windows-build`，完整运行目录生成到本项目的 `dist`。发布时复制整个 `dist`，其中包括 EXE、Qt 平台插件及 libssh / 加密库 DLL。

也可以用 Qt Creator 打开 `SerialSshLogger.pro`，选择 MinGW 64 位 Kit，工程会自动引用 `lib/windows-x64`。构建目录设置到工作区根目录下的 `.codex-tmp`。

如果使用 MSVC Kit，需要提供 MSVC 编译的 libssh，并用 `-LibsshRoot` 或 qmake 参数 `LIBSSH_ROOT=依赖安装目录` 指定；项目附带的 MinGW 导入库不能作为 MSVC 的 `ssh.lib` 使用。libssh 官方提供了 [vcpkg 安装说明](https://www.libssh.org/get-it/)。

macOS arm64 默认引用 `lib/macos-arm64`；当前附带的本机库要求 macOS 26.0 或更新版本。

## 本地验证

测试工程为 `tests/tests.pro`，使用 Qt Test。构建方式与主程序相同，运行前设置 `LOGGER_TEST_ROOT`，指向当前工作区 `.codex-tmp` 下的测试输出目录。

测试覆盖：中文编码跨接收批次、终端控制序列和命令编辑、提示符和回显、时间戳、分文件连续性、动态间隔、写入失败、按键发送、显示容量和界面截图。

Unix 主机还可验证伪终端串口；设置 `LOGGER_PYTHON` 为已安装 Paramiko 的 Python 解释器、`LOGGER_SSH_FIXTURE` 为 `tests/ssh_fixture.py` 的绝对路径后，可验证回环 SSH 连接、主机密钥确认、命令回显、中断、分文件和认证失败。SSH 测试服务只监听 `127.0.0.1`。

## 当前范围

本版记录文本日志，处理常见颜色、标题、退格和行内编辑控制序列；采用面向普通命令日志的终端，未实现 `vim`、`top` 等全屏终端仿真。串口二进制协议的逐字节原始保存不在本版范围内。

界面保留最近 10000 行，文件记录独立于显示缓存。约每 200 ms 刷新文件缓冲，这不等同于物理磁盘强制同步，断电前最后一部分数据仍可能丢失。

接口依据：[Qt 串口示例](https://doc.qt.io/qt-6/qtserialport-terminal-example.html)、[libssh 交互式终端](https://api.libssh.org/stable/libssh_tutor_shell.html)。
