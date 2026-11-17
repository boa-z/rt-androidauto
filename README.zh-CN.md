# rt-androidauto

面向 RTOS 和嵌入式系统的可移植 Android Auto Receiver SDK。

[English](README.md)

当前交付是可独立构建、经过 Host 测试的首版 SDK，包含 C11 的 AOA 控制请求、AAP 流解析和按通道分片重组、mbedTLS TLS 1.2 后端、nanopb 服务发现、基础 H.264/PCM/麦克风会话、输入和基础传感器。协议核心不依赖 RT-Thread、D213、MPP、CherryUSB、LVGL 或 rt-carplay。可选 RT-Thread Port 提供分配器和单调时钟。

**当前尚未完成全部软件开发范围。** 已实现有界重连控制器、协商按键与单指触摸生命周期；整机工程单独开发有线后端与共用 Mobility Cockpit UI，并以真实 Git Submodule 引入 SDK。无线蓝牙/Wi-Fi 启动和同一镜像内的 CarPlay/Android Auto 切换仍待完成。没有连接 Android 手机或 D213，也没有运行 DHU。Host 测试通过不能作为手机互操作或认证结论。

## 构建与验证

准备 CMake 3.20+、C11 编译器和本地 mbedTLS 2.28.9 源码目录。构建过程不会联网下载依赖。

```sh
cmake -S . -B build -DRTAA_MBEDTLS_SOURCE=/path/to/mbedtls-2.28.9 -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Linux 可追加 `-DRTAA_SANITIZERS=ON`。Windows Visual Studio 生成器需要为构建和测试分别追加 `--config Debug` 和 `-C Debug`。本地 Windows 使用 MinGW/Ninja 验证。

nanopb 运行库和生成的消息源码已随 SDK 提供。重新生成和核对协议定义时使用 `requirements-dev.txt`、`scripts/generate-proto.py` 和 `scripts/verify-protocol.py`。

平台必须提供真实的传输、音视频 sink、麦克风控制、随机数和凭证信任策略。服务 Channel ID 来自实际注册和服务发现描述，不能将角色枚举值当作固定通道号。所有 Receiver API 由一个工作线程串行调用；回调不得重入同一个对象。

`include/rtaa/connection.h` 提供有界重连：只对传输 I/O 错误和超时重试，达到次数上限后等待显式重新启动。输入绑定、手势与取消规则见 [连接和输入生命周期](docs/connection-lifecycle.md)。

具体实现范围、验证结果、剩余工作和整机集成前置条件见 [交付状态](docs/delivery-status.md)、[协议兼容性](docs/protocol-compatibility.md)、[移植说明](docs/porting.md) 和 [Luban-Lite 集成准备](docs/luban-lite-integration.md)。

组合 SDK 使用 GPL-3.0-only，保留 nanopb、Open Android Auto 和 AllPlay 的原始声明，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

有线协议扩展进展：已新增导航焦点、语音/电池通知、通话可用状态发送、
通道关闭与重开、发送分片、可配置按键/旋钮及传感器调度回调。详见
[扩展实现与待办](docs/wired-extensions.md)。默认仍采用 AAP 1.1；可配置
版本上限不代表完整实现对应版本。无线和真实手机验证仍未完成。
