# 第三方许可说明

## E-Hal Media SDK

仓库中的 `vendor/ehal_media/LICENSE` 声明为 MIT License，版权归 EulerPi 海鸥派所有。对外交付时必须保留该许可原文。

## OpenSSL

工牌 Demo 使用固定 OpenSSL 1.1 运行库和公开头文件完成 TLS、证书链、SNI 和主机名校验。正式对外交付必须附带对应 OpenSSL 版本的许可原文和版权说明。

Demo 随附的 `demo/config/ca-certificates.crt` 是目标固件使用的公开根证书集合，只包含公开证书，不包含私钥。正式发布时应记录其来源、更新时间和 SHA-256，并按证书集合上游条款保留相应说明。

## Eclipse Paho Embedded C

工牌 Demo 使用 Eclipse Paho Embedded C 的 MQTT Client 和 Packet 源码实现 MQTT 3.1.1 协议。上游源码按 Eclipse Public License 1.0 和 Eclipse Distribution License 1.0 提供，许可原文位于 `demo/third_party/paho-embedded-c/epl-v10` 与 `demo/third_party/paho-embedded-c/edl-v10`，对外交付时不得删除。

## JsonCpp

底层服务使用随固定 E-Hal 包提供的 JsonCpp 静态库处理 WiFi 持久化状态。正式发布必须核对对应版本许可并附带原文。

## ARM musl 交叉编译工具链

客户开发环境使用 SDK 随附的固定 GCC 10.3.0、GNU Binutils 2.41、musl 1.2.3 和 Linux 目标头文件。项目已确认该固定工具链允许向本项目客户再分发，发布时必须同时保留 `TOOLCHAIN_NOTICE.md`、工具链独立 SHA-256 以及适用的开源许可和源码提供记录。

## 芯片和媒体二进制

海思 MPP、Sensor、音频算法及其他厂商二进制库不是本项目原创开源代码。虽然技术交付目录已经按 ELF 依赖整理这些文件，但对外再分发前必须由项目商务或法务依据采购合同、SDK 授权文件和芯片厂商条款确认再分发权。

在未取得书面确认前，不应把包含这些厂商二进制的目录直接发送给外部客户。

## 发布要求

正式发布人员应：

1. 锁定每个第三方文件的来源和版本。
2. 保存原始许可文件。
3. 确认商业再分发范围。
4. 将许可原文加入最终压缩包。
5. 在发布记录中保存文件 SHA-256。
