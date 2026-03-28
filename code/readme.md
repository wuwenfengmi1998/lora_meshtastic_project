+ git submodule update --init

# my change 

+ #ifndef SETTING_MAX_POWER
#define SETTING_MAX_POWER 3
#endif
+ RDEF(CN, 470.0f, 510.0f, 100, 0, SETTING_MAX_POWER, true, false, false),

# TODO: 升级 IO 扩展器 TCA9535 → PCAL9535

- 原因：TCA9535 无可配置内部上拉电阻，矩阵键盘列线（P0.4~P0.7）悬空易受电磁干扰误触发
- PCAL9535 pin-compatible，增加可配置上拉/下拉寄存器（0x41~0x46），软件可控内部上拉
- PCAL9535 还支持每引脚独立中断遮罩、可配置输出驱动强度
- 影响文件：TCA9535ButtonThread.h/.cpp 中的 init() 需追加上拉配置寄存器写入
- 备注：如不改芯片，可在 PCB 上列线加 10kΩ 外部上拉电阻作为替代方案