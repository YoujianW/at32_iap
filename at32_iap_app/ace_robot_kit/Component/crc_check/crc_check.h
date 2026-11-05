/***************************************************************
 * crc_check.h
 * CRC 校验算法
 *
 * 包含 CRC8 / CRC16 / CRC32 三种校验，全部采用查表法。
 *
 * 查表原理：
 *
 *   逐位法每次处理 1 bit，N 字节数据需要 N×8 次循环。
 *   查表法以 1 字节（8 bit）为单位，预计算 0x00~0xFF 各字节
 *   对 CRC 值的"贡献"，一次查表 = 8 次逐位移位+异或。
 *
 *   CRC(n) = TABLE[ (CRC(n-1) ^ data_byte) & 0xFF ]
 *          ^ (CRC(n-1) >> 8)
 *
 *   速度对比（逐位 vs 查表）：约 8:1
 *   空间代价：CRC8 表 256B / CRC16 表 512B / CRC32 表 1KB
 *
 * 多项式说明：
 *
 *   CRC8  — G(x) = x^8 + x^5 + x^4 + 1     (Dallas/Maxim CRC)
 *           常见用途：DS18B20、MT6701 磁编码器角度数据校验
 *
 *   CRC16 — 标准 Modbus-RTU 多项式 (0x8005, 反转)
 *           常见用途：RS-485 通信、Modbus 协议
 *           输出为小端序：[CRC_LO][CRC_HI]
 *
 *   CRC32 — 标准 Ethernet/MPEG-2 多项式 (0x04C11DB7, 反转)
 *           常见用途：网络数据包、固件校验、gzip
 *           输出为小端序：[B0][B1][B2][B3]
 *
 * API 模式：
 *
 *   三个操作覆盖所有使用场景：
 *
 *   Calc(buf, len, init)
 *     → 计算 CRC 值，返回校验字
 *     → init：上次计算的 CRC 残留值（首次调用传 INIT 常量，续传时传上次返回值）
 *     → 支持分段计算——大块数据分多次调用 Calc，中间 CRC 值作为下次 init 传入
 *
 *   Verify(buf, len)
 *     → 校验 buf[len-2]~buf[len-1]（或 CRC8 的 buf[len-1]）是否与数据匹配
 *     → 返回 0=校验失败, 1=校验通过
 *     → len 必须 ≥ N+1（CRC8: 2, CRC16: 3, CRC32: 5）
 *
 *   Append(buf, len)
 *     → 计算 CRC 并填入 buf 末尾的 N 个字节
 *     → len 必须 ≥ N+1，末尾 N 字节会被覆盖
 *
 * 分段计算示例（CRC16）：
 *
 *   uint16_t crc = CRC16_Calc(chunk1, len1, CRC16_INIT);  // 第一段
 *   crc = CRC16_Calc(chunk2, len2, crc);                    // 续传 CRC 值
 *   crc = CRC16_Calc(chunk3, len3, crc);                    // 最后一段
 *   // crc 即整块数据的 CRC16
 *
 * 设计原则：
 *  - 纯软件算法，不依赖任何硬件 CRC 外设
 *  - 表项为编译期 const 常量，0 RAM 占用，仅 Flash
 *  - 所有函数可重入（reentrant），无全局状态
 *  - 指针入参做 NULL 检查，防御段错误
 ***************************************************************/
#ifndef CRC_CHECK_H
#define CRC_CHECK_H

#include <stdint.h>

/*================================================================
 * CRC8 — G(x) = x^8 + x^5 + x^4 + 1
 *
 *   别名：Dallas CRC-8、Maxim CRC-8
 *   多项式值：0x31（非反转形式）
 *   初始值：0x77
 *   输出宽度：8 bit（1 字节）
 *
 *   Verify 要求 len ≥ 2：
 *     [data ...] [CRC8]
 *
 *   Append 将 CRC 填入 buf[len-1]
 *================================================================*/

uint8_t  CRC8_Calc(const uint8_t *data, uint32_t len, uint8_t init);
uint32_t CRC8_Verify(const uint8_t *data, uint32_t len);
void     CRC8_Append(uint8_t *data, uint32_t len);

/*================================================================
 * CRC16 — 标准 Modbus-RTU 多项式
 *
 *   多项式：0x8005（反转形式），即 G(x) = x^16 + x^15 + x^2 + 1
 *   初始值：0x3AA3
 *   输出宽度：16 bit（2 字节，小端序）
 *
 *   Verify 要求 len ≥ 3：
 *     [data ...] [CRC_LO] [CRC_HI]
 *
 *   Append 将 CRC 填入 buf[len-2] 和 buf[len-1]（小端）
 *
 *   注意：Modbus 标准使用 0xFFFF 作为初始值。
 *   本项目使用 0x3AA3，与 RTU 从机协议保持一致。
 *================================================================*/

uint16_t CRC16_Calc(const uint8_t *data, uint32_t len, uint16_t init);
uint32_t CRC16_Verify(const uint8_t *data, uint32_t len);
void     CRC16_Append(uint8_t *data, uint32_t len);

/*================================================================
 * CRC32 — 标准 Ethernet/MPEG-2 多项式
 *
 *   多项式：0x04C11DB7（反转形式）
 *   初始值：0x3AA3
 *   输出宽度：32 bit（4 字节，小端序）
 *
 *   Verify 要求 len ≥ 5：
 *     [data ...] [B0] [B1] [B2] [B3]
 *
 *   Append 将 CRC 填入 buf[len-4] ~ buf[len-1]（小端）
 *
 *   注意：标准 Ethernet CRC32 使用 0xFFFFFFFF 作为初始值且
 *   输出取反。本项目使用 0x3AA3 且不取反，与私有协议保持一致。
 *================================================================*/

uint32_t CRC32_Calc(const uint8_t *data, uint32_t len, uint32_t init);
uint32_t CRC32_Verify(const uint8_t *data, uint32_t len);
void     CRC32_Append(uint8_t *data, uint32_t len);

#endif /* CRC_CHECK_H */
