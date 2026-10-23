# libssh 平台依赖

两个平台的头文件、链接库和运行库分别保存，工程通过 `libssh.pri` 自动选择：

```text
lib/
├── macos-arm64/
│   ├── include/libssh/
│   ├── lib/
│   │   ├── libssh.dylib -> libssh.4.12.0.dylib
│   │   ├── libssh.4.dylib -> libssh.4.12.0.dylib
│   │   ├── libssh.4.12.0.dylib
│   │   └── libcrypto.3.dylib
│   ├── licenses/
│   └── SOURCES.json
└── windows-x64/
    ├── include/libssh/
    ├── lib/libssh.dll.a
    ├── bin/
    │   ├── libssh.dll
    │   ├── libcrypto-3-x64.dll
    │   ├── libwinpthread-1.dll
    │   └── zlib1.dll
    ├── licenses/
    └── SOURCES.json
```

## macOS arm64

- libssh 0.12.2：使用[官方源码](https://www.libssh.org/files/0.12/libssh-0.12.2.tar.xz)在本机编译。
- OpenSSL 3.6.2：附带本机 arm64 `libcrypto.3.dylib`。
- 当前二进制要求 macOS 26.0 或更新版本。
- libssh 通过 `@loader_path/libcrypto.3.dylib` 加载相邻的 OpenSSL，避免引用原 Homebrew 安装路径；修改后的库已重新进行本地签名。
- 本机已实际加载该库并完成程序构建、串口伪终端和回环 SSH 测试。

## Windows x64 / MinGW

- 使用 MSYS2 `mingw64` 的预编译动态库，适用于 Qt 的 MinGW 64 位 Kit。
- libssh 0.12.2-1、OpenSSL 3.6.5-1、libwinpthread 14.0.0.r495.g63ebe6cdb-2、zlib 1.3.2-2。
- `libssh.dll.a` 是 MinGW 导入库；MSVC Kit 需要另外提供 MSVC 构建的 `ssh.lib` 及相应 DLL。
- 已核对四个原始包的官方 SHA256、所有 DLL 的 x64 格式、程序使用的 SSH 导出函数，以及非系统依赖 DLL 的完整性。
- 当前主机为 macOS，未在 Windows 上运行这些 DLL，也未执行 Windows EXE 构建。

原始包来源：

- [MSYS2 libssh](https://packages.msys2.org/packages/mingw-w64-x86_64-libssh)
- [MSYS2 OpenSSL](https://packages.msys2.org/packages/mingw-w64-x86_64-openssl)
- [MSYS2 libwinpthread](https://packages.msys2.org/packages/mingw-w64-x86_64-libwinpthread)
- [MSYS2 zlib](https://packages.msys2.org/packages/mingw-w64-x86_64-zlib)

`SOURCES.json` 记录来源、包版本、原始包 SHA256 和各文件校验值。`licenses` 保存相应许可文本。
