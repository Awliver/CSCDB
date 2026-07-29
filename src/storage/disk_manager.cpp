/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/disk_manager.h"

#include <assert.h>    // for assert
#include <errno.h>     // for errno / EINTR
#include <string.h>    // for memset
#include <sys/stat.h>  // for stat
#include <unistd.h>    // for lseek / pwrite / ftruncate / fdatasync

#include "defs.h"

DiskManager::DiskManager() { memset(fd2pageno_, 0, MAX_FD * (sizeof(std::atomic<page_id_t>) / sizeof(char))); }

/**
 * @description: 将数据写入文件的指定磁盘页面中
 * @param {int} fd 磁盘文件的文件句柄
 * @param {page_id_t} page_no 写入目标页面的page_id
 * @param {char} *offset 要写入磁盘的数据
 * @param {int} num_bytes 要写入磁盘的数据大小
 */
void DiskManager::write_page(int fd, page_id_t page_no, const char *offset, int num_bytes) {
    off_t pos = static_cast<off_t>(page_no) * PAGE_SIZE;
    ssize_t bytes_written = pwrite(fd, offset, num_bytes, pos);
    if (bytes_written != num_bytes)
    {
        throw InternalError("DiskManager::write_page Error");
    }
}

/**
 * @description: 读取文件中指定编号的页面中的部分数据到内存中
 * @param {int} fd 磁盘文件的文件句柄
 * @param {page_id_t} page_no 指定的页面编号
 * @param {char} *offset 读取的内容写入到offset中
 * @param {int} num_bytes 读取的数据量大小
 */
void DiskManager::read_page(int fd, page_id_t page_no, char *offset, int num_bytes) {
    off_t pos = static_cast<off_t>(page_no) * PAGE_SIZE;
    ssize_t bytes_read = pread(fd, offset, num_bytes, pos);
    if (bytes_read != num_bytes)
    {
        // 诊断关键：短读=页从未写盘就被淘汰；got=-1 则看 errno（EBADF=fd 失效等）
        int err = errno;
        off_t fsize = lseek(fd, 0, SEEK_END);
        std::string path = "?";
        auto it = fd2path_.find(fd);
        if (it != fd2path_.end()) path = it->second;
        throw InternalError("DiskManager::read_page Error: file=" + path +
                            " fd=" + std::to_string(fd) +
                            " page=" + std::to_string(page_no) +
                            " got=" + std::to_string((long)bytes_read) +
                            " errno=" + std::string(bytes_read < 0 ? strerror(err) : "-") +
                            " fsize_pages=" + std::to_string((long)(fsize / PAGE_SIZE)));
    }
}

/**
 * @description: 分配一个新的页号
 * @return {page_id_t} 分配的新页号
 * @param {int} fd 指定文件的文件句柄
 */
page_id_t DiskManager::allocate_page(int fd) {
    // 简单的自增分配策略，指定文件的页面编号加1
    assert(fd >= 0 && fd < MAX_FD);
    return fd2pageno_[fd]++;
}

void DiskManager::deallocate_page(__attribute__((unused)) page_id_t page_id) {}

bool DiskManager::is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void DiskManager::create_dir(const std::string &path) {
    // Create a subdirectory
    std::string cmd = "mkdir " + path;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为path的目录
        throw UnixError();
    }
}

