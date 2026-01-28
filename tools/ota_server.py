#!/usr/bin/env python3
"""
OTA 固件下载服务器（多线程版本）

用法：
    python ota_server.py

改进点：
1. 使用 ThreadingTCPServer 多线程处理请求，避免单个连接阻塞
2. 添加连接超时，防止半开连接卡死
3. 允许地址复用，快速重启服务器
"""

import http.server
import socketserver
import os
import socket

PORT = 8000
DIRECTORY = "."  # 固件就在当前目录
TIMEOUT = 30  # 连接超时（秒）


def get_ip():
    """获取本机局域网 IP"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        IP = s.getsockname()[0]
    except Exception:
        IP = '127.0.0.1'
    finally:
        s.close()
    return IP


class OTAHandler(http.server.SimpleHTTPRequestHandler):
    """自定义 HTTP 请求处理器"""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def log_message(self, format, *args):
        """自定义日志格式"""
        print(f"\033[92m[Device Access] {self.address_string()} - {format % args}\033[0m")

    def setup(self):
        """设置连接超时"""
        super().setup()
        self.request.settimeout(TIMEOUT)

    def copyfile(self, source, outputfile):
        """覆盖默认的文件传输方法，强制每次写入后 flush"""
        # shutil.copyfileobj(source, outputfile) # 默认实现
        try:
            # 获取文件大小 (如果是文件对象)
            fsize = os.fstat(source.fileno()).st_size
            print(f"  > Sending file size: {fsize} bytes")
        except:
            print(f"  > Sending file stream...")

        BUFFER_SIZE = 1024  # 进一步缩小 Buffer (512 -> 256)
        total_sent = 0
        import time
        while True:
            buf = source.read(BUFFER_SIZE)
            if not buf:
                break
            outputfile.write(buf)
            outputfile.flush()  # 关键：强制发送
            total_sent += len(buf)
            time.sleep(0.06)  # 50ms 延时，带宽约 5KB/s
            # print(f"    - Sent chunk: {len(buf)} bytes (Total: {total_sent})") # Verbose

        print(f"  > Transfer complete. Sent: {total_sent} bytes")


class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    """多线程 TCP 服务器"""
    allow_reuse_address = True  # 允许地址复用（快速重启）
    daemon_threads = True  # 后台线程（主线程退出时自动终止）


if __name__ == "__main__":
    print(f"\n{'=' * 50}")
    print(f"  OTA Server (Threaded)")
    print(f"{'=' * 50}")
    print(f"  URL:       \033[93mhttp://{get_ip()}:{PORT}\033[0m")
    print(f"  Directory: {os.path.abspath(DIRECTORY)}")
    print(f"  Timeout:   {TIMEOUT}s")
    print(f"{'=' * 50}\n")

    with ThreadedTCPServer(("", PORT), OTAHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n\033[91mServer stopped.\033[0m")
