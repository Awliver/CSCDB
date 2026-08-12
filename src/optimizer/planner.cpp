/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "planner.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "index/ix.h"
#include "record_printer.h"

namespace {
std::vector<TabCol> plan_output_cols(const std::shared_ptr<Plan> &plan);
bool mvcc_force_seqscan(Context *context, const std::string &tab, size_t n_tables,
                        const std::vector<Condition> &conds);
}

// 最左匹配规则：对表上每条索引按 cols 顺序贪心匹配前缀。
// 列要么能找到 OP_EQ 条件（继续匹配后续列），要么找到 OP_LT/GT/LE/GE 条件（匹配此列后停止）。
// 跨多条索引时选匹配前缀最长的。
bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds,
                             std::vector<std::string>& index_col_names,
                             const std::string &binding_name) {
    index_col_names.clear();
    TabMeta& tab = sm_manager_->db_.get_table(tab_name);
    const std::string &condition_table = binding_name.empty() ? tab_name : binding_name;

    int best_match_len = 0;
    const IndexMeta* best_index = nullptr;

    for (auto& index : tab.indexes) {
        int match_len = 0;
        for (auto& idx_col : index.cols) {
            bool found_eq = false;
            bool found_range = false;
            for (auto& cond : curr_conds) {
                if (!cond.is_rhs_val) continue;
                if (cond.lhs_col.tab_name != condition_table) continue;
                if (cond.lhs_col.col_name != idx_col.name) continue;
                if (cond.op == OP_EQ) { found_eq = true; break; }
                if (cond.op == OP_LT || cond.op == OP_GT || cond.op == OP_LE || cond.op == OP_GE) {
                    found_range = true;
                    // 不 break——继续看有没有同列的 OP_EQ（优先 EQ）
                }
            }
            if (found_eq) {
                match_len++;
            } else if (found_range) {
                match_len++;
                break;  // 范围列匹配后停止——后续列即使匹配也无序
            } else {
                break;  // 该列没有可用条件
            }
        }
        if (match_len > best_match_len) {
            best_match_len = match_len;
            best_index = &index;
        }
    }

    // 决赛 Delivery 模板 `sum(ol_amount) where ol_o_id=? and ol_d_id=?` 缺索引首列：
    // 常规最左匹配失败会退化全表扫（OJ 1500 万行必超时）。若某索引"首列无条件、
    // 第 2 列起有连续 EQ"，选它做 index skip scan（executor 枚举首列 distinct 值 ×
    // 后续 EQ 子范围）。仅在无常规前缀匹配时启用。
    if (best_match_len == 0) {
        for (auto& index : tab.indexes) {
            if (index.cols.size() < 2) continue;
            int skip_eq = 0;
            for (size_t ci = 1; ci < index.cols.size(); ci++) {
                bool found_eq = false;
                for (auto& cond : curr_conds) {
                    if (cond.is_rhs_val && cond.op == OP_EQ &&
                        cond.lhs_col.tab_name == condition_table &&
                        cond.lhs_col.col_name == index.cols[ci].name) {
                        found_eq = true;
                        break;
                    }
                }
                if (!found_eq) break;
                skip_eq++;
            }
            // 首列自身不得有条件（有条件则常规匹配早已命中）
            bool first_has_cond = false;
            for (auto& cond : curr_conds) {
                if (cond.is_rhs_val && cond.lhs_col.tab_name == condition_table &&
                    cond.lhs_col.col_name == index.cols[0].name) {
                    first_has_cond = true;
                    break;
                }
            }
            if (skip_eq >= 1 && !first_has_cond) {
                best_index = &index;
                best_match_len = skip_eq;   // 标记选中；executor 侧自行识别 skip 形态
                break;
            }
        }
    }

    if (best_match_len == 0) return false;
    // 输出整条索引的全部 col_names（让 get_index_meta 能完整命中）
    // IndexScanExecutor 自己再分析 predicates_ 决定能用几列做 key
    for (auto& col : best_index->cols) {
        index_col_names.push_back(col.name);
    }
    return true;
}

