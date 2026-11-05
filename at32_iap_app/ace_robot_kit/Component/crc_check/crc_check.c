/***************************************************************
 * crc_check.c
 * CRC 校验算法
 *
 * 全部采用查表法（Table-Driven CRC），表由多项式预计算生成。
 *
 * ── 为什么用查表法而不是逐位计算？ ──
 *
 *   逐位法（Bit-by-Bit）：
 *     for each byte:
 *       for each bit (8 times):
 *         if (crc & 0x80) crc = (crc << 1) ^ POLY;
 *         else             crc = (crc << 1);
 *
 *   每字节 8 次循环，N 字节 → 8N 次迭代。
 *
 *   查表法（Table-Driven）：
 *     for each byte:
 *       idx = (crc >> 8) ^ data_byte;
 *       crc = (crc << 8) ^ TABLE[idx];
 *
 *   每字节 1 次查表 + 1 次异或，N 字节 → N 次迭代。
 *
 *   加速比 ≈ 8:1。代价是 ROM 占用（CRC8: 256B, CRC16: 512B, CRC32: 1KB）。
 *   在 AT32F403AVGT7（1MB Flash）上这些表占用可忽略。
 *
 * ── 表是如何生成的？ ──
 *
 *   TABLE[i] = 对单字节 i 执行逐位 CRC 计算 8 次的结果。
 *   i ∈ [0x00, 0xFF]，共 256 项。
 *
 *   例（CRC8, 多项式 0x31）：
 *     TABLE[0x5E] = 对 0x5E 逐位 CRC8 计算 8 次后的值。
 *
 *   本文件所有表均为编译期常量，由工具脚本预生成后嵌入。
 *
 * ── CRC 校验的数学本质 ──
 *
 *   CRC 本质是二元域 GF(2) 上的多项式除法余数。
 *
 *   发送方：
 *     原始数据 M(x) 左移 r 位 → M(x) × x^r
 *     计算 R(x) = [M(x) × x^r] mod G(x)
 *     发送 T(x) = M(x) × x^r + R(x)
 *
 *   接收方：
 *     收到 T'(x)，计算 T'(x) mod G(x)
 *     若余数 = 0 → 传输无误
 *     若余数 ≠ 0 → 数据损坏
 *
 *   实际实现中不直接做多项式除法，而是用 LFSR（线性反馈移位寄存器）
 *   的硬件等效——这正是 CRC 表查表法模拟的东西。
 ***************************************************************/
#include "crc_check.h"
#include <stddef.h>

/*================================================================
 * CRC8 — G(x) = x^8 + x^5 + x^4 + 1
 *
 *   多项式二进制表示：0x31（非反转）
 *      x^8 + x^5 + x^4 + 1 = (1)·x^8 + (0)·x^7 + (0)·x^6 + (1)·x^5
 *                           + (1)·x^4 + (0)·x^3 + (0)·x^2 + (0)·x^1
 *                           + (1)·x^0
 *                           = 0b100110001 = 0x131
 *   反转（Reflected）形式用于 LSB-first 计算时取低 8 位得 0x31。
 *
 *   初始值 CRC8_INIT = 0x77：
 *     非标准选择，与协议的"预置全 1 再反转"对应。
 *
 *   TABLE 占用：256 × 1 = 256 Byte
 *
 *   使用示例——MT6701 磁编码器角度数据 CRC 校验：
 *     uint8_t buf[3];  // [angle_H][angle_L][CRC8]
 *     SPI_Read(buf, 3);
 *     if (CRC8_Verify(buf, 3)) {
 *         angle = ((buf[0] << 6) | (buf[1] >> 2));
 *     }
 *================================================================*/

static const uint8_t CRC8_INIT = 0x77;

/*
 * CRC8 查找表
 *
 * 多项式：G(x) = x^8 + x^5 + x^4 + 1
 * 算法：对 i = 0..255，逐位 CRC8 计算 8 次，结果填入 TABLE[i]
 */
static const uint8_t CRC8_TABLE[256] = {
    0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41,
    0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e, 0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc,
    0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c, 0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
    0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c, 0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff,
    0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5, 0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07,
    0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
    0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24,
    0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b, 0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9,
    0x8c, 0xd2, 0x30, 0x6e, 0xed, 0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
    0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50,
    0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c, 0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee,
    0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
    0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b,
    0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4, 0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16,
    0xe9, 0xb7, 0x55, 0x0b, 0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
    0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35,
};

