#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
串口 OTA 固件下载工具
协议：[0x55][0xAA][CMD][LEN_H][LEN_L][DATA...][CRC_H][CRC_L]
"""

import serial
import struct
import sys
import time
from pathlib import Path

# 命令定义
CMD_START = 0x01
CMD_DATA = 0x02
CMD_END = 0x03
CMD_ABORT = 0x04

ACK = 0x06
NAK = 0x15

# 配置
CHUNK_SIZE = 512  # 每帧数据大小（可调整到 1024）
TIMEOUT = 5       # 超时秒数


def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT 计算"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def build_frame(cmd: int, data: bytes = b'') -> bytes:
    """构建帧"""
    length = len(data)
    crc = crc16_ccitt(data)
    frame = bytes([0x55, 0xAA, cmd, (length >> 8) & 0xFF, length & 0xFF])
    frame += data
    frame += bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    return frame


def send_frame(ser: serial.Serial, cmd: int, data: bytes = b'') -> bool:
    """发送帧并等待 ACK"""
    frame = build_frame(cmd, data)
    ser.write(frame)
    ser.flush()

    # 等待回复
    start = time.time()
    while time.time() - start < TIMEOUT:
        if ser.in_waiting > 0:
            reply = ser.read(1)
            if reply[0] == ACK:
                return True
            elif reply[0] == NAK:
                print(f"\033[91m收到 NAK\033[0m")
                return False
        time.sleep(0.001)

    print(f"\033[91m超时无响应\033[0m")
    return False


def ota_download(port: str, firmware_path: str, baudrate: int = 2000000):
    """执行 OTA 下载"""
    # 读取固件
    fw_path = Path(firmware_path)
    if not fw_path.exists():
        print(f"文件不存在: {firmware_path}")
        sys.exit(1)

    firmware = fw_path.read_bytes()
    fw_size = len(firmware)
    print(f"固件: {fw_path.name}, 大小: {fw_size} 字节")

    # 打开串口
    try:
        ser = serial.Serial(port, baudrate, timeout=0.1)
    except Exception as e:
        print(f"打开串口失败: {e}")
        sys.exit(1)

    print(f"串口: {port} @ {baudrate} bps")
    time.sleep(0.5)  # 等待设备就绪

    # 1. 发送 START
    print("发送 START...")
    size_data = struct.pack('<I', fw_size)  # 小端 4 字节
    if not send_frame(ser, CMD_START, size_data):
        print("START 失败")
        ser.close()
        sys.exit(1)
    print("\033[92mSTART OK\033[0m")

    # 2. 发送 DATA
    offset = 0
    total_chunks = (fw_size + CHUNK_SIZE - 1) // CHUNK_SIZE
    chunk_num = 0

    while offset < fw_size:
        chunk = firmware[offset:offset + CHUNK_SIZE]
        chunk_num += 1

        # 进度显示
        progress = int(offset * 100 / fw_size)
        print(f"\r发送数据: {chunk_num}/{total_chunks} ({progress}%) - {offset}/{fw_size} bytes", end='')

        if not send_frame(ser, CMD_DATA, chunk):
            print(f"\n数据帧 {chunk_num} 失败 (offset={offset})")
            ser.close()
            sys.exit(1)

        offset += len(chunk)

    print(f"\r发送数据: {total_chunks}/{total_chunks} (100%) - {fw_size}/{fw_size} bytes")
    print("\033[92m数据发送完成\033[0m")

    # 3. 发送 END
    print("发送 END...")
    if not send_frame(ser, CMD_END):
        print("END 失败")
        ser.close()
        sys.exit(1)

    print("\033[92m\n===== OTA 完成！设备将自动重启 =====\033[0m")
    ser.close()


def main():
    if len(sys.argv) < 3:
        print("用法: python ota_uart_download.py <COM端口> <固件路径> [波特率]")
        print("示例: python ota_uart_download.py COM3 SmartClock.bin 2000000")
        sys.exit(1)

    port = sys.argv[1]
    firmware = sys.argv[2]
    baudrate = int(sys.argv[3]) if len(sys.argv) > 3 else 2000000

    ota_download(port, firmware, baudrate)


if __name__ == "__main__":
    main()
