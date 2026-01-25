#!/usr/bin/env python3
"""
简单的HTTP服务器用于测试HttpSink
接收POST请求并打印日志内容
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
from datetime import datetime

class LogHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length)
        
        print(f"\n[{datetime.now()}] 收到日志请求:")
        print(f"路径: {self.path}")
        print(f"Content-Type: {self.headers.get('Content-Type', 'N/A')}")
        print(f"Content-Length: {content_length} bytes")
        
        try:
            data = json.loads(post_data.decode('utf-8'))
            print(f"日志内容:")
            print(json.dumps(data, indent=2, ensure_ascii=False))
            
            # 返回成功响应
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            response = {"status": "success", "message": "日志已接收"}
            self.wfile.write(json.dumps(response).encode('utf-8'))
            
        except json.JSONDecodeError as e:
            print(f"JSON解析错误: {e}")
            self.send_response(400)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            response = {"status": "error", "message": "无效的JSON数据"}
            self.wfile.write(json.dumps(response).encode('utf-8'))
        except Exception as e:
            print(f"处理错误: {e}")
            self.send_response(500)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            response = {"status": "error", "message": str(e)}
            self.wfile.write(json.dumps(response).encode('utf-8'))
    
    def log_message(self, format, *args):
        pass  # 禁用默认日志输出

def run_server(port=8080):
    server_address = ('', port)
    httpd = HTTPServer(server_address, LogHandler)
    print(f"HTTP测试服务器启动在端口 {port}")
    print(f"监听路径: /api/logs")
    print("按 Ctrl+C 停止服务器\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n服务器已停止")
        httpd.server_close()

if __name__ == '__main__':
    run_server(8080)