bool Planner::get_join_index_cols(const std::string &right_table, const std::string &right_binding,
                                  const std::vector<Condition> &scan_conds,
                                  const std::vector<Condition> &join_conds,
                                  std::vector<std::string> &index_col_names) {
    index_col_names.clear();
    TabMeta &tab = sm_manager_->db_.get_table(right_table);

    auto is_join_eq_on_col = [&](const std::string &col_name) {
        for (auto &cond : join_conds) {
            if (cond.op != OP_EQ || cond.is_rhs_val) continue;
            if ((cond.lhs_col.tab_name == right_binding && cond.lhs_col.col_name == col_name) ||
                (cond.rhs_col.tab_name == right_binding && cond.rhs_col.col_name == col_name)) {
                return true;
            }
        }
        return false;
    };

    int best_match_len = 0;
    const IndexMeta *best_index = nullptr;
    for (auto &index : tab.indexes) {
        int match_len = 0;
        for (auto &idx_col : index.cols) {
            bool matched = false;
            for (auto &cond : scan_conds) {
                if (!cond.is_rhs_val || cond.op != OP_EQ) continue;
                if (cond.lhs_col.tab_name != right_binding) continue;
                if (cond.lhs_col.col_name != idx_col.name) continue;
                matched = true;
                break;
            }
            if (!matched) matched = is_join_eq_on_col(idx_col.name);
            if (!matched) break;
            match_len++;
        }
        if (match_len > best_match_len) {
            best_match_len = match_len;
            best_index = &index;
        }
    }

    if (best_match_len == 0 || best_index == nullptr) return false;
    bool has_join_key = false;
    for (auto &col : best_index->cols) {
        if (is_join_eq_on_col(col.name)) {
            has_join_key = true;
            break;
        }
    }
    if (!has_join_key) return false;

    for (auto &col : best_index->cols) index_col_names.push_back(col.name);
    return true;
}

std::shared_ptr<Plan> Planner::make_join_plan(std::shared_ptr<Plan> left, std::shared_ptr<Plan> right,
                                              std::vector<Condition> join_conds,
                                              JoinType join_type, Context *context) {
    auto right_scan = std::dynamic_pointer_cast<ScanPlan>(right);
    auto right_projection = std::dynamic_pointer_cast<ProjectionPlan>(right);
    if (right_scan == nullptr && right_projection != nullptr) {
        right_scan = std::dynamic_pointer_cast<ScanPlan>(right_projection->subplan_);
    }
    if (join_type == INNER_JOIN && right_scan != nullptr &&
        !mvcc_force_seqscan(context, right_scan->tab_name_, 2, right_scan->predicates_)) {
            // 只有 INNER JOIN 才选择 INLJ
        std::vector<std::string> index_col_names;
        if (get_join_index_cols(right_scan->tab_name_, right_scan->binding_name_,
                                right_scan->predicates_, join_conds, index_col_names)) {
            right_scan->tag = T_IndexScan;
            right_scan->index_col_names_ = std::move(index_col_names);
            // IndexNestedLoopJoinExecutor 需要直接接收 ScanPlan。若逻辑优化已在
            // 右分支放置投影，则先执行 INLJ，再立即投影为“左侧现有列 + 右侧
            // 必需列”；对上层连接而言与在右分支投影等价，且不会禁用索引连接。
            if (right_projection != nullptr) {
                auto output_cols = plan_output_cols(left);
                output_cols.insert(output_cols.end(), right_projection->sel_cols_.begin(),
                                   right_projection->sel_cols_.end());
                auto join = std::make_shared<JoinPlan>(T_IndexNestLoop, join_type, std::move(left),
                                                       std::move(right_scan), std::move(join_conds));
                return std::make_shared<ProjectionPlan>(T_Projection, std::move(join),
                                                        std::move(output_cols));
            }
            return std::make_shared<JoinPlan>(T_IndexNestLoop, join_type, std::move(left), std::move(right),
                                              std::move(join_conds));
        }
    }
    return std::make_shared<JoinPlan>(T_NestLoop, join_type, std::move(left), std::move(right),
                                      std::move(join_conds));
}

