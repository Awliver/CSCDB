/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sm_manager.h"

#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

#include "index/ix.h"
#include "record/rm.h"
#include "record_printer.h"

/**
 * @description: 判断是否为一个文件夹
 * @return {bool} 返回是否为一个文件夹
 * @param {string&} db_name 数据库文件名称，与文件夹同名
 */
bool SmManager::is_dir(const std::string& db_name) {
    struct stat st;
    return stat(db_name.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @description: 创建数据库，所有的数据库相关文件都放在数据库同名文件夹下
 * @param {string&} db_name 数据库名称
 */
void SmManager::create_db(const std::string& db_name) {
    if (is_dir(db_name)) {
        throw DatabaseExistsError(db_name);
    }
    //为数据库创建一个子目录
    std::string cmd = "mkdir " + db_name;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为db_name的目录
        throw UnixError();
    }
    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }
    //创建系统目录
    DbMeta *new_db = new DbMeta();
    new_db->name_ = db_name;

    // 注意，此处ofstream会在当前目录创建(如果没有此文件先创建)和打开一个名为DB_META_NAME的文件
    std::ofstream ofs(DB_META_NAME);

    // 将new_db中的信息，按照定义好的operator<<操作符，写入到ofs打开的DB_META_NAME文件中
    ofs << *new_db;  // 注意：此处重载了操作符<<

    delete new_db;

    // 创建日志文件
    disk_manager_->create_file(LOG_FILE_NAME);

    // 回到根目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 删除数据库，同时需要清空相关文件以及数据库同名文件夹
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::drop_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    std::string cmd = "rm -r " + db_name;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 打开数据库，找到数据库对应的文件夹，并加载数据库元数据和相关文件
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::open_db(const std::string& db_name) {
    // 检查数据库目录是否存在
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    // 进入目录
    if (chdir(db_name.c_str()) < 0) {
        throw UnixError();
    }
    // 从 db.meta 反序列化加载元数据到内存
    //    DbMeta 重载了 operator>>，会自动读取所有 TabMeta
    std::ifstream ifs(DB_META_NAME);
    ifs >> db_;
    ifs.close();

    // 遍历所有表，打开数据文件并加入 fhs_
    for (auto& entry : db_.tabs_) {
        const std::string& tab_name = entry.first;
        TabMeta& tab = entry.second;
        // RmManager::open_file 返回 unique_ptr<RmFileHandle>
        // 构造时会从磁盘读 RmFileHdr 到内存
        fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));

        // 打开该表上的所有索引
        for (auto& index : tab.indexes) {
            std::string index_name = ix_manager_->get_index_name(tab_name, index.cols);
            ihs_.emplace(index_name, ix_manager_->open_index(tab_name, index.cols));
        }
    }
}

/**
 * @description: 把数据库相关的元数据刷入磁盘中
 */
void SmManager::flush_meta() {
    // 默认清空文件
    std::ofstream ofs(DB_META_NAME);
    ofs << db_;
}

/**
 * @description: 关闭数据库并把数据落盘
 */
void SmManager::close_db() {
    // 内存里的db_ 元数据到db.meta
    flush_meta();

    // 关闭所有表文件
    //    rm_manager_->close_file ：
    //      a. 把 file_hdr 写回磁盘第 0 页
    //      b. flush_all_pages 把缓冲池中该 fd 的所有脏页刷盘
    //      c. close fd
    for (auto& entry : fhs_) {
        rm_manager_->close_file(entry.second.get());
    }
    fhs_.clear();

    // 关闭所有索引文件
    for (auto& entry : ihs_) {
        ix_manager_->close_index(entry.second.get());
    }
    ihs_.clear();

    // 重置内存中的元数据，防止下次 open_db 时残留旧数据
    db_.name_.clear();
    db_.tabs_.clear();

    // 回到数据库目录的上层
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context
 */
void SmManager::show_tables(Context* context) {
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << "| Tables |\n";
    RecordPrinter printer(1);
    printer.print_separator(context);
    printer.print_record({"Tables"}, context);
    printer.print_separator(context);
    for (auto &entry : db_.tabs_) {
        auto &tab = entry.second;
        printer.print_record({tab.name}, context);
        outfile << "| " << tab.name << " |\n";
    }
    printer.print_separator(context);
    outfile.close();
}

/**
 * @description: 显示表的元数据
 * @param {string&} tab_name 表名称
 * @param {Context*} context
 */
/**
 * @description: 显示某张表上的所有索引，写入 output.txt
 *               输出格式：| tab_name | unique | (col1,col2,...) |
 * @param {string&} tab_name 表名
 * @param {Context*} context
 */
void SmManager::show_indexes(const std::string& tab_name, Context* context) {
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }
    TabMeta& tab = db_.tabs_[tab_name];

    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    for (auto& index : tab.indexes) {
        outfile << "| " << tab_name << " | unique | (";
        for (size_t i = 0; i < index.cols.size(); ++i) {
            if (i > 0) outfile << ",";
            outfile << index.cols[i].name;
        }
        outfile << ") |\n";
    }
    outfile.close();
}

void SmManager::desc_table(const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);

    std::vector<std::string> captions = {"Field", "Type", "Index"};
    RecordPrinter printer(captions.size());
    // Print header
    printer.print_separator(context);
    printer.print_record(captions, context);
    printer.print_separator(context);
    // Print fields
    for (auto &col : tab.cols) {
        std::vector<std::string> field_info = {col.name, coltype2str(col.type), col.index ? "YES" : "NO"};
        printer.print_record(field_info, context);
    }
    // Print footer
    printer.print_separator(context);
}

