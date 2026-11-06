"""与 APP 的 UPFW 升级协议对应，不修改板端协议。"""

import struct
import time
import zlib
from pathlib import Path

MAGIC = b"UPFW"
BLOCK_SIZE = 1024
MAX_FIRMWARE_SIZE = 0x5A000
CRC_INIT = 0x3AA3


class TransferError(Exception):
    pass


class TransferCancelled(TransferError):
    pass


def crc32_firmware(data: bytes) -> int:
    # zlib 内部会取反；这里转换为组件使用的初值与“不取反”结果。
    return (zlib.crc32(data, CRC_INIT ^ 0xFFFFFFFF) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def read_firmware(path):
    path = Path(path)
    with path.open("rb") as file:
        data = file.read(MAX_FIRMWARE_SIZE + 1)
    if not data:
        raise ValueError("固件文件为空。")
    if len(data) > MAX_FIRMWARE_SIZE:
        raise ValueError("固件超过 360 KB（368640 字节），已禁止下发。")
    return data, crc32_firmware(data)


def send_firmware(port, data, cancel, on_progress, on_log):
    """每块收到 OK 才继续；取消或失败均停止，不自动重发数据块。"""
    if not 0 < len(data) <= MAX_FIRMWARE_SIZE:
        raise ValueError("固件大小必须在 1 字节至 360 KB 之间。")

    def check_cancel():
        if cancel.is_set():
            raise TransferCancelled("已停止发送。")

    def write(payload):
        check_cancel()
        if port.write(payload) != len(payload):
            raise TransferError("串口数据未完整发送，请重新连接后重试。")

    def wait_reply(expected, timeout):
        deadline = time.monotonic() + timeout
        pending = bytearray()
        while time.monotonic() < deadline:
            check_cancel()
            chunk = port.read_until(b"\n")
            if not chunk:
                continue
            pending.extend(chunk)
            while b"\n" in pending:
                raw, _, remaining = pending.partition(b"\n")
                pending[:] = remaining
                reply = raw.strip()
                if reply:
                    kind = "reply" if reply in (b"READY", b"OK", b"ERROR") else "muted"
                    on_log("AT32 → " + reply.decode("utf-8", errors="replace"), kind)
                if reply == b"ERROR":
                    raise TransferError("设备返回 ERROR，升级未完成，请检查固件或串口连接。")
                if reply == expected:
                    return
        name = expected.decode("ascii")
        raise TransferError(f"等待设备 {name} 超时，已停止下发。")

    port.reset_input_buffer()
    crc = crc32_firmware(data)
    header = struct.pack("<4sII", MAGIC, len(data), crc)
    on_log(f"发送升级请求 · {len(data):,} 字节 · CRC32 0x{crc:08X}", "info")
    write(header)
    wait_reply(b"READY", 5.0)
    on_log("设备已就绪，开始下发固件。", "info")

    started = time.monotonic()
    blocks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
    for index, offset in enumerate(range(0, len(data), BLOCK_SIZE), 1):
        block = data[offset:offset + BLOCK_SIZE]
        write(block)
        wait_reply(b"OK", 6.0)
        confirmed = offset + len(block)
        on_progress(confirmed, len(data), time.monotonic() - started)
        if index == 1 or index % 32 == 0 or index == blocks:
            on_log(f"设备已确认 {index} / {blocks} 块", "muted")

    # 最后一个 OK 表示板端 CRC 与升级信息提交成功，随后设备会复位。
    on_log("固件下发完成，设备已提交升级并将自动重启。", "success")