void DiskManager::destroy_dir(const std::string &path) {
    std::string cmd = "rm -r " + path;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 判断指定路径文件是否存在
 * @return {bool} 若指定路径文件存在则返回true
 * @param {string} &path 指定路径文件
 */
bool DiskManager::is_file(const std::string &path) {
    // 用struct stat获取文件信息
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * @description: 用于创建指定路径文件
 * @return {*}
 * @param {string} &path
 */
void DiskManager::create_file(const std::string &path) {
    if (is_file(path)) {
        throw FileExistsError(path);
    }
    int fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd == -1) throw UnixError();
    if (close(fd) != 0) throw UnixError();
}

/**
 * @description: 删除指定路径的文件
 * @param {string} &path 文件所在路径
 */
void DiskManager::destroy_file(const std::string &path) {
    if (path2fd_.count(path)) throw FileNotClosedError(path);
    if (!is_file(path)) throw FileNotFoundError(path);
    if (unlink(path.c_str()) != 0) throw UnixError();
}


/**
 * @description: 打开指定路径文件
 * @return {int} 返回打开的文件的文件句柄
 * @param {string} &path 文件所在路径
 */
int DiskManager::open_file(const std::string &path) {
    if (!is_file(path)) throw FileNotFoundError(path);
    auto it = path2fd_.find(path);
    if (it != path2fd_.end()) return it->second;
    int fd = open(path.c_str(), O_RDWR);
    if (fd == -1) throw UnixError();
    path2fd_[path] = fd;
    fd2path_[fd] = path;
    return fd;
}

/**
 * @description:用于关闭指定路径文件
 * @param {int} fd 打开的文件的文件句柄
 */
void DiskManager::close_file(int fd) {
    auto it = fd2path_.find(fd);
    if (it == fd2path_.end()) throw FileNotOpenError(fd);
    const std::string path = it->second;
    if (close(fd) != 0) throw UnixError();
    path2fd_.erase(path);
    fd2path_.erase(fd);
}


/**
 * @description: 获得文件的大小
 * @return {int} 文件的大小
 * @param {string} &file_name 文件名
 */
int DiskManager::get_file_size(const std::string &file_name) {
    struct stat stat_buf;
    int rc = stat(file_name.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

/**
 * @description: 根据文件句柄获得文件名
 * @return {string} 文件句柄对应文件的文件名
 * @param {int} fd 文件句柄
 */
std::string DiskManager::get_file_name(int fd) {
    if (!fd2path_.count(fd)) {
        throw FileNotOpenError(fd);
    }
    return fd2path_[fd];
}

/**
 * @description:  获得文件名对应的文件句柄
 * @return {int} 文件句柄
 * @param {string} &file_name 文件名
 */
int DiskManager::get_file_fd(const std::string &file_name) {
    if (!path2fd_.count(file_name)) {
        return open_file(file_name);
    }
    return path2fd_[file_name];
}


/**
 * @description:  读取日志文件内容
 * @return {int} 返回读取的数据量，若为-1说明读取数据的起始位置超过了文件大小
 * @param {char} *log_data 读取内容到log_data中
 * @param {int} size 读取的数据量大小
 * @param {int} offset 读取的内容在文件中的位置
 */
int DiskManager::read_log(char *log_data, int size, int offset) {
    // read log file from the previous end
    if (log_fd_ == -1) {
        log_fd_ = open_file(LOG_FILE_NAME);
        int fs = get_file_size(LOG_FILE_NAME);
        if (fs > 0) log_prealloc_end_ = fs;
    }
    int file_size = get_file_size(LOG_FILE_NAME);
    if (offset > file_size) {
        return -1;
    }

    size = std::min(size, file_size - offset);
    if(size == 0) return 0;
    lseek(log_fd_, offset, SEEK_SET);
    ssize_t bytes_read = read(log_fd_, log_data, size);
    assert(bytes_read == size);
    return bytes_read;
}

void DiskManager::ensure_log_capacity(long need_end) {
    if (log_fd_ == -1) {
        log_fd_ = open_file(LOG_FILE_NAME);
        int fs = get_file_size(LOG_FILE_NAME);
        if (fs > 0) log_prealloc_end_ = fs;
    }
    if (need_end <= log_prealloc_end_) return;
    long new_end = ((need_end / LOG_PREALLOC_CHUNK) + 1) * LOG_PREALLOC_CHUNK;
    if (ftruncate(log_fd_, new_end) != 0) {
        throw UnixError();
    }
    log_prealloc_end_ = new_end;
}

void DiskManager::reset_log_prealloc(long size) {
    if (log_fd_ == -1) {
        if (!is_file(LOG_FILE_NAME)) return;
        log_fd_ = open_file(LOG_FILE_NAME);
    }
    if (ftruncate(log_fd_, size) != 0) {
        throw UnixError();
    }
    log_prealloc_end_ = size;
}

/**
 * @description: 按逻辑偏移写日志（P1：pwrite + 预分配，避免每次 fdatasync 写 inode journal）
 */
void DiskManager::write_log(char *log_data, int size, long offset) {
    if (log_fd_ == -1) {
        log_fd_ = open_file(LOG_FILE_NAME);
        int fs = get_file_size(LOG_FILE_NAME);
        if (fs > 0) log_prealloc_end_ = fs;
    }
    ensure_log_capacity(offset + size);

    int remain = size;
    char *p = log_data;
    long off = offset;
    while (remain > 0) {
        ssize_t n = pwrite(log_fd_, p, remain, off);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw UnixError();
        }
        p += n;
        off += n;
        remain -= (int)n;
    }
}

void DiskManager::sync_log() {
    if (log_fd_ == -1) return;
    if (fdatasync(log_fd_) != 0) {
        throw UnixError();
    }
}
