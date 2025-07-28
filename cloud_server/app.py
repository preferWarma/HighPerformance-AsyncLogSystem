# app.py
from flask import Flask, request, jsonify, render_template, send_file, abort
from flask_sqlalchemy import SQLAlchemy
from flask_cors import CORS
from werkzeug.utils import secure_filename
from datetime import datetime
import os
import json
import hashlib

app = Flask(__name__)
app.config['SECRET_KEY'] = '123456'
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///logs.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
app.config['UPLOAD_FOLDER'] = os.path.join(os.path.dirname(__file__), 'storage', 'logs')
app.config['MAX_CONTENT_LENGTH'] = 100 * 1024 * 1024  # 100MB
app.config['ALLOWED_EXTENSIONS'] = {'txt', 'log'}

# 确保上传目录存在
os.makedirs(app.config['UPLOAD_FOLDER'], exist_ok=True)

db = SQLAlchemy(app)
CORS(app)  # 允许跨域请求

# 数据模型
class LogFile(db.Model):
    __tablename__ = 'log_files'
    
    id = db.Column(db.Integer, primary_key=True)
    filename = db.Column(db.String(255), nullable=False)
    original_filename = db.Column(db.String(255), nullable=False)
    file_path = db.Column(db.String(500), nullable=False)
    file_size = db.Column(db.BigInteger, nullable=False)
    file_hash = db.Column(db.String(64))  # SHA256哈希，用于去重
    upload_time = db.Column(db.DateTime, default=datetime.utcnow)
    client_info = db.Column(db.String(500))
    is_deleted = db.Column(db.Boolean, default=False)
    
    def to_dict(self):
        return {
            'id': self.id,
            'filename': self.original_filename,
            'file_size': self.file_size,
            'upload_time': self.upload_time.isoformat(),
            'client_info': self.client_info
        }
    
    def __repr__(self):
        return f'<LogFile {self.original_filename}>'

# 工具函数
def allowed_file(filename):
    return '.' in filename and \
           filename.rsplit('.', 1)[1].lower() in app.config['ALLOWED_EXTENSIONS']

def calculate_file_hash(file_path):
    """计算文件的SHA256哈希值"""
    hash_sha256 = hashlib.sha256()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_sha256.update(chunk)
    return hash_sha256.hexdigest()

def format_file_size(size_bytes):
    """格式化文件大小"""
    if size_bytes == 0:
        return "0B"
    size_names = ["B", "KB", "MB", "GB", "TB"]
    import math
    i = int(math.floor(math.log(size_bytes, 1024)))
    p = math.pow(1024, i)
    s = round(size_bytes / p, 2)
    return f"{s} {size_names[i]}"

# API路由
@app.route('/')
def index():
    """主页面"""
    return render_template('index.html')