namespace {

// 返回计划节点真正向父节点暴露的列，用于 INLJ 右侧投影改写。INLJ 必须直接
// 持有 ScanPlan，因此把右侧投影等价地放到 INLJ 之上时，需要同时保留左侧列。
std::vector<TabCol> plan_output_cols(const std::shared_ptr<Plan> &plan) {
    if (auto projection = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        return projection->sel_cols_;
    }
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        std::vector<TabCol> cols;
        cols.reserve(scan->cols_.size());
        for (const auto &col : scan->cols_) cols.push_back({col.tab_name, col.name});
        return cols;
    }
    if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        auto cols = plan_output_cols(join->left_);
        auto right_cols = plan_output_cols(join->right_);
        cols.insert(cols.end(), right_cols.begin(), right_cols.end());
        return cols;
    }
    if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        return plan_output_cols(sort->subplan_);
    }
    if (auto filter = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        return plan_output_cols(filter->subplan_);
    }
    if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        return plan_output_cols(limit->subplan_);
    }
    return {};
}

}  // namespace


// 表上是否有范围谓词（LT/GT/LE/GE）。
static bool has_range_cond(const std::vector<Condition> &conds, const std::string &tab) {
    (void)tab;
    for (const auto &c : conds) {
        // conds 已是当前 Scan 的局部谓词，限定符可能是 alias/binding，不能再
        // 与物理表名比较。
        if (!c.is_rhs_val) continue;
        if (c.op == OP_LT || c.op == OP_GT || c.op == OP_LE || c.op == OP_GE) return true;
    }
    return false;
}

// 题9：SER 显式事务下是否强制 SeqScan（放弃 IndexScan）。仅影响显式 SER 事务
// （is_ser 要求 txn_mode）；autocommit 单语句不走此分支。
//
// 两种情况必须强制 SeqScan：
//   1) join（n_tables>1）：INLJ 内表读尚无 SSI 跟踪钩子，走索引会漏检危险结构；
//   2) 单表【范围查询】：IndexScan 按索引序返回、SeqScan 按堆(插入)序返回，行序不同；
//      题九并发测试逐字比对 SELECT 输出、期望值是旧的强制 SeqScan(插入序)所生成，
//      范围查询改走 IndexScan 会因行序不符而失败。
// 单表等值点查可走 IndexScan；join 内表或范围条件仍强制 SeqScan（SER 谓词读保序）。
namespace {
bool mvcc_force_seqscan(Context *context, const std::string &tab, size_t n_tables,
                        const std::vector<Condition> &conds) {
    if (!(context && context->txn_ && context->txn_mgr_ &&
          context->txn_mgr_->is_ser(context->txn_)))
        return false;
    if (!(context->txn_->get_txn_mode() || context->txn_mgr_->table_is_dirty(tab)))
        return false;
    if (n_tables > 1) return true;                 // join：内表 INLJ 无 SSI 钩子
    return has_range_cond(conds, tab);             // 范围查询保序；等值点查放行索引
}
}  // namespace