/*
 * CRC8 核心计算
 *
 * 查表递推（单步）：
 *   crc_new = TABLE[ crc_old ^ data_byte ]
 *
 * 解释：
 *   将当前 CRC 值与新数据字节异或得到的临时值作为查表索引，
 *   表项即为"消去这个字节后 CRC 寄存器的更新值"。
 *
 *   这里的简洁形式（crc = TABLE[crc ^ data]）是"直接表"查表法，
 *   不同于 CRC16/CRC32 的"间接表"（需要位移+异或），因为 CRC8 的
 *   寄存器宽度恰好等于一个字节——表项本身就是完整的 CRC 残值。
 *
 * 参数：
 *   data  — 数据首地址（只读）
 *   len   — 数据字节数
 *   init  — CRC 初始值（首次调用传 CRC8_INIT，续传时传上次返回值）
 *
 * 返回值：CRC8 校验值（1 字节，取 uint8_t 的低 8 位即可）
 */
uint8_t CRC8_Calc(const uint8_t *data, uint32_t len, uint8_t init)
{
    if (data == NULL) return 0;

    uint8_t crc = init;
    uint8_t idx;

    while (len--) {
        idx = crc ^ (*data++);
        crc = CRC8_TABLE[idx];
    }
    return crc;
}

/*
 * CRC8 校验
 *
 * 原理：
 *   将收到的 [数据 + CRC] 整体再做一次 CRC 计算。
 *   若数据传输无误，计算结果应等于数据末尾携带的 CRC 值。
 *
 *   更严格的方式是对整包（含 CRC 尾字节）做 CRC 计算，
 *   无误时应得到固定残值（residue）。本项目采用逐字节比对方式，
 *   性能相同且逻辑更直观。
 *
 * 参数：
 *   data — 数据首地址（包含末尾 1 字节 CRC）
 *   len  — 总长度（数据 + CRC），必须 ≥ 2
 *
 * 返回值：1=校验通过, 0=失败
 */
uint32_t CRC8_Verify(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len <= 1) return 0;

    /*
     * 对 data[0..len-2] 计算 CRC，与 data[len-1] 比对。
     *
     * 例：收到 [0x12, 0x34, 0xAB]（len=3）
     *     Calc([0x12, 0x34], 2) → 0x5C
     *     data[2] = 0xAB ≠ 0x5C → 校验失败
     */
    uint8_t expected = CRC8_Calc(data, len - 1, CRC8_INIT);
    return (expected == data[len - 1]);
}

/*
 * CRC8 追加
 *
 * 计算数据 CRC 并填入末尾字节。
 * 调用方需确保 buf 有 len 字节空间，其中最后 1 字节为 CRC 预留。
 *
 * 参数：
 *   data — 数据缓冲区（末尾 1 字节将被覆盖）
 *   len  — 总长度（数据 + CRC），必须 ≥ 2
 */
void CRC8_Append(uint8_t *data, uint32_t len)
{
    if (data == NULL || len <= 1) return;

    data[len - 1] = CRC8_Calc(data, len - 1, CRC8_INIT);
}


/*================================================================
 * CRC16 — 标准 Modbus-RTU 多项式 (0x8005, 反转)
 *
 *   多项式二进制表示（反转后）：
 *     G(x) = x^16 + x^15 + x^2 + 1
 *     反转形式：0xA001
 *     （0x8005 是非反转形式，Modbus 在实际传输中使用反转形式 0xA001）
 *
 *   初始值 CRC16_INIT = 0x3AA3：
 *     非 Modbus 标准（标准为 0xFFFF），与项目私有 RTU 协议匹配。
 *
 *   数据格式（小端）：
 *     [data byte 0] [data byte 1] ... [data byte N-3] [CRC_LO] [CRC_HI]
 *     CRC 低字节在前、高字节在后。
 *
 *   查表递推（单步）：
 *     crc = (crc >> 8) ^ TABLE[ (crc ^ data_byte) & 0x00FF ]
 *
 *   解释：
 *     1. (crc ^ data_byte) & 0xFF → 取 crc 低 8 位与数据字节异或
 *     2. TABLE[idx] → 查表得到"消去低 8 位后需要异或回去的值"
 *     3. crc >> 8 → 移位腾出低 8 位
 *     4. 两者异或 → 完成本字节的 CRC 更新
 *
 *   TABLE 占用：256 × 2 = 512 Byte
 *================================================================*/

static const uint16_t CRC16_INIT = 0x3AA3;

/*
 * CRC16 查找表
 *
 * 多项式：0x8005（反转形式 0xA001）
 * 用途：Modbus-RTU 通信校验
 * 注意：此表使用 MSB-first 索引方式（标准 Modbus 查表法）
 */
