/**
 * Meshtastic 固件 - LoRa 消息接收、转发与处理链路分析
 * 
 * 本文档追踪从 LoRa 硬件中断到应用层消息处理的完整代码链路
 * 适用版本：firmware-2.7.15.567b8ea
 * 目标变体：esp32c3_moonshine_travelers_3（E22_400M33S / LLCC68）
 */

// ============================================================================
// 一、LoRa 消息接收（中断驱动）
// ============================================================================

/**
 * 接收方式：DIO1 硬件中断（非轮询）
 * 
 * 完整中断链路：
 * 
 * LoRa 芯片（SX1262/LLCC68）收到数据包
 *   → DIO1 引脚拉高（硬件中断信号）
 *   → ESP32-C3 GPIO3 (LORA_DIO1) 收到中断
 *   → [RadioLib 库中断分发]
 *   → SX126xInterface::isrRxLevel0()        [RadioLibInterface.cpp:69]
 *       → isrLevel0Common(ISR_RX)           [RadioLibInterface.cpp:56-67]
 *           → instance->notifyFromISR(&xHigherPriorityTaskWoken, cause, true)
 *               （通知 RadioLibInterface 工作线程）
 *           → RadioLibInterface::onNotify(ISR_RX)  [RadioLibInterface.cpp:264-267]
 *               → handleReceiveInterrupt()    [RadioLibInterface.cpp:425-512]
 *                   - iface->readData() 读取接收到的数据
 *                   - 构建 meshtastic_MeshPacket 结构体
 *                   - 调用 addReceiveMetadata() 添加 SNR/RSSI 信息
 *                   - 调用 deliverToReceiver(mp)
 */

// 关键文件：
//   src/mesh/SX126xInterface.cpp      第 283-303 行：startReceive() 注册中断
//   src/mesh/RadioLibInterface.h      第 50 行：lora.setDio1Action(isrRxLevel0)
//   src/mesh/RadioLibInterface.cpp    第 69-77 行：isrRxLevel0() 中断入口
//   src/mesh/RadioLibInterface.cpp    第 425-512 行：handleReceiveInterrupt() 读取数据

// ============================================================================
// 二、消息转发判断（Router 层）
// ============================================================================

/**
 * 消息从 RadioInterface 交付到 Router 队列：
 * 
 * RadioInterface::deliverToReceiver(mp)     [RadioInterface.cpp:675-681]
 *   → mp->transport_mechanism = TRANSPORT_LORA
 *   → router->enqueueReceivedMessage(p)     将消息入队到 fromRadioQueue
 * 
 * Router 工作线程处理（独立 OSThread）：
 * 
 * Router::runOnce()                          [Router.cpp:137-147]
 *   → 从 fromRadioQueue 取出消息
 *   → perhapsHandleReceived(mp)              [Router.cpp:759-808]
 *       - 检查 ignore_incoming 列表（被屏蔽的节点）
 *       - 检查节点是否被忽略（node->is_ignored）
 *       - 检查是否来自 MQTT 需要特殊处理
 *       → shouldFilterReceived(mp)  【转发判断入口】
 */

/**
 * 转发判断流程（三层路由类继承）：
 * 
 * Router（基类）
 *   ↓
 * FloodingRouter（泛洪路由）
 *   ↓
 * NextHopRouter（下一跳路由）
 *   ↓
 * ReliableRouter（可靠路由，实际使用的类）
 * 
 * 判断顺序：
 * 
 * 1. ReliableRouter::shouldFilterReceived()  [ReliableRouter.cpp:44]
 *    → 检查是否有人转发过我们的包（隐式 ACK）
 * 
 * 2. FloodingRouter::shouldFilterReceived()  [FloodingRouter.cpp:27-60]
 *    → wasSeenRecently(p) 去重检测
 *        → PacketHistory::find(sender, packet_id)  [PacketHistory.cpp:199-225]
 *        → 通过 (发送者节点号, 数据包ID) 组合判断是否重复
 *    → 如果是重复包：rxDupe++，调用 perhapsRebroadcast() 重新广播
 *    → 如果是新包：继续向下处理
 * 
 * 3. NextHopRouter::shouldFilterReceived()  [NextHopRouter.cpp:37]
 *    → 处理 fallback 到泛洪模式
 *    → sniffReceived() 更新下一跳信息  [NextHopRouter.cpp:86]
 * 
 * 4. 转发决策：NextHopRouter::perhapsRebroadcast()  [NextHopRouter.cpp:128-168]
 *    条件检查：
 *      a. !isToUs(p) && !isFromUs(p)  不是发给我们，也不是我们发的
 *      b. p->hop_limit > 0              剩余跳数 > 0
 *      c. isRebroadcaster()               设备角色允许转发
 *           → 角色不是 CLIENT_MUTE 且 rebroadcast_mode != NONE
 *      d. p->next_hop == NO_NEXT_HOP_PREFERENCE（泛洪）
 *          或 p->next_hop == 本节点号（指定下一跳）
 *    跳数处理：
 *      → shouldDecrementHopLimit(p) 判断是否需要递减跳数
 *      → toSend->hop_limit--  （跳数递减）
 *    发送：
 *      → FloodingRouter::send(toSend)  泛洪发送
 *      → 或 NextHopRouter::send(toSend)  下一跳发送
 */