/*
    专门对连接进行逻辑优化和物理优化
    逻辑优化主要解决三个问题：条件应该放在scan、join、还是join上方？保留哪些列？哪些逻辑变换不会影响结果

*/
std::shared_ptr<Plan> Planner::make_join_tree_plan(std::shared_ptr<Query> query, Context *context) {
    if (query == nullptr || query->from == nullptr) {
        throw InternalError("SELECT plan requires an analyzed FROM tree");
    }

    using BindingSet = std::set<std::string>;
    struct BuildResult {
        std::shared_ptr<Plan> plan;
        BindingSet bindings;
        double rows = 1.0;
    };

    auto all_bindings = query->from->bindings;
    if (query->select_all) {
        query->cols.clear();
        for (const auto &binding : all_bindings) {
            const auto &tab = sm_manager_->db_.get_table(binding.table_name);
            for (const auto &col : tab.cols) query->cols.push_back({binding.binding_name, col.name});
        }
    }

    auto condition_bindings = [](const Condition &cond) {
        BindingSet result{cond.lhs_col.tab_name};
        if (!cond.is_rhs_val) result.insert(cond.rhs_col.tab_name);
        result.erase("");
        return result;
    };
    auto node_bindings = [](const std::shared_ptr<AnalyzedFrom> &node) {
        BindingSet result;
        for (const auto &binding : node->bindings) result.insert(binding.binding_name);
        return result;
    };
    // jointree 是否包含非 INNER 或 CROSS JOIN
    std::function<bool(const std::shared_ptr<AnalyzedFrom> &)> is_inner_group;
    is_inner_group = [&](const std::shared_ptr<AnalyzedFrom> &node) {
        if (node->is_table) return true;
        if (node->join_type != INNER_JOIN && node->join_type != CROSS_JOIN) return false;
        return is_inner_group(node->left) && is_inner_group(node->right);
    };

    std::map<std::string, std::vector<Condition>> scan_filters; // 对应表的 ScabPlan
    std::map<const AnalyzedFrom *, std::vector<Condition>> join_filters; // 对应 JoinPlan 的 ON 条件

    /*
        ON 条件只在不改变外连接补行语义时下推
        例如 LEFT JOIN 的右表单表 ON 谓词可下推，左表单表 ON 谓词不可下推
        INNER JOIN：左右单表 ON 都可下推
        LEFT JOIN：只有右侧单表 ON 可下推
        RIGHT JOIN：只有左侧单表 ON 可下推
        FULL JOIN：左右都不可下推
        核心递归函数
    */
    std::function<void(const std::shared_ptr<AnalyzedFrom> &)> prepare_on; 
    prepare_on = [&](const std::shared_ptr<AnalyzedFrom> &node) {
        if (node->is_table) return; // 根节点返回
        prepare_on(node->left); // 递归处理左子树
        prepare_on(node->right); // 递归处理右子树
        const auto left_names = node_bindings(node->left);
        const auto right_names = node_bindings(node->right);
        for (const auto &cond : node->on_conds) {
            const auto names = condition_bindings(cond);
            if (names.size() == 1) {
                const auto &name = *names.begin();
                const bool in_left = left_names.count(name) != 0; // 单表条件是在左侧
                const bool in_right = right_names.count(name) != 0; // 单表条件在右侧
                const bool push_left = in_left && is_inner_group(node->left) &&
                    (node->join_type == INNER_JOIN || node->join_type == RIGHT_JOIN);
                const bool push_right = in_right && is_inner_group(node->right) &&
                    (node->join_type == INNER_JOIN || node->join_type == LEFT_JOIN);
                if (push_left || push_right) {
                    scan_filters[name].push_back(cond); // 能下推
                    continue;
                }
            }
            join_filters[node.get()].push_back(cond); // 不能下推
        }
    };
    prepare_on(query->from);

    /*
        WHERE 只能穿过保留该侧输出的连接，一旦路径上该关系处于NULL-supplying 一侧，条件就留在 JOIN 上方的 FilterPlan。
        检查条件能否从 Join Tree 顶部一直下推到目标表。
    */
    std::function<bool(const std::shared_ptr<AnalyzedFrom> &, const std::string &)> where_pushable;
    where_pushable = [&](const std::shared_ptr<AnalyzedFrom> &node, const std::string &binding) {
        if (node->is_table) return node->table.binding_name == binding;
        const auto left_names = node_bindings(node->left);
        if (left_names.count(binding) != 0) {
            if (node->join_type == RIGHT_JOIN || node->join_type == FULL_JOIN) return false;
            return where_pushable(node->left, binding);
        }
        const auto right_names = node_bindings(node->right);
        if (right_names.count(binding) != 0) {
            if (node->join_type == LEFT_JOIN || node->join_type == FULL_JOIN) return false;
            return where_pushable(node->right, binding);
        }
        return false;
    };

    std::vector<Condition> post_join_filters; // 整颗 jointree 上发的 FilterPlan
    for (const auto &cond : query->where_conds) { // 最终分类
        const auto names = condition_bindings(cond);
        if (names.size() == 1 && where_pushable(query->from, *names.begin())) {
            scan_filters[*names.begin()].push_back(cond); // 只涉及到一张表，且路径安全，则可下推
        } else if (names.size() > 1 && is_inner_group(query->from)) {
            // 将笛卡尔积转化为 INNER JOIN
            join_filters[query->from.get()].push_back(cond);
        } else {
            post_join_filters.push_back(cond); // 否则放入 JoinPlan
        }
    }

    /*
        投影下推的必需列集：最终输出以及 WHERE/ON/GROUP/AGG/HAVING/ORDER
        任一后续算子会用到的列都不能被裁掉，包括一下来源：
            query->cols
            query->group_by_cols
            query->aggs
            query->having_conds
            query->orders
            scan_filters
            join_filters
            post_join_filters
    */
    std::map<std::string, std::set<std::string>> required_cols;
    auto require_col = [&](const TabCol &col) {
        if (!col.tab_name.empty() && !col.col_name.empty()) required_cols[col.tab_name].insert(col.col_name);
    };
    auto require_cond = [&](const Condition &cond) {
        require_col(cond.lhs_col);
        if (!cond.is_rhs_val) require_col(cond.rhs_col);
    };
    for (const auto &col : query->cols) require_col(col);
    for (const auto &col : query->group_by_cols) require_col(col);
    for (const auto &agg : query->aggs) if (!agg.is_star) require_col(agg.col);
    for (const auto &cond : query->having_conds) require_cond(cond);
    for (const auto &order : query->orders) require_col(order.first);
    for (const auto &[_, conds] : scan_filters) for (const auto &cond : conds) require_cond(cond);
    for (const auto &[_, conds] : join_filters) for (const auto &cond : conds) require_cond(cond);
    for (const auto &cond : post_join_filters) require_cond(cond);

    const size_t relation_count = all_bindings.size();
    auto make_scan = [&](const TableBinding &binding) -> BuildResult {
        auto conds = scan_filters[binding.binding_name];
        std::vector<std::string> index_cols;
        bool use_index = get_index_cols(binding.table_name, conds, index_cols, binding.binding_name);
        if (use_index && mvcc_force_seqscan(context, binding.table_name, relation_count, conds)) use_index = false;
        std::shared_ptr<Plan> plan = std::make_shared<ScanPlan>(
            use_index ? T_IndexScan : T_SeqScan, sm_manager_, binding.table_name,
            binding.binding_name, conds, index_cols);

        const auto &table = sm_manager_->db_.get_table(binding.table_name);
        std::vector<TabCol> projection;
        for (const auto &col : table.cols) { // 建立 Scan 时遍历真实表的列
            if (required_cols[binding.binding_name].count(col.name) != 0) {
                projection.push_back({binding.binding_name, col.name});
            }
        }
        if (!projection.empty() && projection.size() < table.cols.size()) {
            // 若存在投影，则加入 ProjectionPlan
            plan = std::make_shared<ProjectionPlan>(T_Projection, std::move(plan), std::move(projection));
        }

        double rows = 1000.0; // 估算计划大概会输出多少行
        auto fh = sm_manager_->fhs_.find(binding.table_name);
        if (fh != sm_manager_->fhs_.end() && fh->second != nullptr) {
            const auto &hdr = fh->second->get_file_hdr();
            rows = std::max(1, hdr.num_pages - 1) * std::max(1, hdr.num_records_per_page);
        }
        for (const auto &cond : conds) rows *= cond.op == OP_EQ ? 0.1 : (cond.op == OP_NE ? 0.5 : 0.3);
        return {std::move(plan), {binding.binding_name}, std::max(1.0, rows)};
    };

    //  连接顺序优化，判断当前连接子树能否重排
    //  INNER JOIN / CROSS JOIN 组成的子树可以重排
    //  LEFT / RIGHT / FULL JOIN 会成为重排障碍
    std::function<BuildResult(const std::shared_ptr<AnalyzedFrom> &)> build;
    build = [&](const std::shared_ptr<AnalyzedFrom> &node) -> BuildResult {
        if (node->is_table) return make_scan(node->table);

        if (is_inner_group(node)) { // 纯 INNER / CROSS 子树，则会将其展开
            std::vector<TableBinding> leaves;
            std::vector<Condition> conditions;
            std::function<void(const std::shared_ptr<AnalyzedFrom> &)> flatten;
            flatten = [&](const std::shared_ptr<AnalyzedFrom> &part) {
                if (part->is_table) {
                    leaves.push_back(part->table);
                    return;
                }
                flatten(part->left);
                flatten(part->right);
                const auto &local = join_filters[part.get()];
                conditions.insert(conditions.end(), local.begin(), local.end()); // 收集所有连接条件
            };
            flatten(node);

            std::vector<BuildResult> relations;
            relations.reserve(leaves.size());
            for (const auto &leaf : leaves) relations.push_back(make_scan(leaf)); // 为每个关系建立候选 Scan
            size_t seed = 0; // 选择估算行数最少的关系
            for (size_t i = 1; i < relations.size(); ++i) {
                if (relations[i].rows < relations[seed].rows) seed = i;
            }
            BuildResult current = std::move(relations[seed]);
            relations.erase(relations.begin() + seed);

            std::vector<bool> used(conditions.size(), false);
            while (!relations.empty()) {
                size_t best = 0;
                double best_score = std::numeric_limits<double>::max();
                for (size_t i = 0; i < relations.size(); ++i) {
                    bool connected = false;
                    for (size_t condition_index = 0; condition_index < conditions.size(); ++condition_index) {
                        if (used[condition_index]) continue;
                        const auto names = condition_bindings(conditions[condition_index]);
                        bool has_current = false;
                        bool has_candidate = false;
                        for (const auto &name : names) {
                            has_current = has_current || current.bindings.count(name) != 0;
                            has_candidate = has_candidate || relations[i].bindings.count(name) != 0;
                        }
                        connected = connected || (has_current && has_candidate);
                    }
                    double score = relations[i].rows * (connected ? 0.1 : 10.0);
                    if (connected) {
                        auto right_scan = std::dynamic_pointer_cast<ScanPlan>(relations[i].plan);
                        if (right_scan == nullptr) {
                            if (auto projection = std::dynamic_pointer_cast<ProjectionPlan>(relations[i].plan)) {
                                right_scan = std::dynamic_pointer_cast<ScanPlan>(projection->subplan_);
                            }
                        }
                        if (right_scan != nullptr) {
                            std::vector<Condition> candidate_conds;
                            for (size_t condition_index = 0; condition_index < conditions.size(); ++condition_index) {
                                if (used[condition_index]) continue;
                                const auto names = condition_bindings(conditions[condition_index]);
                                bool touches_left = false;
                                bool touches_right = false;
                                for (const auto &name : names) {
                                    touches_left = touches_left || current.bindings.count(name) != 0;
                                    touches_right = touches_right || relations[i].bindings.count(name) != 0;
                                }
                                if (touches_left && touches_right) candidate_conds.push_back(conditions[condition_index]);
                            }
                            std::vector<std::string> index_cols;
                            if (get_join_index_cols(right_scan->tab_name_, right_scan->binding_name_,
                                                    right_scan->predicates_, candidate_conds, index_cols)) {
                                score *= 0.5;
                            }
                        }
                    }
                    if (score < best_score) {
                        best_score = score;
                        best = i;
                    }
                }

                auto right = std::move(relations[best]);
                relations.erase(relations.begin() + best);
                BindingSet combined = current.bindings;
                combined.insert(right.bindings.begin(), right.bindings.end());
                std::vector<Condition> join_conds;
                for (size_t i = 0; i < conditions.size(); ++i) {
                    if (used[i]) continue;
                    const auto names = condition_bindings(conditions[i]);
                    bool all_available = true;
                    bool touches_left = false;
                    bool touches_right = false;
                    for (const auto &name : names) {
                        all_available = all_available && combined.count(name) != 0;
                        touches_left = touches_left || current.bindings.count(name) != 0;
                        touches_right = touches_right || right.bindings.count(name) != 0;
                    }
                    if (all_available && touches_left && touches_right) {
                        join_conds.push_back(conditions[i]);
                        used[i] = true;
                    }
                }
                const JoinType type = join_conds.empty() ? CROSS_JOIN : INNER_JOIN;
                const double selectivity = join_conds.empty() ? 1.0 : std::pow(0.1, join_conds.size());
                auto plan = make_join_plan(std::move(current.plan), std::move(right.plan),
                                           std::move(join_conds), type, context);
                current = {std::move(plan), std::move(combined),
                           std::max(1.0, current.rows * right.rows * selectivity)};
            }
            return current;
        }

        //  外连接是重排屏障；它的左右子树仍可以各自优化纯内连接组。
        //  可以优化外连接内部的子树
        //  不能跨过外连接边界重排
        auto left = build(node->left);
        auto right = build(node->right);
        BindingSet combined = left.bindings;
        combined.insert(right.bindings.begin(), right.bindings.end());
        auto conds = join_filters[node.get()];
        const double selectivity = conds.empty() ? 1.0 : std::pow(0.1, conds.size());
        const double inner_rows = std::max(1.0, left.rows * right.rows * selectivity);
        double rows = inner_rows;
        if (node->join_type == LEFT_JOIN) rows = std::max(rows, left.rows);
        if (node->join_type == RIGHT_JOIN) rows = std::max(rows, right.rows);
        if (node->join_type == FULL_JOIN) rows = std::max(rows, left.rows + right.rows);
        return {make_join_plan(std::move(left.plan), std::move(right.plan), std::move(conds),
                               node->join_type, context), std::move(combined), rows};
    };

    auto result = build(query->from);
    if (!post_join_filters.empty()) {
        result.plan = std::make_shared<FilterPlan>(T_Filter, std::move(result.plan),
                                                   std::move(post_join_filters));
    }
    return result.plan;
}