static const uint16_t CRC16_TABLE[256] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241, 0xc601,
    0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440, 0xcc01, 0x0cc0,
    0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40, 0x0a00, 0xcac1, 0xcb81,
    0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841, 0xd801, 0x18c0, 0x1980, 0xd941,
    0x1b00, 0xdbc1, 0xda81, 0x1a40, 0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01,
    0x1dc0, 0x1c80, 0xdc41, 0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0,
    0x1680, 0xd641, 0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081,
    0x1040, 0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
    0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441, 0x3c00,
    0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41, 0xfa01, 0x3ac0,
    0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840, 0x2800, 0xe8c1, 0xe981,
    0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41, 0xee01, 0x2ec0, 0x2f80, 0xef41,
    0x2d00, 0xedc1, 0xec81, 0x2c40, 0xe401, 0x24c0, 0x2580, 0xe541, 0x2700,
    0xe7c1, 0xe681, 0x2640, 0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0,
    0x2080, 0xe041, 0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281,
    0x6240, 0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
    0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41, 0xaa01,
    0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840, 0x7800, 0xb8c1,
    0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41, 0xbe01, 0x7ec0, 0x7f80,
    0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40, 0xb401, 0x74c0, 0x7580, 0xb541,
    0x7700, 0xb7c1, 0xb681, 0x7640, 0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101,
    0x71c0, 0x7080, 0xb041, 0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0,
    0x5280, 0x9241, 0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481,
    0x5440, 0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
    0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841, 0x8801,
    0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40, 0x4e00, 0x8ec1,
    0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41, 0x4400, 0x84c1, 0x8581,
    0x4540, 0x8701, 0x47c0, 0x4680, 0x8641, 0x8201, 0x42c0, 0x4380, 0x8341,
    0x4100, 0x81c1, 0x8081, 0x4040
};

/*
 * CRC16 核心计算
 *
 * 查表递推（单步）：
 *   crc = (crc >> 8) ^ TABLE[ (crc ^ data_byte) & 0x00FF ]
 *
 * 与 CRC8 的差异：
 *   CRC8 的寄存器刚好是 1 字节，表项直接覆盖整个寄存器，
 *   所以 crc_new = TABLE[crc ^ data] 一步到位。
 *
 *   CRC16 的寄存器是 2 字节，表项也为 2 字节。
 *   每步消去低 8 位，高 8 位右移后与表项异或。
 *
 * 参数：
 *   data — 数据首地址（只读）
 *   len  — 数据字节数
 *   init — CRC 初始值（首次调用传 CRC16_INIT）
 *
 * 返回值：CRC16 校验值（2 字节小端：低字节在 [7:0]，高字节在 [15:8]）
 *
 * 分段计算示例——Modbus 帧 CRC 校验：
 *   // 帧 = [addr] [func] [data...] [CRC_LO] [CRC_HI]
 *   uint16_t crc = CRC16_INIT;
 *   crc = CRC16_Calc(&addr, 1, crc);     // 地址
 *   crc = CRC16_Calc(&func, 1, crc);     // 功能码
 *   crc = CRC16_Calc(data, data_len, crc); // 数据段
 *   // crc 即全帧 CRC，可填入帧尾或比对
 */
uint16_t CRC16_Calc(const uint8_t *data, uint32_t len, uint16_t init)
{
    if (data == NULL) return 0xFFFF;

    uint16_t crc = init;
    while (len--) {
        crc = (uint16_t)(crc >> 8) ^ CRC16_TABLE[(crc ^ (uint16_t)(*data++)) & 0x00FF];
    }
    return crc;
}

/*
 * CRC16 校验
 *
 * 将 [data + CRC_LO + CRC_HI] 中的 CRC 提取，与对数据段重新计算的值比对。
 *
 * 参数：
 *   data — 数据首地址（包含末尾 2 字节 CRC）
 *   len  — 总长度（数据 + 2 字节 CRC），必须 ≥ 3
 *
 * 返回值：1=校验通过, 0=失败
 */
uint32_t CRC16_Verify(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len <= 2) return 0;

    /*
     * 对 data[0..len-3] 重新计算 CRC16
     */
    uint16_t expected = CRC16_Calc(data, len - 2, CRC16_INIT);

    /*
     * 与帧尾 CRC_LO（data[len-2]）和 CRC_HI（data[len-1]）比对
     *
     * Modbus 帧尾格式（小端）：
     *   data[len-2] = CRC 低 8 位
     *   data[len-1] = CRC 高 8 位
     */
    return ((expected & 0xFF) == data[len - 2] &&
            ((expected >> 8) & 0xFF) == data[len - 1]);
}

/*
 * CRC16 追加
 *
 * 计算数据 CRC 并填入末尾 2 字节（小端序）。
 *
 * 参数：
 *   data — 数据缓冲区（末尾 2 字节将被覆盖）
 *   len  — 总长度（数据 + 2 字节 CRC），必须 ≥ 3
 */
