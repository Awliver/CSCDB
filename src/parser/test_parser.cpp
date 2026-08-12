/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */
#undef NDEBUG

#include <cassert>

#include "parser.h"

int main() {
    std::vector<std::string> sqls = {
        "show tables;",
        "desc tb;",
        "create table tb (a int, b float, c char(4));",
        "drop table tb;",
        "create index tb(a);",
        "create index tb(a, b, c);",
        "drop index tb(a, b, c);",
        "drop index tb(b);",
        "insert into tb values (1, 3.14, 'pi');",
        "delete from tb where a = 1;",
        "delete from tb where not (a = 1 or b = 2);",
        "update tb set a = 1, b = 2.2, c = 'xyz' where x = 2 and y < 1.1 and z > 'abc';",
        "update tb set a = 1 where x = 2 or not y = 3;",
        "select * from tb;",
        "select * from tb where x <> 2 and y >= 3. and z <= '123' and b < tb.a;",
        "select x.a, y.b from x, y where x.a = y.b and c = d;",
        "select x.a, y.b from x join y where x.a = y.b and c = d;",
        "select * from a left join b on a.id = b.id where b.enabled = 1;",
        "select * from a left join b on a.id = b.id or not (a.k = b.k and b.enabled = 1);",
        "select * from a right outer join b on a.id = b.id;",
        "select * from a full join b on a.id = b.id;",
        "select * from a cross join b;",
        "select * from a natural join b;",
        "select * from a natural inner join b;",
        "select * from a natural left outer join b;",
        "select * from a natural right join b;",
        "select * from a natural full outer join b;",
        "select * from a semi join b on a.id = b.id;",
        "select * from a left semi join b on a.id = b.id;",
        "select * from a right semi join b on a.id = b.id;",
        "select * from a anti join b on a.id = b.id;",
        "select * from a left anti join b on a.id = b.id;",
        "select * from a right anti join b on a.id = b.id;",
        "select * from a join lateral (select * from b) x on a.id = x.id;",
        "select * from a inner join lateral (select * from b) as x on a.id = x.id;",
        "select * from a cross join lateral (select * from b) x;",
        "select * from a left outer join lateral (select * from b) x on a.id = x.id;",
        "select * from a left join lateral (select * from b) x on true;",
        "select * from a right join lateral (select * from b) x on a.id = x.id;",
        "select * from a full outer join lateral (select * from b) x on a.id = x.id;",
        "select * from a natural join lateral (select * from b) x;",
        "exit;",
        "help;",
        "",
    };
    for (auto &sql : sqls) {
        std::cout << sql << std::endl;
        YY_BUFFER_STATE buf = yy_scan_string(sql.c_str());
        assert(yyparse() == 0);
        if (ast::parse_tree != nullptr) {
            ast::TreePrinter::print(ast::parse_tree);
            yy_delete_buffer(buf);
            std::cout << std::endl;
        } else {
            std::cout << "exit/EOF" << std::endl;
        }
    }

    auto parse_select = [](const std::string &sql) {
        ast::parse_tree.reset();
        YY_BUFFER_STATE buf = yy_scan_string(sql.c_str());
        assert(yyparse() == 0);
        auto select = std::dynamic_pointer_cast<ast::SelectStmt>(ast::parse_tree);
        assert(select != nullptr);
        yy_delete_buffer(buf);
        return select;
    };

    auto as_logical = [](const std::shared_ptr<ast::BoolExpr> &expr,
                         ast::LogicalOp op) {
        auto logical = std::dynamic_pointer_cast<ast::LogicalExpr>(expr);
        assert(logical != nullptr && logical->op == op);
        return logical;
    };

    auto as_atom = [](const std::shared_ptr<ast::BoolExpr> &expr) {
        auto atom = std::dynamic_pointer_cast<ast::BinaryExpr>(expr);
        assert(atom != nullptr);
        return atom;
    };

    {
        auto select = parse_select(
            "select * from a left join b on a.id = b.id where b.enabled = 1;");
        auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
        assert(join != nullptr && join->type == LEFT_JOIN);
        assert(!join->natural && !join->lateral);
        assert(as_atom(join->on_expr) != nullptr);
        assert(as_atom(select->where_expr) != nullptr);
    }
    {
        YY_BUFFER_STATE buf = yy_scan_string(
            "select k, count(distinct v), avg(v) from t group by k "
            "having count(distinct v) > 1;");
        assert(yyparse() == 0);
        auto select = std::dynamic_pointer_cast<ast::SelectStmt>(ast::parse_tree);
        assert(select != nullptr);
        assert(select->aggs.size() == 2);
        assert(select->aggs[0]->agg_type == ast::AGG_COUNT);
        assert(select->aggs[0]->distinct);
        assert(select->aggs[0]->to_string() == "count(distinct v)");
        auto having = as_atom(select->having_expr);
        assert(having->lhs_agg != nullptr);
        assert(having->lhs_agg->distinct);
        yy_delete_buffer(buf);
    }
    {
        // 未被词法器保留的聚合名称只有出现在中央注册表中才会被接受。
        // 这是“两处修改即可扩展”约定在解析器侧的保证。
        YY_BUFFER_STATE buf = yy_scan_string("select not_registered(v) from t;");
        assert(yyparse() != 0);
        yy_delete_buffer(buf);
    }

    {
        auto select = parse_select("select * from a natural full outer join b;");
        auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
        assert(join != nullptr && join->type == FULL_JOIN);
        assert(join->natural && !join->lateral);
        assert(join->on_expr == nullptr);
    }

    {
        const std::vector<std::pair<std::string, JoinType>> cases = {
            {"select * from a semi join b on a.id = b.id;", LEFT_SEMI_JOIN},
            {"select * from a left semi join b on a.id = b.id;", LEFT_SEMI_JOIN},
            {"select * from a right semi join b on a.id = b.id;", RIGHT_SEMI_JOIN},
            {"select * from a anti join b on a.id = b.id;", LEFT_ANTI_JOIN},
            {"select * from a left anti join b on a.id = b.id;", LEFT_ANTI_JOIN},
            {"select * from a right anti join b on a.id = b.id;", RIGHT_ANTI_JOIN},
        };
        for (const auto &[sql, type] : cases) {
            auto select = parse_select(sql);
            auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
            assert(join != nullptr && join->type == type);
            assert(!join->natural && !join->lateral);
            assert(as_atom(join->on_expr) != nullptr);
        }
    }

    {
        auto select = parse_select(
            "select * from a left outer join lateral "
            "(select b.id from b where b.id > 0) as x on a.id = x.id;");
        auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
        assert(join != nullptr && join->type == LEFT_JOIN);
        assert(!join->natural && join->lateral);
        assert(as_atom(join->on_expr) != nullptr);
        auto lateral = std::dynamic_pointer_cast<ast::LateralRef>(join->right);
        assert(lateral != nullptr && lateral->alias == "x");
        auto lateral_select = std::dynamic_pointer_cast<ast::SelectStmt>(lateral->subquery);
        assert(lateral_select != nullptr);
        assert(as_atom(lateral_select->where_expr) != nullptr);
    }

    {
        auto select = parse_select(
            "select * from a left join lateral (select * from b) x on true;");
        auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
        assert(join != nullptr && join->type == LEFT_JOIN && join->lateral);
        assert(join->on_true && join->on_expr == nullptr);
    }

    {
        auto select = parse_select(
            "select * from a natural join lateral (select * from b) x;");
        auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
        assert(join != nullptr && join->type == INNER_JOIN);
        assert(join->natural && join->lateral && join->on_expr == nullptr);
    }

    {
        // SQL precedence is NOT > AND > OR.
        auto select = parse_select(
            "select * from t where a = 1 or b = 2 and not c = 3;");
        auto root = as_logical(select->where_expr, ast::LogicalOp::OR);
        assert(as_atom(root->left)->lhs->col_name == "a");
        auto rhs = as_logical(root->right, ast::LogicalOp::AND);
        assert(as_atom(rhs->left)->lhs->col_name == "b");
        auto negated = std::dynamic_pointer_cast<ast::NotExpr>(rhs->right);
        assert(negated != nullptr);
        assert(as_atom(negated->child)->lhs->col_name == "c");
    }

    {
        // Parentheses change the tree shape but do not survive as a runtime node.
        auto select = parse_select(
            "select * from t where (a = 1 or b = 2) and c = 3;");
        auto root = as_logical(select->where_expr, ast::LogicalOp::AND);
        auto grouped = as_logical(root->left, ast::LogicalOp::OR);
        assert(as_atom(grouped->left)->lhs->col_name == "a");
        assert(as_atom(grouped->right)->lhs->col_name == "b");
        assert(as_atom(root->right)->lhs->col_name == "c");
    }

    {
        auto select = parse_select(
            "select * from a join b on a.id = b.id or "
            "not (a.k = b.k and b.enabled = 1);");
        auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
        assert(join != nullptr);
        auto root = as_logical(join->on_expr, ast::LogicalOp::OR);
        assert(as_atom(root->left)->lhs->col_name == "id");
        auto negated = std::dynamic_pointer_cast<ast::NotExpr>(root->right);
        assert(negated != nullptr);
        assert(as_logical(negated->child, ast::LogicalOp::AND) != nullptr);
    }

    {
        auto select = parse_select(
            "select k, count(*) as n from t group by k "
            "having not count(*) = 0 or k = 1 and count(*) > 2;");
        auto root = as_logical(select->having_expr, ast::LogicalOp::OR);
        auto negated = std::dynamic_pointer_cast<ast::NotExpr>(root->left);
        assert(negated != nullptr);
        assert(as_atom(negated->child)->lhs_agg != nullptr);
        auto rhs = as_logical(root->right, ast::LogicalOp::AND);
        assert(as_atom(rhs->left)->lhs->col_name == "k");
        assert(as_atom(rhs->right)->lhs_agg != nullptr);
    }

    {
        ast::parse_tree.reset();
        YY_BUFFER_STATE buf = yy_scan_string(
            "update t set v = 1 where not (a = 1 or b = 2) and c = 3;");
        assert(yyparse() == 0);
        auto update = std::dynamic_pointer_cast<ast::UpdateStmt>(ast::parse_tree);
        assert(update != nullptr);
        auto root = as_logical(update->where_expr, ast::LogicalOp::AND);
        assert(std::dynamic_pointer_cast<ast::NotExpr>(root->left) != nullptr);
        assert(as_atom(root->right)->lhs->col_name == "c");
        yy_delete_buffer(buf);
    }

    {
        ast::parse_tree.reset();
        YY_BUFFER_STATE buf = yy_scan_string(
            "delete from t where a = 1 or not b = 2;");
        assert(yyparse() == 0);
        auto remove = std::dynamic_pointer_cast<ast::DeleteStmt>(ast::parse_tree);
        assert(remove != nullptr);
        auto root = as_logical(remove->where_expr, ast::LogicalOp::OR);
        assert(as_atom(root->left)->lhs->col_name == "a");
        assert(std::dynamic_pointer_cast<ast::NotExpr>(root->right) != nullptr);
        yy_delete_buffer(buf);
    }

    {
        auto select = parse_select("select * from t;");
        assert(select->where_expr == nullptr && select->having_expr == nullptr);
    }

    {
        auto select = parse_select("select * from t where name not like 'A%';");
        auto negated = std::dynamic_pointer_cast<ast::NotExpr>(select->where_expr);
        assert(negated != nullptr);
        assert(as_atom(negated->child)->op == ast::SV_OP_LIKE);
    }

    {
        auto select = parse_select(
            "select * from t where score between 10 and 20 and id in (1,3,5);");
        auto root = as_logical(select->where_expr, ast::LogicalOp::AND);
        auto between = as_logical(root->left, ast::LogicalOp::AND);
        assert(as_atom(between->left)->op == ast::SV_OP_GE);
        assert(as_atom(between->right)->op == ast::SV_OP_LE);
        auto in_list = as_logical(root->right, ast::LogicalOp::OR);
        assert(as_logical(in_list->left, ast::LogicalOp::OR) != nullptr);
        assert(as_atom(in_list->right)->op == ast::SV_OP_EQ);
    }

    {
        auto select = parse_select(
            "select * from t where exists (select id from u where u.id=t.id) "
            "or id in (select id from v);");
        auto root = as_logical(select->where_expr, ast::LogicalOp::OR);
        auto exists = std::dynamic_pointer_cast<ast::SubqueryPredicate>(root->left);
        auto in_subquery = std::dynamic_pointer_cast<ast::SubqueryPredicate>(root->right);
        assert(exists != nullptr &&
               exists->type == ast::SubqueryPredicateType::EXISTS &&
               exists->lhs == nullptr);
        assert(in_subquery != nullptr &&
               in_subquery->type == ast::SubqueryPredicateType::IN &&
               in_subquery->lhs->col_name == "id");
    }

    {
        ast::parse_tree.reset();
        YY_BUFFER_STATE buf = yy_scan_string(
            "select v from a union all select v from b union distinct "
            "select v from c order by 1 desc limit 2;");
        assert(yyparse() == 0);
        auto root = std::dynamic_pointer_cast<ast::UnionStmt>(ast::parse_tree);
        assert(root != nullptr && !root->all);
        auto left = std::dynamic_pointer_cast<ast::UnionStmt>(root->left);
        assert(left != nullptr && left->all);
        assert(root->orders.size() == 1 && root->orders[0]->is_ordinal &&
               root->orders[0]->ordinal == 1);
        assert(root->orders[0]->orderby_dir == ast::OrderBy_DESC);
        assert(root->has_limit && root->limit_count == 2);
        yy_delete_buffer(buf);
    }

    {
        ast::parse_tree.reset();
        YY_BUFFER_STATE buf = yy_scan_string(
            "select v from a union all (select v from b union select v from c);");
        assert(yyparse() == 0);
        auto root = std::dynamic_pointer_cast<ast::UnionStmt>(ast::parse_tree);
        assert(root != nullptr && root->all);
        auto right_group = std::dynamic_pointer_cast<ast::QueryGroup>(root->right);
        assert(right_group != nullptr);
        auto right_union = std::dynamic_pointer_cast<ast::UnionStmt>(right_group->child);
        assert(right_union != nullptr && !right_union->all);
        yy_delete_buffer(buf);
    }

    {
        auto select = parse_select(
            "select d.v from (select v from a union all select v from b) d "
            "where d.v > 0;");
        auto derived = std::dynamic_pointer_cast<ast::DerivedTableRef>(select->from);
        assert(derived != nullptr && derived->alias == "d");
        assert(std::dynamic_pointer_cast<ast::UnionStmt>(derived->subquery) != nullptr);
    }

    {
        auto select = parse_select(
            "select a.v,x.v from a cross join lateral "
            "(select v from b where b.v=a.v union all "
            "select v from c where c.v=a.v) x;");
        auto join = std::dynamic_pointer_cast<ast::JoinExpr>(select->from);
        assert(join != nullptr && join->lateral && join->type == CROSS_JOIN);
        auto lateral = std::dynamic_pointer_cast<ast::LateralRef>(join->right);
        assert(lateral != nullptr && lateral->alias == "x");
        assert(std::dynamic_pointer_cast<ast::UnionStmt>(lateral->subquery) != nullptr);
    }

    auto assert_parse_error = [](const std::string &sql) {
        ast::parse_tree.reset();
        YY_BUFFER_STATE buf = yy_scan_string(sql.c_str());
        assert(yyparse() != 0);
        yy_delete_buffer(buf);
    };
    assert_parse_error("select * from a natural cross join b;");
    assert_parse_error("select * from a natural join b on a.id = b.id;");
    assert_parse_error("select * from a semi join b;");
    assert_parse_error("select * from a cross join lateral (select * from b);");
    assert_parse_error("select * from a left join lateral (select * from b) x on false;");
    assert_parse_error("select v from a union all distinct select v from b;");
    assert_parse_error("select v from a order by v union all select v from b;");

    ast::parse_tree.reset();
    return 0;
}