/**
 * @description: 创建表
 * @param {string&} tab_name 表的名称
 * @param {vector<ColDef>&} col_defs 表的字段
 * @param {Context*} context
 */
void SmManager::create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context) {
    if (db_.is_table(tab_name)) {
        throw TableExistsError(tab_name);
    }
    // Create table meta
    int curr_offset = 0;
    TabMeta tab;
    tab.name = tab_name;
    for (auto &col_def : col_defs) {
        ColMeta col = {.tab_name = tab_name,
                       .name = col_def.name,
                       .type = col_def.type,
                       .len = col_def.len,
                       .offset = curr_offset,
                       .index = false};
        curr_offset += col_def.len;
        tab.cols.push_back(col);
    }
    // Create & open record file
    int record_size = curr_offset;  // record_size就是col meta所占的大小（表的元数据也是以记录的形式进行存储的）
    rm_manager_->create_file(tab_name, record_size);
    db_.tabs_[tab_name] = tab;
    // fhs_[tab_name] = rm_manager_->open_file(tab_name);
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));

    flush_meta();
}

/**
 * @description: 删除表
 * @param {string&} tab_name 表的名称
 * @param {Context*} context
 */
void SmManager::drop_table(const std::string& tab_name, Context* context) {
    // 检查表是否存在
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }

    // 删除该表上的所有索引
    //    注意：复制一份 indexes 再遍历，避免 drop_index 修改 tab.indexes 时迭代器失效
    TabMeta& tab = db_.tabs_[tab_name];
    auto indexes_copy = tab.indexes;
    for (auto& index : indexes_copy) {
        drop_index(tab_name, index.cols, context);
    }

    // 关闭表文件句柄
    //    先 close 再 destroy_file，否则 destroy_file 会抛出 FileNotClosedError
    rm_manager_->close_file(fhs_[tab_name].get());
    fhs_.erase(tab_name);

    // 删除磁盘上的表文件
    rm_manager_->destroy_file(tab_name);

    // 从内存元数据中移除
    db_.tabs_.erase(tab_name);

    // 新元数据到 db.meta
    flush_meta();
}

/**
 * @description: 创建索引
 * @param {string&} tab_name 表的名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    // 1. 检查表存在
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }
    TabMeta& tab = db_.tabs_[tab_name];

    // 2. 解析列并构造 IndexMeta
    IndexMeta index;
    index.tab_name = tab_name;
    index.col_num = col_names.size();
    index.col_tot_len = 0;
    for (auto& col_name : col_names) {
        auto col_it = tab.get_col(col_name);  // 列不存在则抛 ColumnNotFoundError
        index.cols.push_back(*col_it);
        index.col_tot_len += col_it->len;
    }

    // 3. 重复索引检测
    if (ix_manager_->exists(tab_name, index.cols)) {
        throw IndexExistsError(tab_name, col_names);
    }

    // 4. 创建索引文件 + 打开
    ix_manager_->create_index(tab_name, index.cols);
    auto ih = ix_manager_->open_index(tab_name, index.cols);

    // 5. 把表里现有所有记录都插到索引里（唯一索引：重复 key 会被 insert_entry 内部 ignored）
    auto fh = fhs_[tab_name].get();
    for (RmScan scan(fh); !scan.is_end(); scan.next()) {
        auto rec = fh->get_record(scan.rid(), context);
        char* key = new char[index.col_tot_len];
        int offset = 0;
        for (auto& col : index.cols) {
            memcpy(key + offset, rec->data + col.offset, col.len);
            offset += col.len;
        }
        ih->insert_entry(key, scan.rid(), context ? context->txn_ : nullptr);
        delete[] key;
    }

    // 6. 注册到 ihs_ + IndexMeta 加入 tab + 持久化
    std::string index_name = ix_manager_->get_index_name(tab_name, index.cols);
    ihs_.emplace(index_name, std::move(ih));
    tab.indexes.push_back(index);
    flush_meta();
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    // 1. 检查表存在
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }
    TabMeta& tab = db_.tabs_[tab_name];

    // 2. 找到 IndexMeta（get_index_meta 找不到会抛 IndexNotFoundError）
    auto index_it = tab.get_index_meta(col_names);
    auto cols = index_it->cols;
    std::string index_name = ix_manager_->get_index_name(tab_name, cols);

    // 3. 关闭索引文件 + 从 ihs_ 移除
    auto ih_it = ihs_.find(index_name);
    if (ih_it != ihs_.end()) {
        ix_manager_->close_index(ih_it->second.get());
        ihs_.erase(ih_it);
    }

    // 4. 删除索引文件
    ix_manager_->destroy_index(tab_name, cols);

    // 5. 从 TabMeta 移除并持久化
    tab.indexes.erase(index_it);
    flush_meta();
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<ColMeta>&} 索引包含的字段元数据
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<ColMeta>& cols, Context* context) {
    // 用 ColMeta 直接调用——drop_table 走的这个重载（避免拼装 col_names）
    std::vector<std::string> col_names;
    col_names.reserve(cols.size());
    for (auto& c : cols) col_names.push_back(c.name);
    drop_index(tab_name, col_names, context);
}