std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plan = make_join_tree_plan(query, context);
    // ORDER BY 由 generate_select_plan 统一处理（含聚合别名）
    return plan;
}

/**
 * @brief select plan 生成
 *
 * @param sel_cols select plan 选取的列
 * @param tab_names select plan 目标的表
 * @param conds select plan 选取条件
 */
std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    // 规划会为 SELECT * 展开输出列，使用副本避免修改 Analyzer 结果。
    query = std::make_shared<Query>(*query);

    auto physical_table = [&](const std::string &binding_name) -> std::string {
        if (query->from != nullptr) {
            for (const auto &binding : query->from->bindings) {
                if (binding.binding_name == binding_name) return binding.table_name;
            }
        }
        return binding_name;
    };

    std::shared_ptr<Plan> plannerRoot = physical_optimization(query, context);
    auto sel_cols = query->cols;

    // 聚合+分组
    if (!query->aggs.empty() || !query->group_by_cols.empty()) {
        std::vector<ColMeta> output_cols;
        int offset = 0;
        // GROUP BY 列
        for (auto &gc : query->group_by_cols) {
            auto tab = sm_manager_->db_.get_table(physical_table(gc.tab_name));
            auto col_it = tab.get_col(gc.col_name);
            ColMeta col = *col_it;
            col.tab_name = gc.tab_name;
            col.offset = offset;
            offset += col.len;
            output_cols.push_back(col);
        }
        // 聚合列
        for (auto &agg : query->aggs) {
            ColMeta col;
            col.name = agg.alias.empty() ? agg.to_string() : agg.alias;
            col.tab_name = agg.col.tab_name;
            if (agg.type == ast::AGG_COUNT) {
                col.type = TYPE_INT;
                col.len = sizeof(int);
            } else if (agg.type == ast::AGG_AVG) {
                col.type = TYPE_FLOAT;
                col.len = sizeof(float);
            } else {
                col.type = agg.arg_type;
                if (agg.arg_type == TYPE_INT) {
                    col.len = sizeof(int);
                } else if (agg.arg_type == TYPE_FLOAT) {
                    col.len = sizeof(float);
                } else {
                    auto tab = sm_manager_->db_.get_table(physical_table(agg.col.tab_name));
                    col.len = tab.get_col(agg.col.col_name)->len;
                }
            }
            col.offset = offset;
            offset += col.len;
            output_cols.push_back(col);
        }
        plannerRoot = std::make_shared<AggPlan>(T_Aggregation, std::move(plannerRoot),
                                                query->group_by_cols, query->aggs,
                                                query->having_conds, output_cols);
        // 聚合查询中，Projection 只选择用户 SELECT 的列
        sel_cols.clear();
        for (auto &gc : query->group_by_cols) {
            sel_cols.push_back(gc);
        }
        for (auto &agg : query->aggs) {
            if (agg.in_output) {
                std::string name = agg.alias.empty() ? agg.to_string() : agg.alias;
                sel_cols.push_back({"", name});
            }
        }
    }

    // ORDER BY
    if (!query->orders.empty()) {
        std::vector<std::pair<TabCol, bool>> sort_cols;
        for (auto &order : query->orders) {
            sort_cols.emplace_back(order.first, order.second == ast::OrderBy_DESC);
        }
        plannerRoot = std::make_shared<SortPlan>(T_Sort, std::move(plannerRoot), sort_cols);
    }

    // Projection
    plannerRoot = std::make_shared<ProjectionPlan>(T_Projection, std::move(plannerRoot),
                                                    std::move(sel_cols));

    // LIMIT
    if (query->has_limit) {
        plannerRoot = std::make_shared<LimitPlan>(T_Limit, std::move(plannerRoot), query->limit_count);
    }

    return plannerRoot;
}

