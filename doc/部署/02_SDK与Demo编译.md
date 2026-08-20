# SDK 与 Demo 编译

## 1. 客户交付物

客户无需编译底层 SDK 服务。正式目录已经提供：

```text
lib/arm-linux-musleabi/libworkcard_sdk.so.1.0.0
bin/arm-linux-musleabi/workcard-sdk-service
```

两个文件均应显示为 ARM EABI5、musl、stripped。

客户只需要使用随附工具链编译 Demo 和自己的应用程序。

## 2. Windows 客户编译 Demo

第一步，在 Windows PowerShell 中进入 WSL：

```powershell
wsl
```

第二步，在 WSL Bash 中进入 SDK 根目录：

```bash
cd /mnt/d/客户目录/work_card_SDK
```

第三步，首次使用时安装 SDK 随附工具链：

```bash
./scripts/setup_dev_env.sh
```

第四步，编译 Demo：

```bash
./scripts/build_demo.sh
```

PowerShell 不支持 Bash 的 `export` 命令，也不能直接访问 WSL 的 Linux 工具链。不要在 `PS C:\...>` 提示符下执行 `.sh` 脚本。

## 3. Ubuntu 客户编译 Demo

在 Ubuntu Bash 中执行：

```bash
cd /客户实际路径/work_card_SDK
./scripts/setup_dev_env.sh
./scripts/build_demo.sh
```

编译脚本会自动查找当前 Linux 用户安装的固定工具链，不需要设置 `TOOLCHAIN_ROOT`。

输出文件：

```text
demo/build/bin/workcard-demo
demo/bin/arm-linux-musleabi/workcard-demo
```

前者是可重建的编译输出，后者是安装 Demo 时使用的正式交付文件。

## 4. 企业统一工具链目录

企业已经把同一版本工具链安装到统一 Linux 路径时，可以显式指定：

```bash
TOOLCHAIN_ROOT=/企业实际路径/arm-v01c02-linux-musleabi-gcc ./scripts/build_demo.sh
```

显式路径只用于企业受管环境，普通客户应直接使用 `setup_dev_env.sh` 的默认安装位置。

## 5. 客户应用编译参数

在 SDK 根目录执行：

```bash
TOOLCHAIN_ROOT="$HOME/.local/workcard-toolchain/gcc-20250305-arm-v01c02-linux-musleabi/arm-v01c02-linux-musleabi-gcc"

"$TOOLCHAIN_ROOT/bin/arm-linux-musleabi-gcc" \
    -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$PWD/include" \
    application.c \
    -L"$PWD/lib/arm-linux-musleabi" \
    -lworkcard_sdk -lpthread \
    -o application
```

客户应用不应包含 `demo/src/cloud/internal`，除非明确复用当前 WSS 参考协议。

## 6. CMake

首次使用仍需先执行：

```bash
./scripts/setup_dev_env.sh
```

CMake 默认自动使用同一固定工具链：

```bash
cmake -S demo -B demo/build-cmake \
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-musleabi-toolchain.cmake
cmake --build demo/build-cmake
```

企业统一安装时，可在运行 CMake 前设置 `WORKCARD_TOOLCHAIN_ROOT` 为实际工具链根目录。

## 7. 架构检查

```bash
file demo/build/bin/workcard-demo
readelf -h demo/build/bin/workcard-demo
readelf -d demo/build/bin/workcard-demo
```

正确结果必须包含：

```text
ELF 32-bit
ARM
EABI5
interpreter /lib/ld-musl-arm.so.1
```

禁止把 x86_64、AArch64、glibc 或 hard-float 产物部署到当前固定固件。
