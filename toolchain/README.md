# 离线 ARM musl 工具链

本目录提供客户应用和 Demo 使用的固定交叉编译工具链。客户不得用其他同名 GCC、系统自带 ARM 工具链或在线下载版本替换本工具链。

## 目录

```text
toolchain/
├── README.md
└── packages/
    ├── workcard-arm-linux-musleabi-toolchain-20250305.tar.gz
    └── workcard-arm-linux-musleabi-toolchain-20250305.tar.gz.sha256
```

工具链主要版本：

- GCC 10.3.0。
- GNU Binutils 2.41。
- musl 1.2.3。
- Linux 5.10 目标头文件。
- 32 位 ARM EABI5 soft-float ABI。

## 安装

必须在原生 Ubuntu x86_64 或 Windows WSL2 Ubuntu 中执行：

```bash
cd work_card_SDK
./scripts/setup_dev_env.sh
```

默认安装位置是：

```text
$HOME/.local/workcard-toolchain/gcc-20250305-arm-v01c02-linux-musleabi/arm-v01c02-linux-musleabi-gcc
```

该位置位于 Linux 文件系统，不需要 `sudo`。不要把工具链直接解压到 Windows 的盘符目录，因为 Windows NTFS 挂载方式可能破坏 Linux 符号链接和执行权限。

企业需要统一安装路径时，可以在 Linux 环境中明确设置：

```bash
WORKCARD_TOOLCHAIN_INSTALL_BASE=/opt/workcard-toolchain ./scripts/setup_dev_env.sh
```

写入 `/opt` 时由客户根据本机权限预先创建目录并配置权限，本 SDK 安装脚本不会自行提升到 root 权限。

## 完整性

安装脚本会在解压前校验工具链包的独立 SHA-256。整个 SDK 的 `MANIFEST.sha256` 还会再次覆盖工具链包和独立哈希文件。

工具链的授权和第三方开源许可边界见 `licenses/TOOLCHAIN_NOTICE.md` 与正式商业合同。
