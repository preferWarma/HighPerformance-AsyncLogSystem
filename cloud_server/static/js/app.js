// static/js/app.js
class LogManager {
    constructor() {
        this.currentPage = 1;
        this.currentFileId = null;
        this.currentStartLine = 0;
        this.linesPerPage = 100;
        this.filesPerPage = 20;
    }

    // 加载文件列表
    async loadFileList(page = 1, search = '') {
        try {
            const response = await fetch(`/api/logs?page=${page}&per_page=${this.filesPerPage}&search=${search}`);
            const data = await response.json();

            this.renderFileTable(data.files);
            this.renderPaginationInfo(data);
            this.renderPagination(data);
            this.currentPage = page;
        } catch (error) {
            console.error('加载文件列表失败:', error);
        }
    }

    // 渲染文件表格
    renderFileTable(files) {
        const tbody = document.getElementById('fileTableBody');
        tbody.innerHTML = '';

        files.forEach(file => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${file.filename}</td>
                <td>${this.formatFileSize(file.file_size)}</td>
                <td>${this.formatDateTime(file.upload_time)}</td>
                <td>
                    <button class="btn btn-sm btn-primary" onclick="logManager.viewLogContent(${file.id}, '${file.filename}')">
                        查看
                    </button>
                    <button class="btn btn-sm btn-success" onclick="logManager.downloadFile(${file.id})">
                        下载
                    </button>
                    <button class="btn btn-sm btn-danger" onclick="logManager.deleteFile(${file.id}, '${file.filename}')">
                        删除
                    </button>
                </td>
            `;
            tbody.appendChild(row);
        });
    }

    // 渲染分页信息
    renderPaginationInfo(data) {
        const container = document.getElementById('fileListContainer');
        let infoDiv = document.getElementById('paginationInfo');
        
        if (!infoDiv) {
            infoDiv = document.createElement('div');
            infoDiv.id = 'paginationInfo';
            infoDiv.className = 'pagination-info text-muted mb-3';
            container.insertBefore(infoDiv, container.firstChild);
        }
        
        infoDiv.innerHTML = `
            共 ${data.total} 个文件，每页显示 ${data.per_page} 个，
            第 ${data.current_page}/${data.pages || 1} 页
        `;
    }

    // 渲染分页控件
    renderPagination(data) {
        const pagination = document.getElementById('pagination');
        pagination.innerHTML = '';

        if (!data.pages || data.pages <= 1) {
            return;
        }

        // 首页
        const firstLi = document.createElement('li');
        firstLi.className = `page-item ${data.current_page === 1 ? 'disabled' : ''}`;
        firstLi.innerHTML = `
            <a class="page-link" href="#" onclick="logManager.loadFileList(1, document.getElementById('searchInput').value)">
                首页
            </a>
        `;
        pagination.appendChild(firstLi);

        // 上一页
        const prevLi = document.createElement('li');
        prevLi.className = `page-item ${data.current_page === 1 ? 'disabled' : ''}`;
        prevLi.innerHTML = `
            <a class="page-link" href="#" onclick="logManager.loadFileList(${data.current_page - 1}, document.getElementById('searchInput').value)">
                上一页
            </a>
        `;
        pagination.appendChild(prevLi);

        // 页码
        let startPage = Math.max(1, data.current_page - 2);
        let endPage = Math.min(data.pages, data.current_page + 2);

        // 确保显示足够的页码
        if (endPage - startPage < 4) {
            if (startPage === 1) {
                endPage = Math.min(5, data.pages);
            } else if (endPage === data.pages) {
                startPage = Math.max(1, data.pages - 4);
            }
        }

        for (let i = startPage; i <= endPage; i++) {
            const li = document.createElement('li');
            li.className = `page-item ${i === data.current_page ? 'active' : ''}`;
            li.innerHTML = `
                <a class="page-link" href="#" onclick="logManager.loadFileList(${i}, document.getElementById('searchInput').value)">
                    ${i}
                </a>
            `;
            pagination.appendChild(li);
        }

        // 下一页
        const nextLi = document.createElement('li');
        nextLi.className = `page-item ${data.current_page === data.pages ? 'disabled' : ''}`;
        nextLi.innerHTML = `
            <a class="page-link" href="#" onclick="logManager.loadFileList(${data.current_page + 1}, document.getElementById('searchInput').value)">
                下一页
            </a>
        `;
        pagination.appendChild(nextLi);

        // 末页
        const lastLi = document.createElement('li');
        lastLi.className = `page-item ${data.current_page === data.pages ? 'disabled' : ''}`;
        lastLi.innerHTML = `
            <a class="page-link" href="#" onclick="logManager.loadFileList(${data.pages}, document.getElementById('searchInput').value)">
                末页(${data.pages})
            </a>
        `;
        pagination.appendChild(lastLi);
    }

    // 查看日志内容
    async viewLogContent(fileId, filename) {
        this.currentFileId = fileId;
        this.currentStartLine = 0;

        document.querySelector('#logContentModal .modal-title').textContent = `日志内容 - ${filename}`;

        await this.loadLogContent();

        const modal = new bootstrap.Modal(document.getElementById('logContentModal'));
        modal.show();
    }

    // 加载日志内容
    async loadLogContent(search = '') {
        try {
            const url = `/api/logs/${this.currentFileId}/content?start=${this.currentStartLine}&lines=${this.linesPerPage}&search=${search}`;
            const response = await fetch(url);
            const data = await response.json();

            // 解析日志内容并添加颜色样式
            const formattedContent = this.formatLogContent(data.content);
            document.getElementById('logContentDisplay').innerHTML = formattedContent;

            document.getElementById('logContentDisplay').textContent = data.content.join('');
            document.getElementById('lineInfo').textContent =
                `第 ${data.start_line + 1}-${data.end_line} 行 (共 ${data.total_lines} 行)`;
        } catch (error) {
            console.error('加载日志内容失败:', error);
        }
    }

    // 工具函数
    formatFileSize(bytes) {
        const units = ['B', 'KB', 'MB', 'GB'];
        let size = bytes;
        let unitIndex = 0;

        while (size >= 1024 && unitIndex < units.length - 1) {
            size /= 1024;
            unitIndex++;
        }

        return `${size.toFixed(2)} ${units[unitIndex]}`;
    }

    formatDateTime(dateString) {
        const date = new Date(dateString);
        return date.toLocaleString('zh-CN');
    }

    // 格式化日志内容，根据日志级别添加颜色样式
    formatLogContent(lines) {
        return lines.map(line => {
            if (line.includes('DEBUG')) {
                return `<span class="log-level-debug">${line}</span>`;
            } else if (line.includes('INFO')) {
                return `<span class="log-level-info">${line}</span>`;
            } else if (line.includes('WARN')) {
                return `<span class="log-level-warn">${line}</span>`;
            } else if (line.includes('ERROR')) {
                return `<span class="log-level-error">${line}</span>`;
            } else if (line.includes('FATAL')) {
                return `<span class="log-level-fatal">${line}</span>`;
            } else {
                return line;
            }
        }).join('');
    }
}

// 初始化
const logManager = new LogManager();

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', function () {
    logManager.loadFileList();
});

// 搜索功能
function searchFiles() {
    const searchTerm = document.getElementById('searchInput').value;
    logManager.loadFileList(1, searchTerm);
}

// 刷新功能
function refreshFiles() {
    document.getElementById('searchInput').value = '';
    logManager.loadFileList(1);
}

// 显示统计信息
function showStatistics() {
    // TODO: 实现统计信息显示功能
    alert('统计信息功能待实现');
}

// 在日志内容中搜索
function searchInContent() {
    const searchTerm = document.getElementById('contentSearch').value;
    logManager.loadLogContent(searchTerm);
}

// 下载文件
LogManager.prototype.downloadFile = function (fileId) {
    if (!fileId) {
        console.error('文件ID不能为空');
        return;
    }
    window.location.href = `/api/logs/${fileId}/download`;
};

// 删除文件
LogManager.prototype.deleteFile = async function (fileId, filename) {
    if (!fileId) {
        console.error('文件ID不能为空');
        return;
    }

    // 确认删除
    if (!confirm(`确定要删除文件 "${filename}" 吗？`)) {
        return;
    }

    try {
        const response = await fetch(`/api/logs/${fileId}`, {
            method: 'DELETE'
        });

        const data = await response.json();

        if (response.ok) {
            alert('文件删除成功');
            // 重新加载文件列表
            this.loadFileList(this.currentPage);
        } else {
            alert(`删除失败: ${data.error}`);
        }
    } catch (error) {
        console.error('删除文件失败:', error);
        alert('删除文件失败');
    }
};

// 加载前一页日志内容
LogManager.prototype.loadPrevLines = function () {
    this.currentStartLine = Math.max(0, this.currentStartLine - this.linesPerPage);
    this.loadLogContent();
};

// 加载后一页日志内容
LogManager.prototype.loadNextLines = function () {
    // 获取显示区域的文本内容来确定总行数
    const lineInfoText = document.getElementById('lineInfo').textContent;
    const totalLinesMatch = lineInfoText.match(/共 (\d+) 行/);
    if (totalLinesMatch) {
        const totalLines = parseInt(totalLinesMatch[1]);
        // 检查是否有下一页内容
        if (this.currentStartLine + this.linesPerPage < totalLines) {
            this.currentStartLine += this.linesPerPage;
            this.loadLogContent();
        }
    } else {
        // 如果无法获取总行数，仍然尝试加载下一页
        this.currentStartLine += this.linesPerPage;
        this.loadLogContent();
    }
};