void CRC16_Append(uint8_t *data, uint32_t len)
{
    if (data == NULL || len <= 2) return;

    uint16_t crc = CRC16_Calc(data, len - 2, CRC16_INIT);

    /*
     * 小端序写入：
     *   data[len-2] = CRC 低字节
     *   data[len-1] = CRC 高字节
     */
    data[len - 2] = (uint8_t)(crc & 0x00FF);
    data[len - 1] = (uint8_t)((crc >> 8) & 0x00FF);
}


/*================================================================
 * CRC32 — 标准 Ethernet/MPEG-2 多项式 (0x04C11DB7, 反转)
 *
 *   多项式二进制表示（反转后）：
 *     G(x) = x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11
 *          + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
 *     反转形式：0xEDB88320
 *
 *   初始值 CRC32_INIT = 0x3AA3：
 *     非标准（标准 Ethernet CRC32 使用 0xFFFFFFFF 且输出取反）。
 *     本项目不需要取反步骤，与私有协议保持一致。
 *
 *   数据格式（小端）：
 *     [data ...] [B0] [B1] [B2] [B3]
 *     B0 为 CRC 最低字节，B3 为最高字节。
 *
 *   查表递推（单步）：
 *     crc = (crc >> 8) ^ TABLE[ (crc ^ data_byte) & 0x000000FF ]
 *
 *   TABLE 占用：256 × 4 = 1024 Byte
 *================================================================*/

static const uint32_t CRC32_INIT = 0x3AA3;

/*
 * CRC32 查找表
 *
 * 多项式：0x04C11DB7（反转形式 0xEDB88320）
 * 标准 Ethernet/MPEG-2 CRC32 表
 * 即常见的 "ZIP/PNG/gzip CRC32" 表
 */
static const uint32_t CRC32_TABLE[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/*
 * CRC32 核心计算
 *
 * 查表递推（单步）：
 *   crc = (crc >> 8) ^ TABLE[ (crc ^ data_byte) & 0x000000FF ]
 *
 * 结构与 CRC16 完全相同，只是寄存器宽度变为 32 位。
 *
 * 参数：
 *   data — 数据首地址（只读）
 *   len  — 数据字节数
 *   init — CRC 初始值（首次调用传 CRC32_INIT）
 *
 * 返回值：CRC32 校验值（4 字节小端）
 */
uint32_t CRC32_Calc(const uint8_t *data, uint32_t len, uint32_t init)
{
    if (data == NULL) return 0xFFFF;

    uint32_t crc = init;
    while (len--) {
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ (uint32_t)(*data++)) & 0x000000FF];
    }
    return crc;
}

/*
 * CRC32 校验
 *
 * 提取 buf 末尾 4 字节作为"声称的 CRC"，与重新计算值比对。
 *
 * 参数：
 *   data — 数据首地址（包含末尾 4 字节 CRC）
 *   len  — 总长度（数据 + 4 字节 CRC），必须 ≥ 5
 *
 * 返回值：1=校验通过, 0=失败
 */
uint32_t CRC32_Verify(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len <= 4) return 0;

    /*
     * 对 data[0..len-5] 重新计算 CRC32
     */
    uint32_t expected = CRC32_Calc(data, len - 4, CRC32_INIT);

    /*
     * 从 buf 末尾提取声称的 CRC32（小端序拼接）
     *
     * buf[len-4] = B0（最低字节）
     * buf[len-3] = B1
     * buf[len-2] = B2
     * buf[len-1] = B3（最高字节）
     */
    uint32_t input_crc = 0;
    input_crc |=  data[len - 4];
    input_crc |= ((uint32_t)data[len - 3] << 8)  & 0x0000FF00;
    input_crc |= ((uint32_t)data[len - 2] << 16) & 0x00FF0000;
    input_crc |= ((uint32_t)data[len - 1] << 24) & 0xFF000000;

    return (input_crc == expected);
}

/*
 * CRC32 追加
 *
 * 计算数据 CRC 并填入末尾 4 字节（小端序）。
 *
 * 参数：
 *   data — 数据缓冲区（末尾 4 字节将被覆盖）
 *   len  — 总长度（数据 + 4 字节 CRC），必须 ≥ 5
 */
void CRC32_Append(uint8_t *data, uint32_t len)
{
    if (data == NULL || len <= 4) return;

    uint32_t crc = CRC32_Calc(data, len - 4, CRC32_INIT);

    /*
     * 小端序写入：
     *   data[len-4] = B0（最低字节）
     *   data[len-3] = B1
     *   data[len-2] = B2
     *   data[len-1] = B3（最高字节）
     */
    data[len - 4] = (uint8_t)(crc & 0xFF);
    data[len - 3] = (uint8_t)((crc >> 8)  & 0xFF);
    data[len - 2] = (uint8_t)((crc >> 16) & 0xFF);
    data[len - 1] = (uint8_t)((crc >> 24) & 0xFF);
}