@app.route('/api/upload', methods=['POST'])
def upload_log_file():
    """接收日志文件上传"""
    try:
        if 'file' not in request.files:
            return jsonify({'error': 'No file provided'}), 400
        
        file = request.files['file']
        if file.filename == '':
            return jsonify({'error': 'No file selected'}), 400
        
        if not allowed_file(file.filename):
            return jsonify({'error': 'Invalid file type. Only .txt and .log files are allowed'}), 400
        
        # 生成安全的文件名
        original_filename = file.filename
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        filename = f"{timestamp}_{secure_filename(original_filename)}"
        file_path = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        
        # 保存文件
        file.save(file_path)
        
        # 计算文件信息
        file_size = os.path.getsize(file_path)
        file_hash = calculate_file_hash(file_path)
        
        # 检查是否已存在相同文件
        existing_file = LogFile.query.filter_by(file_hash=file_hash, is_deleted=False).first()
        if existing_file:
            # 删除重复文件
            os.remove(file_path)
            return jsonify({
                'message': 'File already exists',
                'file_id': existing_file.id,
                'existing_filename': existing_file.original_filename
            }), 200
        
        # 创建数据库记录
        log_file = LogFile(
            filename=filename,
            original_filename=original_filename,
            file_path=file_path,
            file_size=file_size,
            file_hash=file_hash,
            upload_time=datetime.utcnow(),
            client_info=request.headers.get('User-Agent', '')
        )
        
        db.session.add(log_file)
        db.session.commit()
        
        return jsonify({
            'message': 'File uploaded successfully',
            'file_id': log_file.id,
            'filename': original_filename,
            'file_size': file_size
        }), 200
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs', methods=['GET'])
def get_log_files():
    """获取日志文件列表"""
    try:
        page = request.args.get('page', 1, type=int)
        per_page = min(request.args.get('per_page', 20, type=int), 100)  # 限制最大值
        search = request.args.get('search', '').strip()
        
        query = LogFile.query.filter_by(is_deleted=False)
        
        if search:
            query = query.filter(LogFile.original_filename.contains(search))
        
        pagination = query.order_by(LogFile.upload_time.desc()).paginate(
            page=page, per_page=per_page, error_out=False
        )
        
        files = [file.to_dict() for file in pagination.items]
        
        return jsonify({
            'files': files,
            'total': pagination.total,
            'pages': pagination.pages,
            'current_page': page,
            'per_page': per_page
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs/<int:file_id>', methods=['GET'])
def get_log_file_info(file_id):
    """获取单个日志文件信息"""
    try:
        log_file = LogFile.query.filter_by(id=file_id, is_deleted=False).first()
        if not log_file:
            return jsonify({'error': 'File not found'}), 404
        
        return jsonify(log_file.to_dict())
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs/<int:file_id>/content', methods=['GET'])
def get_log_content(file_id):
    """获取日志文件内容"""
    try:
        log_file = LogFile.query.filter_by(id=file_id, is_deleted=False).first()
        if not log_file:
            return jsonify({'error': 'File not found'}), 404
        
        if not os.path.exists(log_file.file_path):
            return jsonify({'error': 'File not found on disk'}), 404
        
        start_line = max(0, request.args.get('start', 0, type=int))
        lines_count = min(1000, request.args.get('lines', 100, type=int))  # 限制最大行数
        search_term = request.args.get('search', '').strip()
        
        with open(log_file.file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
        
        total_lines = len(lines)
        
        # 如果有搜索词，过滤行
        if search_term:
            filtered_lines = []
            line_numbers = []
            for i, line in enumerate(lines):
                if search_term.lower() in line.lower():
                    filtered_lines.append(line)
                    line_numbers.append(i + 1)  # 行号从1开始
            lines = filtered_lines
            # 重新计算分页
            end_index = start_line + lines_count
            selected_lines = lines[start_line:end_index]
            actual_line_numbers = line_numbers[start_line:end_index] if line_numbers else []
        else:
            end_line = start_line + lines_count
            selected_lines = lines[start_line:end_line]
            actual_line_numbers = list(range(start_line + 1, min(end_line + 1, total_lines + 1)))
        
        return jsonify({
            'content': selected_lines,
            'line_numbers': actual_line_numbers,
            'total_lines': len(lines),  # 过滤后的总行数
            'original_total_lines': total_lines,  # 原始总行数
            'start_line': start_line,
            'end_line': min(start_line + len(selected_lines), len(lines)),
            'has_search': bool(search_term)
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs/<int:file_id>/download', methods=['GET'])
def download_log_file(file_id):
    """下载日志文件"""
    try:
        log_file = LogFile.query.filter_by(id=file_id, is_deleted=False).first()
        if not log_file:
            abort(404)
        
        if not os.path.exists(log_file.file_path):
            abort(404)
        
        return send_file(
            log_file.file_path,
            as_attachment=True,
            download_name=log_file.original_filename,
            mimetype='text/plain'
        )
        
    except Exception as e:
        abort(500)

@app.route('/api/logs/<int:file_id>', methods=['DELETE'])
def delete_log_file(file_id):
    """删除日志文件"""
    try:
        log_file = LogFile.query.filter_by(id=file_id, is_deleted=False).first()
        if not log_file:
            return jsonify({'error': 'File not found'}), 404
        
        # 标记为已删除而不是物理删除
        log_file.is_deleted = True
        db.session.commit()
        
        # 可选：物理删除文件
        try:
            if os.path.exists(log_file.file_path):
                os.remove(log_file.file_path)
        except OSError:
            pass  # 忽略删除文件失败的情况
        
        return jsonify({'message': 'File deleted successfully'})
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/stats', methods=['GET'])
def get_statistics():
    """获取统计信息"""
    try:
        total_files = LogFile.query.filter_by(is_deleted=False).count()
        total_size = db.session.query(db.func.sum(LogFile.file_size)).filter_by(is_deleted=False).scalar() or 0
        
        # 按日期统计
        from sqlalchemy import func, extract
        daily_stats = db.session.query(
            func.date(LogFile.upload_time).label('date'),
            func.count(LogFile.id).label('count'),
            func.sum(LogFile.file_size).label('size')
        ).filter_by(is_deleted=False).group_by(func.date(LogFile.upload_time)).limit(30).all()
        
        daily_data = []
        for stat in daily_stats:
            daily_data.append({
                'date': stat.date.isoformat() if stat.date else '',
                'count': stat.count,
                'size': stat.size or 0
            })
        
        return jsonify({
            'total_files': total_files,
            'total_size': total_size,
            'total_size_formatted': format_file_size(total_size),
            'daily_stats': daily_data
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

# 错误处理
@app.errorhandler(404)
def not_found(error):
    return jsonify({'error': 'Not found'}), 404

@app.errorhandler(500)
def internal_error(error):
    return jsonify({'error': 'Internal server error'}), 500

@app.errorhandler(413)
def too_large(error):
    return jsonify({'error': 'File too large'}), 413

# 初始化数据库
def init_db():
    db.create_all()

@app.cli.command("init-db")
def init_db_command():
    """Clear the existing data and create new tables."""
    init_db()
    print("Initialized the database.")

if __name__ == '__main__':
    # 开发环境配置
    app.run(debug=True, host='0.0.0.0', port=50000)