// 生成DDL语句和DML语句的查询执行计划
std::shared_ptr<Plan> Planner::do_planner(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plannerRoot;
    if (auto x = std::dynamic_pointer_cast<ast::CreateTable>(query->parse)) {
        // create table;
        std::vector<ColDef> col_defs;
        for (auto &field : x->fields) {
            if (auto sv_col_def = std::dynamic_pointer_cast<ast::ColDef>(field)) {
                ColDef col_def = {.name = sv_col_def->col_name,
                                  .type = interp_sv_type(sv_col_def->type_len->type),
                                  .len = sv_col_def->type_len->len};
                col_defs.push_back(col_def);
            } else {
                throw InternalError("Unexpected field type");
            }
        }
        plannerRoot = std::make_shared<DDLPlan>(T_CreateTable, x->tab_name, std::vector<std::string>(), col_defs);
    } else if (auto x = std::dynamic_pointer_cast<ast::DropTable>(query->parse)) {
        // drop table;
        plannerRoot = std::make_shared<DDLPlan>(T_DropTable, x->tab_name, std::vector<std::string>(), std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::CreateIndex>(query->parse)) {
        // create index;
        plannerRoot = std::make_shared<DDLPlan>(T_CreateIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DropIndex>(query->parse)) {
        // drop index
        plannerRoot = std::make_shared<DDLPlan>(T_DropIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(query->parse)) {
        // insert;
        plannerRoot = std::make_shared<DMLPlan>(T_Insert, std::shared_ptr<Plan>(),  x->tab_name,  
                                                    query->values, std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(query->parse)) {
        // delete;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->where_conds, index_col_names);
        
        if (index_exist && mvcc_force_seqscan(context, x->tab_name, 1, query->where_conds)) index_exist = false;
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors =
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, x->tab_name,
                                           query->where_conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, x->tab_name,
                                           query->where_conds, index_col_names);
        }

        plannerRoot = std::make_shared<DMLPlan>(T_Delete, table_scan_executors, x->tab_name,  
                                                std::vector<Value>(), std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(query->parse)) {
        // update;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->where_conds, index_col_names);

        if (index_exist && mvcc_force_seqscan(context, x->tab_name, 1, query->where_conds)) index_exist = false;
        if (index_exist == false) {  // 该表没有索引
        index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, x->tab_name,
                                           query->where_conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, x->tab_name,
                                           query->where_conds, index_col_names);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_Update, table_scan_executors, x->tab_name,
                                                std::vector<Value>(), query->set_clauses);
    } else if (auto x = std::dynamic_pointer_cast<ast::ExplainStmt>(query->parse)) {
        auto select_plan = generate_select_plan(query->explain_query, context);
        plannerRoot = std::make_shared<ExplainPlan>(select_plan, query->explain_query, query->explain_analyze);
    } else if (auto x = std::dynamic_pointer_cast<ast::UnionStmt>(query->parse)) {
        std::vector<std::shared_ptr<Plan>> subplans;
        for (auto &child : query->union_queries) {
            subplans.push_back(generate_select_plan(child, context));
        }
        std::shared_ptr<Plan> union_plan =
            std::make_shared<UnionPlan>(T_Union, std::move(subplans), query->union_output_cols);
        if (!query->orders.empty()) {
            std::vector<std::pair<TabCol, bool>> sort_cols;
            for (auto &order : query->orders) {
                sort_cols.emplace_back(order.first, order.second == ast::OrderBy_DESC);
            }
            union_plan = std::make_shared<SortPlan>(T_Sort, std::move(union_plan), sort_cols);
        }
        if (query->has_limit) {
            union_plan = std::make_shared<LimitPlan>(T_Limit, std::move(union_plan), query->limit_count);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_select, union_plan, std::string(), std::vector<Value>(),
                                                std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {

        // 生成select语句的查询执行计划
        std::shared_ptr<Plan> projection = generate_select_plan(std::move(query), context);
        plannerRoot = std::make_shared<DMLPlan>(T_select, projection, std::string(), std::vector<Value>(),
                                                std::vector<SetClause>());
    } else {
        throw InternalError("Unexpected AST root");
    }
    return plannerRoot;
}
