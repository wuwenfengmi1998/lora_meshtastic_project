# MEMORY.md - 项目长期记忆

## 当前固件工程

- **路径**：`code/firmware-2.7.15.567b8ea`
- **版本**：Meshtastic v2.7.15（PlatformIO 工程）
- **支持平台**：ESP32 / nRF52 / RP2040 / RP2350 / Linux(Portduino)
- **构建系统**：PlatformIO，默认目标 `tbeam`，variants/ 目录存放各板型配置

## 自定义硬件扩展（重要！）

### TCA9535 I²C IO 扩展器
用于代理几个不能直连 MCU GPIO 的关键引脚：
- **P1.4** → LoRa 模块 RST（通过 `TCA9535GpioHal` 自定义 HAL 拦截 RadioLib 调用）
- **P1.6** → 振子 VIBRATOR（高电平震动）：开机/关机各震动 300ms（变体 esp32c3_moonshine_travelers_3）
- **P1.7** → GPS 使能引脚（`GpioTca9535GpsEnPin` 包装）
- **POWER_EN** → 上电保持（早期 boot 时即拉高，防止松开按键后 MOS 断电）

### 自定义文件
- `src/input/TCA9535ButtonThread.cpp` / `.h`：I²C 按键驱动（~16KB + ~12KB）
- `src/main.cpp`：含 TCA9535GpioHal 类定义、GPS EN 替换逻辑、TCA9535 按键线程初始化

## 工程架构要点

- **路由层级**：ReliableRouter → NextHopRouter → FloodingRouter → Router
- **Mesh 服务**：MeshService 连接射频收发、手机 API、GPS、模块系统
- **模块系统**：setupModules() 统一初始化，各模块通过 MeshModule 订阅数据包端口
- **线程调度**：基于 concurrency::OSThread + Periodic，loop() 中 mainController.runOrDelay()
- **消息协议**：Protobuf（protobufs/ 目录），生成代码在 src/mesh/generated/