// 去重缓存结构：
//   struct PacketRecord {
//       NodeNum sender;        // 发送者节点号
//       PacketId id;           // 数据包 ID
//       uint32_t rxTimeMsec;  // 接收时间戳
//       uint8_t next_hop;     // 下一跳偏好
//       uint8_t hop_limit;     // 跳数限制
//       uint8_t relayed_by[6];// 中继节点列表
//   };

// ============================================================================
// 三、消息处理（应用层）
// ============================================================================

/**
 * 通过转发判断后，进入消息处理：
 * 
 * Router::handleReceived(mp, src)        [Router.cpp:683-757]
 *   → p->rx_time = getValidTime(RTCQualityFromNet)  记录接收时间
 *   → perhapsDecode(p)                   [Router.cpp:408-536]
 *       - 尝试 PKI 解密（端到端加密）
 *       - 尝试 PSK 频道解密（频道共享密钥）
 *       - 返回 DecodeState::DECODE_SUCCESS / DECODE_FAIL / DECODE_NOT_NEEDED
 *   → 如果解密成功：
 *       → MeshModule::callModules(*p, src)  [MeshModule.cpp:88-205]
 *           - 遍历所有已注册的模块（位置模块、文本模块、遥测模块等）
 *           - 对每个模块调用 wantPacket(&mp) 判断是否关心此包
 *           - 如果关心，调用 module.handleReceived(mp)
 *           - 如果 handled == STOP，停止分发给后续模块
 *       → 如果包是发给我们的（toUs）：
 *           → 触发模块 sendResponse() 发送回复
 *   → 如果启用了 MQTT：
 *       → mqtt->onSend() 转发到 MQTT 服务器
 */

// ============================================================================
// 四、完整调用链总结
// ============================================================================

/**
 * 接收链路：
 *   LoRa 硬件 DIO1 中断
 *     → RadioLibInterface::isrRxLevel0()
 *       → handleReceiveInterrupt()
 *         → deliverToReceiver(mp)
 *           → router->enqueueReceivedMessage(p)
 * 
 * 处理链路：
 *   Router::runOnce() 从队列取消息
 *     → perhapsHandleReceived(mp)
 *       → shouldFilterReceived()  [去重 + 转发判断]
 *         → handleReceived(mp)    [解密]
 *           → MeshModule::callModules()  [模块分发]
 *             → module.wantPacket() + module.handleReceived()
 * 
 * 转发链路：
 *   perhapsRebroadcast(mp)  [跳数>0 且角色允许]
 *     → hop_limit--
 *     → FloodingRouter::send() 或 NextHopRouter::send()
 *       → RadioLibInterface::send()
 *         → txQueue 入队
 *           → startSend() → iface->startTransmit()
 *             → DIO3/TXEN 中断 → completeSending()
 */

// ============================================================================
// 五、关键文件索引
// ============================================================================

// 接收层：
//   src/mesh/SX126xInterface.cpp      中断注册、startReceive()
//   src/mesh/RadioLibInterface.cpp    中断处理、数据读取、消息交付

// 路由层：
//   src/mesh/Router.cpp               基类路由、消息处理入口、解密
//   src/mesh/FloodingRouter.cpp      泛洪路由、去重、转发判断
//   src/mesh/NextHopRouter.cpp       下一跳路由、跳数管理
//   src/mesh/ReliableRouter.cpp      可靠传输（ACK 机制）
//   src/mesh/PacketHistory.cpp        去重缓存管理

// 应用层：
//   src/mesh/MeshModule.cpp          模块注册与消息分发
//   src/mesh/MeshService.cpp         消息服务（发包、收包高层逻辑）
//   src/modules/                     各功能模块实现（位置、文本、遥测等）

// 配置相关：
//   src/mesh/generated/meshtastic/config.pb.h   Protobuf 定义（MeshPacket 结构体）
//   variant.h                              变体引脚定义（LORA_DIO1 等）
