/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <cstring>
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"

namespace {
// #region agent log
inline void debug_log_union(const char *run_id, const char *hypothesis_id, const std::string &location,
                            const std::string &message, const std::string &data) {
    std::ofstream ofs("/home/neo/CSC_DB/db2026/.cursor/debug-b42dcf.log", std::ios::app);
    if (!ofs.is_open()) return;
    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    ofs << "{\"sessionId\":\"b42dcf\",\"runId\":\"" << run_id << "\",\"hypothesisId\":\"" << hypothesis_id
        << "\",\"location\":\"" << location << "\",\"message\":\"" << message << "\",\"data\":\"" << data
        << "\",\"timestamp\":" << ts << "}\n";
}
// #endregion
}  // namespace

class UnionExecutor : public AbstractExecutor {
   private:
    std::vector<std::unique_ptr<AbstractExecutor>> children_;
    std::vector<ColMeta> cols_;
    size_t len_ = 0;
    std::vector<std::unique_ptr<RmRecord>> rows_;
    size_t idx_ = 0;

    void write_value(char *dest, const ColMeta &dst_col, const char *src, const ColMeta &src_col) {
        if (dst_col.type == TYPE_FLOAT && src_col.type == TYPE_INT) {
            float v = static_cast<float>(*reinterpret_cast<const int *>(src));
            memcpy(dest, &v, sizeof(float));
        } else if (dst_col.type == TYPE_INT && src_col.type == TYPE_INT) {
            memcpy(dest, src, sizeof(int));
        } else if (dst_col.type == TYPE_FLOAT && src_col.type == TYPE_FLOAT) {
            memcpy(dest, src, sizeof(float));
        } else if (dst_col.type == TYPE_STRING && src_col.type == TYPE_STRING) {
            memset(dest, 0, dst_col.len);
            memcpy(dest, src, std::min(dst_col.len, src_col.len));
        } else {
            throw InternalError("failure");
        }
    }

   public:
    UnionExecutor(std::vector<std::unique_ptr<AbstractExecutor>> children, std::vector<ColMeta> output_cols) {
        children_ = std::move(children);
        cols_ = std::move(output_cols);
        len_ = cols_.empty() ? 0 : cols_.back().offset + cols_.back().len;
    }

    void beginTuple() override {
        rows_.clear();
        idx_ = 0;
        std::set<std::string> seen;
        // #region agent log
        debug_log_union("run2", "H5", "executor_union.h:66", "union beginTuple",
                        "children=" + std::to_string(children_.size()) + ",output_cols=" + std::to_string(cols_.size()) +
                            ",len=" + std::to_string(len_));
        // #endregion
        for (auto &child : children_) {
            const auto &child_cols = child->cols();
            if (child_cols.size() != cols_.size()) {
                // #region agent log
                debug_log_union("run2", "H6", "executor_union.h:73", "child cols mismatch",
                                "child_cols=" + std::to_string(child_cols.size()) +
                                    ",output_cols=" + std::to_string(cols_.size()));
                // #endregion
                throw InternalError("failure");
            }
            for (child->beginTuple(); !child->is_end(); child->nextTuple()) {
                auto src = child->Next();
                if (src == nullptr) {
                    // #region agent log
                    debug_log_union("run2", "H7", "executor_union.h:82", "child Next returned nullptr", "");
                    // #endregion
                    throw InternalError("failure");
                }
                auto out = std::make_unique<RmRecord>(len_);
                for (size_t i = 0; i < cols_.size(); i++) {
                    write_value(out->data + cols_[i].offset, cols_[i],
                                src->data + child_cols[i].offset, child_cols[i]);
                }
                std::string key(out->data, len_);
                if (seen.insert(key).second) {
                    rows_.push_back(std::move(out));
                }
            }
        }
        // #region agent log
        debug_log_union("run2", "H5", "executor_union.h:97", "union rows produced",
                        "rows=" + std::to_string(rows_.size()));
        // #endregion
    }

    void nextTuple() override { idx_++; }

    bool is_end() const override { return idx_ >= rows_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        if (idx_ >= rows_.size()) return nullptr;
        return std::make_unique<RmRecord>(*rows_[idx_]);
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return _abstract_rid; }
};
