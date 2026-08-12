/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "execution/execution_defs.h"
#include "execution/execution_manager.h"
#include "record/rm.h"
#include "system/sm.h"
#include "common/context.h"
#include "plan.h"
#include "parser/parser.h"
#include "common/common.h"
#include "analyze/analyze.h"

class Planner {
   private:
    SmManager *sm_manager_;

    bool enable_nestedloop_join = true;
    bool enable_sortmerge_join = false;

   public:
    Planner(SmManager *sm_manager) : sm_manager_(sm_manager) {}


    std::shared_ptr<Plan> do_planner(std::shared_ptr<Query> query, Context *context);

    void set_enable_nestedloop_join(bool set_val) { enable_nestedloop_join = set_val; }
    
    void set_enable_sortmerge_join(bool set_val) { enable_sortmerge_join = set_val; }
    
   private:
    std::shared_ptr<Plan> physical_optimization(std::shared_ptr<Query> query, Context *context);

    std::shared_ptr<Plan> make_join_tree_plan(std::shared_ptr<Query> query, Context *context);
    
    std::shared_ptr<Plan> generate_select_plan(std::shared_ptr<Query> query, Context *context);


    // access_conditions 只包含从完整布尔树中安全提取的正向合取原子。
    bool get_index_cols(const std::string &tab_name,
                        const std::vector<Condition> &access_conditions,
                        std::vector<std::string>& index_col_names,
                        const std::string &binding_name = "");
    bool get_join_index_cols(const std::string &right_table, const std::string &right_binding,
                             const std::vector<Condition> &scan_conds,
                             const std::vector<Condition> &join_conds,
                             std::vector<std::string> &index_col_names);
    std::shared_ptr<Plan> make_join_plan(std::shared_ptr<Plan> left, std::shared_ptr<Plan> right,
                                         ConditionExprPtr join_predicate,
                                         JoinType join_type = INNER_JOIN,
                                         Context *context = nullptr,
                                         bool natural = false, bool lateral = false,
                                         std::vector<CoalescedJoinColumn> coalesced_cols = {});

    ColType interp_sv_type(ast::SvType sv_type) {
        std::map<ast::SvType, ColType> m = {
            {ast::SV_TYPE_INT, TYPE_INT}, {ast::SV_TYPE_FLOAT, TYPE_FLOAT}, {ast::SV_TYPE_STRING, TYPE_STRING}};
        return m.at(sv_type);
    }
};
