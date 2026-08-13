#!/usr/bin/env python3
"""
RMDB P2 自动化测试脚本（决赛 Wire Protocol v3）

比对基于 Wire EXEC_STREAM 返回的类型化结果（格式化为 | cell | 文本），
不再依赖 output.txt（决赛测评不读 output.txt）。

使用:
    python3 run_tests.py
"""

import os
import sys
import time
import socket
import signal
import subprocess
import shutil

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_BASE_DIR = os.path.join(BUILD_DIR, "test_dbs")
PORT = 8765

_LOCAL = os.path.join(PROJECT_ROOT, "tests", "local")
if _LOCAL not in sys.path:
    sys.path.insert(0, _LOCAL)

from wire_client import WireClient, wait_ready  # noqa: E402


def ensure_build():
    if not os.path.isfile(SERVER_BIN):
        print(f"[ERROR] 服务端二进制不存在: {SERVER_BIN}")
        print("请先编译项目: cd build && make -j$(nproc)")
        sys.exit(1)


def is_port_open(port):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.5)
        s.connect(("127.0.0.1", port))
        s.close()
        return True
    except Exception:
        return False


def kill_existing_server():
    """尝试清理可能残留的 rmdb 进程"""
    try:
        subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], capture_output=True, timeout=3)
        time.sleep(0.5)
    except Exception:
        pass
    for _ in range(40):
        if not is_port_open(PORT):
            return
        time.sleep(0.2)


def start_server(db_name):
    os.makedirs(DB_BASE_DIR, exist_ok=True)
    db_dir = os.path.join(DB_BASE_DIR, db_name)
    if os.path.isdir(db_dir):
        shutil.rmtree(db_dir)
    direct_dir = os.path.join(BUILD_DIR, db_name)
    if os.path.isdir(direct_dir):
        shutil.rmtree(direct_dir)

    kill_existing_server()

    last_err = None
    for attempt in range(3):
        proc = subprocess.Popen(
            [SERVER_BIN, db_name],
            cwd=DB_BASE_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            wait_ready(timeout=15.0)
            return proc
        except Exception as e:
            last_err = str(e)
            try:
                proc.kill()
                _stdout, stderr = proc.communicate(timeout=2)
                stderr_tail = stderr.decode(errors="replace")[-800:].strip()
                if stderr_tail:
                    last_err += "; server stderr: " + stderr_tail
            except Exception:
                pass
            kill_existing_server()
            time.sleep(0.5 * (attempt + 1))
    raise RuntimeError("服务器启动失败或 Wire 就绪失败" + (f": {last_err}" if last_err else ""))


def stop_server(proc):
    """停止服务器（Wire 无 exit 帧，直接结束进程）"""
    try:
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass
    kill_existing_server()
    for _ in range(40):
        if not is_port_open(PORT):
            return
        time.sleep(0.2)


def run_testpoint(name, db_name, sqls, expected, unordered_steps=None):
    print(f"\n{'='*60}")
    print(f"测试点: {name}")
    print(f"数据库: {db_name}")
    print(f"{'='*60}")

    try:
        proc = start_server(db_name)
    except Exception as e:
        print(f"[ERROR] 启动服务器失败: {e}")
        return False

    accumulated = []
    try:
        cli = WireClient(timeout=30)
        for sql in sqls:
            print(f"[SQL] {sql}")
            res = cli.exec_stream(sql)
            text = ""
            if res.error or res.aborted:
                # 对齐历史 output.txt：执行失败写 "failure"（健壮性/边界用例依赖）
                accumulated.append("failure")
                text = "failure"
            elif res.columns or res.rows:
                from wire_client import format_table
                text = format_table(res.columns, res.rows)
                for line in text.splitlines():
                    line = line.strip()
                    if line.startswith("|"):
                        accumulated.append(line)
            if text.strip():
                first_line = text.strip().splitlines()[0]
                print(f"[REPLY] {first_line} ...")
        cli.close()
    except Exception as e:
        print(f"[ERROR] 发送 SQL 时出错: {e}")
        stop_server(proc)
        return False

    stop_server(proc)

    actual = "\n".join(accumulated) + ("\n" if accumulated else "")
    print(f"\n----- wire result -----")
    print(actual, end="")
    print("----- end -----")

    actual_lines = [line.strip() for line in actual.strip().splitlines() if line.strip() != ""]
    expected_lines = [line.strip() for line in expected.strip().splitlines() if line.strip() != ""]

    if unordered_steps:
        actual_blocks = []
        block = []
        for line in actual_lines:
            if line.startswith("|") and not block:
                block.append(line)
            elif line.startswith("|"):
                block.append(line)
            else:
                if block:
                    actual_blocks.append(block)
                    block = []
        if block:
            actual_blocks.append(block)

        expected_blocks = []
        block = []
        for line in expected_lines:
            if line.startswith("|") and not block:
                block.append(line)
            elif line.startswith("|"):
                block.append(line)
            else:
                if block:
                    expected_blocks.append(block)
                    block = []
        if block:
            expected_blocks.append(block)

        if len(actual_blocks) == len(expected_blocks):
            for idx in unordered_steps:
                if idx < len(actual_blocks) and idx < len(expected_blocks):
                    if sorted(actual_blocks[idx]) != sorted(expected_blocks[idx]):
                        print(f"[FAIL] {name} (第{idx+1}个结果块集合不匹配)")
                        return False
            print(f"[PASS] {name}")
            return True

    if actual_lines == expected_lines:
        print(f"[PASS] {name}")
        return True
    else:
        print(f"[FAIL] {name}")
        print(f"  期望行数: {len(expected_lines)}, 实际行数: {len(actual_lines)}")
        max_lines = max(len(expected_lines), len(actual_lines))
        for i in range(max_lines):
            e = expected_lines[i] if i < len(expected_lines) else "<missing>"
            a = actual_lines[i] if i < len(actual_lines) else "<missing>"
            if e != a:
                print(f"  第{i+1}行不同:")
                print(f"    期望: {e}")
                print(f"    实际: {a}")
        return False


def main():
    ensure_build()
    kill_existing_server()

    results = []

    # ==================== 测试点1: 尝试建表 ====================
    tp1_sqls = [
        "create table t1(id int,name char(4))",
        "show tables",
        "create table t2(id int)",
        "show tables",
        "drop table t1",
        "show tables",
        "drop table t2",
        "show tables",
    ]
    tp1_expected = """| Tables |
| t1 |
| Tables |
| t1 |
| t2 |
| Tables |
| t2 |
| Tables |"""
    results.append(run_testpoint("尝试建表", "tp1_db", tp1_sqls, tp1_expected))

    # ==================== 测试点2: 单表插入与条件查询 ====================
    tp2_sqls = [
        "create table grade (name char(20),id int,score float)",
        "insert into grade values ('Data Structure', 1, 90.5)",
        "insert into grade values ('Data Structure', 2, 95.0)",
        "insert into grade values ('Calculus', 2, 92.0)",
        "insert into grade values ('Calculus', 1, 88.5)",
        "select * from grade",
        "select score,name,id from grade where score > 90",
        "select id from grade where name = 'Data Structure'",
        "select name from grade where id = 2 and score > 90",
    ]
    tp2_expected = """| name | id | score |
| Data Structure | 1 | 90.500000 |
| Data Structure | 2 | 95.000000 |
| Calculus | 2 | 92.000000 |
| Calculus | 1 | 88.500000 |
| score | name | id |
| 90.500000 | Data Structure | 1 |
| 95.000000 | Data Structure | 2 |
| 92.000000 | Calculus | 2 |
| id |
| 1 |
| 2 |
| name |
| Data Structure |
| Calculus |"""
    results.append(run_testpoint("单表插入与条件查询", "tp2_db", tp2_sqls, tp2_expected))

    # ==================== 测试点3: 单表更新与条件查询 ====================
    tp3_sqls = [
        "create table grade (name char(20),id int,score float)",
        "insert into grade values ('Data Structure', 1, 90.5)",
        "insert into grade values ('Data Structure', 2, 95.0)",
        "insert into grade values ('Calculus', 2, 92.0)",
        "insert into grade values ('Calculus', 1, 88.5)",
        "select * from grade",
        "update grade set score = 90 where name = 'Calculus'",
        "select * from grade",
        "update grade set name = 'Error name' where name > 'A'",
        "select * from grade",
        "update grade set name = 'Error' ,id = -1,score = 0 where name = 'Error name' and score >= 90",
        "select * from grade",
    ]
    tp3_expected = """| name | id | score |
| Data Structure | 1 | 90.500000 |
| Data Structure | 2 | 95.000000 |
| Calculus | 2 | 92.000000 |
| Calculus | 1 | 88.500000 |
| name | id | score |
| Data Structure | 1 | 90.500000 |
| Data Structure | 2 | 95.000000 |
| Calculus | 2 | 90.000000 |
| Calculus | 1 | 90.000000 |
| name | id | score |
| Error name | 1 | 90.500000 |
| Error name | 2 | 95.000000 |
| Error name | 2 | 90.000000 |
| Error name | 1 | 90.000000 |
| name | id | score |
| Error | -1 | 0.000000 |
| Error | -1 | 0.000000 |
| Error | -1 | 0.000000 |
| Error | -1 | 0.000000 |"""
    results.append(run_testpoint("单表更新与条件查询", "tp3_db", tp3_sqls, tp3_expected))

    # ==================== 测试点4: 单表删除与条件查询 ====================
    tp4_sqls = [
        "create table grade (name char(20),id int,score float)",
        "insert into grade values ('Data Structure', 1, 90.5)",
        "select * from grade",
        "delete from grade where score > 90",
        "select * from grade",
    ]
    tp4_expected = """| name | id | score |
| Data Structure | 1 | 90.500000 |
| name | id | score |"""
    results.append(run_testpoint("单表删除与条件查询", "tp4_db", tp4_sqls, tp4_expected))

    # ==================== 测试点5: 连接查询 ====================
    tp5_sqls = [
        "create table t ( id int , t_name char (3))",
        "create table d (d_name char(5),id int)",
        "insert into t values (1,'aaa')",
        "insert into t values (2,'baa')",
        "insert into t values (3,'bba')",
        "insert into d values ('12345',1)",
        "insert into d values ('23456',2)",
        "select * from t, d",
        "select t.id,t_name,d_name from t,d where t.id = d.id",
        "select t.id,t_name,d_name from t join d where t.id = d.id",
    ]
    tp5_expected = """| id | t_name | d_name | id |
| 1 | aaa | 23456 | 2 |
| 1 | aaa | 12345 | 1 |
| 2 | baa | 23456 | 2 |
| 2 | baa | 12345 | 1 |
| 3 | bba | 23456 | 2 |
| 3 | bba | 12345 | 1 |
| id | t_name | d_name |
| 1 | aaa | 12345 |
| 2 | baa | 23456 |
| id | t_name | d_name |
| 1 | aaa | 12345 |
| 2 | baa | 23456 |"""
    # 测试点5的笛卡尔积（第1个select，索引0）不保证顺序，使用集合比较
    results.append(run_testpoint("连接查询", "tp5_db", tp5_sqls, tp5_expected, unordered_steps=[0]))

    # ==================== 测试点6: 单独使用聚合函数 ====================
    tp6_sqls = [
        "create table grade (course char(20),id int,score float)",
        "insert into grade values('DataStructure',1,95)",
        "insert into grade values('DataStructure',2,93.5)",
        "insert into grade values('DataStructure',4,87)",
        "insert into grade values('DataStructure',3,85)",
        "insert into grade values('DB',1,94)",
        "insert into grade values('DB',2,74.5)",
        "insert into grade values('DB',4,83)",
        "insert into grade values('DB',3,87)",
        "select MAX(id) as max_id from grade",
        "select MIN(score) as min_score from grade where course = 'DB'",
        "select AVG(score) as avg_score from grade where course = 'DataStructure'",
        "select COUNT(course) as course_num from grade",
        "select COUNT(*) as row_num from grade",
        "select COUNT(*) as row_num from grade where score < 60",
        "select SUM(score) as sum_score from grade where id = 1",
        "drop table grade",
    ]
    tp6_expected = """| max_id |
| 4 |
| min_score |
| 74.500000 |
| avg_score |
| 90.125000 |
| course_num |
| 8 |
| row_num |
| 8 |
| row_num |
| 0 |
| sum_score |
| 189.000000 |"""
    results.append(run_testpoint("单独使用聚合函数", "tp6_db", tp6_sqls, tp6_expected))

    # ==================== 测试点7: 聚合函数加分组统计 ====================
    tp7_sqls = [
        "create table grade (course char(20),id int,score float)",
        "insert into grade values('DataStructure',1,95)",
        "insert into grade values('DataStructure',2,93.5)",
        "insert into grade values('DataStructure',3,94.5)",
        "insert into grade values('ComputerNetworks',1,99)",
        "insert into grade values('ComputerNetworks',2,88.5)",
        "insert into grade values('ComputerNetworks',3,92.5)",
        "insert into grade values('C++',1,92)",
        "insert into grade values('C++',2,89)",
        "insert into grade values('C++',3,89.5)",
        "select id,MAX(score) as max_score,MIN(score) as min_score,SUM(score) as sum_score from grade group by id",
        "select id,MAX(score) as max_score from grade group by id having COUNT(*) > 3",
        "insert into grade values ('ParallelCompute',1,100)",
        "select id,MAX(score) as max_score from grade group by id having COUNT(*) > 3",
        "select id,MAX(score) as max_score,MIN(score) as min_score from grade group by id having COUNT(*) > 1 and MIN(score) > 88",
        "select course ,COUNT(*) as row_num , COUNT(id) as student_num , MAX(score) as top_score, MIN(score) as lowest_score from grade group by course",
        "select course, id, score from grade order by score desc",
        "drop table grade",
    ]
    tp7_expected = """| id | max_score | min_score | sum_score |
| 1 | 99.000000 | 92.000000 | 286.000000 |
| 2 | 93.500000 | 88.500000 | 271.000000 |
| 3 | 94.500000 | 89.500000 | 276.500000 |
| id | max_score |
| id | max_score |
| 1 | 100.000000 |
| id | max_score | min_score |
| 1 | 100.000000 | 92.000000 |
| 2 | 93.500000 | 88.500000 |
| 3 | 94.500000 | 89.500000 |
| course | row_num | student_num | top_score | lowest_score |
| DataStructure | 3 | 3 | 95.000000 | 93.500000 |
| ComputerNetworks | 3 | 3 | 99.000000 | 88.500000 |
| C++ | 3 | 3 | 92.000000 | 89.000000 |
| ParallelCompute | 1 | 1 | 100.000000 | 100.000000 |
| course | id | score |
| ParallelCompute | 1 | 100.000000 |
| ComputerNetworks | 1 | 99.000000 |
| DataStructure | 1 | 95.000000 |
| DataStructure | 3 | 94.500000 |
| DataStructure | 2 | 93.500000 |
| ComputerNetworks | 3 | 92.500000 |
| C++ | 1 | 92.000000 |
| C++ | 3 | 89.500000 |
| C++ | 2 | 89.000000 |
| ComputerNetworks | 2 | 88.500000 |"""
    # ORDER BY 结果需要无序比较吗？不，ORDER BY 是有序的
    results.append(run_testpoint("聚合函数加分组统计", "tp7_db", tp7_sqls, tp7_expected))

    # ==================== 测试点8: 健壮性测试 ====================
    tp8_sqls = [
        "create table grade (course char(20),id int,score float)",
        "insert into grade values('DataStructure',1,95)",
        "insert into grade values('DataStructure',2,93.5)",
        "insert into grade values('DataStructure',3,94.5)",
        "insert into grade values('ComputerNetworks',1,99)",
        "insert into grade values('ComputerNetworks',2,88.5)",
        "insert into grade values('ComputerNetworks',3,92.5)",
        "select id , score from grade group by course",
        "select id, MAX(score) as max_score from grade where MAX(score) > 90 group by id",
    ]
    tp8_expected = """failure
failure"""
    results.append(run_testpoint("健壮性测试", "tp8_db", tp8_sqls, tp8_expected))

    # ==================== 测试点9: 边界情况测试 ====================
    tp9_sqls = [
        "create table grade (course char(20),id int,score float)",
        # 空表聚合测试
        "select COUNT(*) as cnt from grade",
        "select MAX(score) as mx from grade",
        "select MIN(score) as mn from grade",
        "select SUM(score) as sm from grade",
        "select AVG(score) as av from grade",
        "insert into grade values('DataStructure',1,95)",
        "insert into grade values('DataStructure',2,93.5)",
        "insert into grade values('DB',1,94)",
        "insert into grade values('DB',2,74.5)",
        # GROUP BY 字符串列
        "select course, COUNT(*) as cnt, AVG(score) as avg_score from grade group by course",
        # GROUP BY + ORDER BY 原始列（当前框架 ORDER BY 聚合别名存在已知限制）
        "select course, id, score from grade order by score desc",
        # 多列 GROUP BY
        "select course, id, MAX(score) as max_score from grade group by course, id",
        # HAVING 过滤所有组
        "select course, COUNT(*) as cnt from grade group by course having COUNT(*) > 10",
        # 聚合函数与非聚合列混合（无 GROUP BY）- 应该失败
        "select id, MAX(score) from grade",
        "drop table grade",
    ]
    tp9_expected = """| cnt |
| 0 |
| mx |
| 0.000000 |
| mn |
| 0.000000 |
| sm |
| 0.000000 |
| av |
| 0.000000 |
| course | cnt | avg_score |
| DataStructure | 2 | 94.250000 |
| DB | 2 | 84.250000 |
| course | id | score |
| DataStructure | 1 | 95.000000 |
| DB | 1 | 94.000000 |
| DataStructure | 2 | 93.500000 |
| DB | 2 | 74.500000 |
| course | id | max_score |
| DataStructure | 1 | 95.000000 |
| DataStructure | 2 | 93.500000 |
| DB | 1 | 94.000000 |
| DB | 2 | 74.500000 |
| course | cnt |
failure"""
    results.append(run_testpoint("边界情况测试", "tp9_db", tp9_sqls, tp9_expected))

    # ==================== 测试点10: JOIN + 聚合 ====================
    tp10_sqls = [
        "create table student (id int, name char(20))",
        "create table score (student_id int, course char(20), score float)",
        "insert into student values (1, 'Alice')",
        "insert into student values (2, 'Bob')",
        "insert into student values (3, 'Charlie')",
        "insert into score values (1, 'Math', 95)",
        "insert into score values (1, 'Physics', 92)",
        "insert into score values (2, 'Math', 88)",
        "insert into score values (2, 'Physics', 85)",
        "insert into score values (3, 'Math', 90)",
        "select student.name, COUNT(*) as cnt, AVG(score.score) as avg_score from student, score where student.id = score.student_id group by student.name",
        "select student.name, MAX(score.score) as max_score from student join score where student.id = score.student_id group by student.name having COUNT(*) > 1",
        "drop table student",
        "drop table score",
    ]
    tp10_expected = """| name | cnt | avg_score |
| Alice | 2 | 93.500000 |
| Bob | 2 | 86.500000 |
| Charlie | 1 | 90.000000 |
| name | max_score |
| Alice | 95.000000 |
| Bob | 88.000000 |"""
    results.append(run_testpoint("JOIN聚合测试", "tp10_db", tp10_sqls, tp10_expected))

    # ==================== 测试点11: 大规模聚合综合测试 ====================
    tp11_sqls = [
        "create table student (id int, name char(20), class char(20), age int)",
        "create table score (student_id int, course char(20), score float)",
        "insert into student values (1, 'Alice', 'ClassA', 19)",
        "insert into student values (2, 'Bob', 'ClassB', 20)",
        "insert into student values (3, 'Charlie', 'ClassC', 18)",
        "insert into student values (4, 'David', 'ClassA', 19)",
        "insert into student values (5, 'Eve', 'ClassB', 20)",
        "insert into student values (6, 'Frank', 'ClassC', 18)",
        "insert into student values (7, 'Grace', 'ClassA', 19)",
        "insert into student values (8, 'Henry', 'ClassB', 20)",
        "insert into student values (9, 'Ivy', 'ClassC', 18)",
        "insert into student values (10, 'Jack', 'ClassA', 19)",
        "insert into student values (11, 'Kate', 'ClassB', 20)",
        "insert into student values (12, 'Leo', 'ClassC', 18)",
        "insert into student values (13, 'Mia', 'ClassA', 19)",
        "insert into student values (14, 'Noah', 'ClassB', 20)",
        "insert into student values (15, 'Olivia', 'ClassC', 18)",
        "insert into student values (16, 'Peter', 'ClassA', 19)",
        "insert into student values (17, 'Quinn', 'ClassB', 20)",
        "insert into student values (18, 'Rose', 'ClassC', 18)",
        "insert into student values (19, 'Sam', 'ClassA', 19)",
        "insert into student values (20, 'Tina', 'ClassB', 20)",
        "insert into score values (1, 'Math', 92.0)",
        "insert into score values (1, 'Physics', 71.0)",
        "insert into score values (1, 'Chemistry', 65.0)",
        "insert into score values (1, 'English', 86.0)",
        "insert into score values (2, 'Math', 79.0)",
        "insert into score values (2, 'Physics', 75.0)",
        "insert into score values (2, 'Chemistry', 69.0)",
        "insert into score values (2, 'English', 81.0)",
        "insert into score values (3, 'Math', 93.0)",
        "insert into score values (3, 'Physics', 85.0)",
        "insert into score values (3, 'Chemistry', 67.0)",
        "insert into score values (3, 'English', 96.0)",
        "insert into score values (4, 'Math', 85.0)",
        "insert into score values (4, 'Physics', 69.0)",
        "insert into score values (4, 'Chemistry', 65.0)",
        "insert into score values (4, 'English', 80.0)",
        "insert into score values (5, 'Math', 78.0)",
        "insert into score values (5, 'Physics', 75.0)",
        "insert into score values (5, 'Chemistry', 81.0)",
        "insert into score values (5, 'English', 97.0)",
        "insert into score values (6, 'Math', 72.0)",
        "insert into score values (6, 'Physics', 85.0)",
        "insert into score values (6, 'Chemistry', 71.0)",
        "insert into score values (6, 'English', 100.0)",
        "insert into score values (7, 'Math', 92.0)",
        "insert into score values (7, 'Physics', 90.0)",
        "insert into score values (7, 'Chemistry', 82.0)",
        "insert into score values (7, 'English', 91.0)",
        "insert into score values (8, 'Math', 79.0)",
        "insert into score values (8, 'Physics', 82.0)",
        "insert into score values (8, 'Chemistry', 83.0)",
        "insert into score values (8, 'English', 86.0)",
        "insert into score values (9, 'Math', 72.0)",
        "insert into score values (9, 'Physics', 73.0)",
        "insert into score values (9, 'Chemistry', 87.0)",
        "insert into score values (9, 'English', 91.0)",
        "insert into score values (10, 'Math', 82.0)",
        "insert into score values (10, 'Physics', 76.0)",
        "insert into score values (10, 'Chemistry', 69.0)",
        "insert into score values (10, 'English', 84.0)",
        "insert into score values (11, 'Math', 82.0)",
        "insert into score values (11, 'Physics', 71.0)",
        "insert into score values (11, 'Chemistry', 67.0)",
        "insert into score values (11, 'English', 90.0)",
        "insert into score values (12, 'Math', 75.0)",
        "insert into score values (12, 'Physics', 79.0)",
        "insert into score values (12, 'Chemistry', 76.0)",
        "insert into score values (12, 'English', 97.0)",
        "insert into score values (13, 'Math', 80.0)",
        "insert into score values (13, 'Physics', 69.0)",
        "insert into score values (13, 'Chemistry', 79.0)",
        "insert into score values (13, 'English', 95.0)",
        "insert into score values (14, 'Math', 75.0)",
        "insert into score values (14, 'Physics', 80.0)",
        "insert into score values (14, 'Chemistry', 67.0)",
        "insert into score values (14, 'English', 95.0)",
        "insert into score values (15, 'Math', 81.0)",
        "insert into score values (15, 'Physics', 88.0)",
        "insert into score values (15, 'Chemistry', 84.0)",
        "insert into score values (15, 'English', 89.0)",
        "insert into score values (16, 'Math', 90.0)",
        "insert into score values (16, 'Physics', 74.0)",
        "insert into score values (16, 'Chemistry', 87.0)",
        "insert into score values (16, 'English', 80.0)",
        "insert into score values (17, 'Math', 73.0)",
        "insert into score values (17, 'Physics', 89.0)",
        "insert into score values (17, 'Chemistry', 72.0)",
        "insert into score values (17, 'English', 87.0)",
        "insert into score values (18, 'Math', 74.0)",
        "insert into score values (18, 'Physics', 75.0)",
        "insert into score values (18, 'Chemistry', 68.0)",
        "insert into score values (18, 'English', 90.0)",
        "insert into score values (19, 'Math', 80.0)",
        "insert into score values (19, 'Physics', 82.0)",
        "insert into score values (19, 'Chemistry', 85.0)",
        "insert into score values (19, 'English', 89.0)",
        "insert into score values (20, 'Math', 77.0)",
        "insert into score values (20, 'Physics', 79.0)",
        "insert into score values (20, 'Chemistry', 76.0)",
        "insert into score values (20, 'English', 84.0)",
        "SELECT COUNT(*) AS total_students FROM student",
        "SELECT COUNT(*) AS total_scores FROM score",
        "SELECT AVG(score) AS avg_score FROM score",
        "SELECT MAX(score) AS max_score, MIN(score) AS min_score FROM score",
        "SELECT class, COUNT(*) AS cnt FROM student GROUP BY class",
        "SELECT class, AVG(age) AS avg_age FROM student GROUP BY class HAVING COUNT(*) > 5",
        "SELECT course, COUNT(*) AS cnt, AVG(score) AS avg_score, MAX(score) AS max_score FROM score GROUP BY course",
        "SELECT course, AVG(score) AS avg_score FROM score GROUP BY course HAVING AVG(score) > 75",
        "SELECT student_id, COUNT(*) AS cnt, AVG(score) AS avg_score FROM score GROUP BY student_id HAVING COUNT(*) >= 4",
        "SELECT class, course, AVG(score) AS avg_score FROM student, score WHERE student.id = score.student_id GROUP BY class, course",
        "SELECT course, SUM(score) AS total_score FROM score GROUP BY course",
        "SELECT student_id, MAX(score) AS max_score FROM score WHERE score >= 90 GROUP BY student_id",
        "SELECT class, AVG(score) AS avg_score FROM student, score WHERE student.id = score.student_id AND score >= 80 GROUP BY class",
        "SELECT COUNT(*) AS cnt FROM score WHERE score < 60",
        "SELECT course, COUNT(*) AS cnt FROM score GROUP BY course HAVING COUNT(*) > 100",
        "SELECT class, COUNT(*) AS cnt, AVG(score) AS avg_score, MAX(score) AS max_score, MIN(score) AS min_score FROM student, score WHERE student.id = score.student_id GROUP BY class",
        "SELECT student_id, SUM(score) AS total_score FROM score GROUP BY student_id",
        "SELECT class, COUNT(*) AS cnt FROM student GROUP BY class",
        "UPDATE score SET score = 100 WHERE student_id = 1 AND course = 'Math'",
        "SELECT student_id, course, score FROM score WHERE student_id = 1",
        "SELECT name, score FROM student, score WHERE student.id = score.student_id GROUP BY class",
        "SELECT id, AVG(score) FROM score WHERE score > 90 GROUP BY student_id",
        "drop table student",
        "drop table score",
    ]

    tp11_expected = """| total_students |
| 20 |
| total_scores |
| 80 |
| avg_score |
| 80.824997 |
| max_score | min_score |
| 100.000000 | 65.000000 |
| class | cnt |
| ClassA | 7 |
| ClassB | 7 |
| ClassC | 6 |
| class | avg_age |
| ClassA | 19.000000 |
| ClassB | 20.000000 |
| ClassC | 18.000000 |
| course | cnt | avg_score | max_score |
| Math | 20 | 80.550003 | 93.000000 |
| Physics | 20 | 78.349998 | 90.000000 |
| Chemistry | 20 | 75.000000 | 87.000000 |
| English | 20 | 89.400002 | 100.000000 |
| course | avg_score |
| Math | 80.550003 |
| Physics | 78.349998 |
| English | 89.400002 |
| student_id | cnt | avg_score |
| 1 | 4 | 78.500000 |
| 2 | 4 | 76.000000 |
| 3 | 4 | 85.250000 |
| 4 | 4 | 74.750000 |
| 5 | 4 | 82.750000 |
| 6 | 4 | 82.000000 |
| 7 | 4 | 88.750000 |
| 8 | 4 | 82.500000 |
| 9 | 4 | 80.750000 |
| 10 | 4 | 77.750000 |
| 11 | 4 | 77.500000 |
| 12 | 4 | 81.750000 |
| 13 | 4 | 80.750000 |
| 14 | 4 | 79.250000 |
| 15 | 4 | 85.500000 |
| 16 | 4 | 82.750000 |
| 17 | 4 | 80.250000 |
| 18 | 4 | 76.750000 |
| 19 | 4 | 84.000000 |
| 20 | 4 | 79.000000 |
| class | course | avg_score |
| ClassA | Math | 85.857140 |
| ClassA | Physics | 75.857140 |
| ClassA | Chemistry | 76.000000 |
| ClassA | English | 86.428574 |
| ClassB | Math | 77.571426 |
| ClassB | Physics | 78.714287 |
| ClassB | Chemistry | 73.571426 |
| ClassB | English | 88.571426 |
| ClassC | Math | 77.833336 |
| ClassC | Physics | 80.833336 |
| ClassC | Chemistry | 75.500000 |
| ClassC | English | 93.833336 |
| course | total_score |
| Math | 1611.000000 |
| Physics | 1567.000000 |
| Chemistry | 1500.000000 |
| English | 1788.000000 |
| student_id | max_score |
| 1 | 92.000000 |
| 3 | 96.000000 |
| 5 | 97.000000 |
| 6 | 100.000000 |
| 7 | 92.000000 |
| 9 | 91.000000 |
| 11 | 90.000000 |
| 12 | 97.000000 |
| 13 | 95.000000 |
| 14 | 95.000000 |
| 16 | 90.000000 |
| 18 | 90.000000 |
| class | avg_score |
| ClassA | 85.894737 |
| ClassB | 85.923080 |
| ClassC | 89.692307 |
| cnt |
| 0 |
| course | cnt |
| class | cnt | avg_score | max_score | min_score |
| ClassA | 28 | 81.035713 | 95.000000 | 65.000000 |
| ClassB | 28 | 79.607140 | 97.000000 | 67.000000 |
| ClassC | 24 | 82.000000 | 100.000000 | 67.000000 |
| student_id | total_score |
| 1 | 314.000000 |
| 2 | 304.000000 |
| 3 | 341.000000 |
| 4 | 299.000000 |
| 5 | 331.000000 |
| 6 | 328.000000 |
| 7 | 355.000000 |
| 8 | 330.000000 |
| 9 | 323.000000 |
| 10 | 311.000000 |
| 11 | 310.000000 |
| 12 | 327.000000 |
| 13 | 323.000000 |
| 14 | 317.000000 |
| 15 | 342.000000 |
| 16 | 331.000000 |
| 17 | 321.000000 |
| 18 | 307.000000 |
| 19 | 336.000000 |
| 20 | 316.000000 |
| class | cnt |
| ClassA | 7 |
| ClassB | 7 |
| ClassC | 6 |
| student_id | course | score |
| 1 | Math | 100.000000 |
| 1 | Physics | 71.000000 |
| 1 | Chemistry | 65.000000 |
| 1 | English | 86.000000 |
failure
failure"""
    results.append(run_testpoint("大规模聚合综合测试", "tp11_db", tp11_sqls, tp11_expected))

    # ==================== 测试点12: JOIN 全功能回归 ====================
    # 覆盖 grammar 中的全部连接写法、全部比较运算符，以及外连接的
    # ON/WHERE、NULL 扩展、空输入和下游算子语义。
    tp12_sqls = [
        "create table jl (id int, lv int)",
        "create table jr (id int, rv int)",
        "create table js (id int, sv int)",
        "create table je (id int)",
        "insert into jl values (0, 100)",
        "insert into jl values (1, 10)",
        "insert into jl values (2, 20)",
        "insert into jl values (4, 40)",
        "insert into jr values (0, 900)",
        "insert into jr values (2, 200)",
        "insert into jr values (3, 300)",
        "insert into jr values (4, 400)",
        "insert into jr values (4, 401)",
        "insert into js values (2, 2000)",
        "insert into js values (4, 4000)",
        "insert into js values (5, 5000)",

        # CROSS JOIN 的三种语法。
        "select COUNT(*) as cnt from jl, jr",
        "select COUNT(*) as cnt from jl join jr",
        "select COUNT(*) as cnt from jl cross join jr",

        # JOIN / INNER JOIN、AS/无 AS 别名，以及重复键。
        "select l.id, r.id, r.rv from jl as l join jr as r on l.id = r.id order by l.id, r.rv",
        "select COUNT(*) as cnt from jl l inner join jr r on l.id = r.id",

        # grammar 支持的六种 ON 比较运算符。
        "select COUNT(*) as eq_cnt from jl l inner join jr r on l.id = r.id",
        "select COUNT(*) as ne_cnt from jl l inner join jr r on l.id <> r.id",
        "select COUNT(*) as lt_cnt from jl l inner join jr r on l.id < r.id",
        "select COUNT(*) as gt_cnt from jl l inner join jr r on l.id > r.id",
        "select COUNT(*) as le_cnt from jl l inner join jr r on l.id <= r.id",
        "select COUNT(*) as ge_cnt from jl l inner join jr r on l.id >= r.id",

        # LEFT/RIGHT/FULL 及可选 OUTER 关键字。
        "select l.id, r.id, r.rv from jl l left join jr r on l.id = r.id order by l.id, r.rv",
        "select COUNT(*) as rows, COUNT(r.id) as matched from jl l left outer join jr r on l.id = r.id",
        "select l.id, r.id from jl l right join jr r on l.id = r.id order by r.id, l.id",
        "select COUNT(*) as rows, COUNT(l.id) as matched from jl l right outer join jr r on l.id = r.id",
        "select l.id, r.id from jl l full join jr r on l.id = r.id order by l.id, r.id",
        "select COUNT(*) as rows, COUNT(l.id) as left_rows, COUNT(r.id) as right_rows from jl l full outer join jr r on l.id = r.id",

        # ON 单侧条件不能错误地下推到外连接保留侧；WHERE 必须在 NULL 扩展后执行。
        "select l.id, r.id from jl l left join jr r on l.id = r.id and l.id > 1 order by l.id, r.id",
        "select l.id, r.id from jl l right join jr r on l.id = r.id and r.id > 2 order by r.id, l.id",
        "select l.id, r.id from jl l left join jr r on l.id = r.id and r.rv > 300 order by l.id, r.id",
        "select l.id, r.id from jl l left join jr r on l.id = r.id where r.rv > 300 order by l.id, r.id",
        "select l.id, r.id from jl l full join jr r on l.id = r.id where r.id = 3",

        # 多表、括号、混合逗号连接和自连接。
        "select l.id, r.id, s.id from (jl l inner join jr r on l.id = r.id) inner join js s on r.id = s.id order by l.id, r.rv",
        "select l.id, r.id, s.id from (jl l left join jr r on l.id = r.id) left join js s on r.id = s.id order by l.id, r.rv",
        "select COUNT(*) as cnt from jl l, jr r inner join js s on r.id = s.id",
        "select COUNT(*) as cnt from jl a inner join jl b on a.id < b.id",

        # NULL 掩码必须被聚合、GROUP BY、排序和 LIMIT 正确消费/转发。
        "select COUNT(*) as rows, COUNT(r.id) as matched, SUM(r.rv) as total from jl l left join jr r on l.id = r.id",
        "select r.id, COUNT(*) as cnt from jl l left join jr r on l.id = r.id group by r.id order by r.id",
        "select l.id, r.id from jl l full join jr r on l.id = r.id order by l.id desc, r.id desc",
        "select l.id, r.id from jl l left join jr r on l.id = r.id order by l.id, r.id limit 3",

        # 任一侧为空及双方均为空。
        "select l.id, e.id from jl l left join je e on l.id = e.id order by l.id",
        "select e.id, r.id from je e right join jr r on e.id = r.id order by r.id",
        "select COUNT(*) as cnt from je a full join je b on a.id = b.id",

        # 唯一连接键索引触发 INNER JOIN 的 INLJ 候选路径，结果语义不变。
        # 当前存储层索引采用唯一键语义，因此避免用 jr.id 的重复键构造索引。
        "create index js(id)",
        "select l.id, s.id, s.sv from jl l inner join js s on l.id = s.id order by l.id",

        # 语法/语义错误：歧义列、重复 binding、未知 binding、非法 CROSS ON、外连接缺 ON。
        "select id from jl l inner join jr r on l.id = r.id",
        "select l.id from jl l inner join jr l on l.id = l.id",
        "select x.id from jl l inner join jr r on l.id = r.id",
        "select * from jl cross join jr on jl.id = jr.id",
        "select * from jl left join jr",
    ]
    tp12_expected = """| cnt |
| 20 |
| cnt |
| 20 |
| cnt |
| 20 |
| id | id | rv |
| 0 | 0 | 900 |
| 2 | 2 | 200 |
| 4 | 4 | 400 |
| 4 | 4 | 401 |
| cnt |
| 4 |
| eq_cnt |
| 4 |
| ne_cnt |
| 16 |
| lt_cnt |
| 11 |
| gt_cnt |
| 5 |
| le_cnt |
| 15 |
| ge_cnt |
| 9 |
| id | id | rv |
| 0 | 0 | 900 |
| 1 | NULL | NULL |
| 2 | 2 | 200 |
| 4 | 4 | 400 |
| 4 | 4 | 401 |
| rows | matched |
| 5 | 4 |
| id | id |
| 0 | 0 |
| 2 | 2 |
| NULL | 3 |
| 4 | 4 |
| 4 | 4 |
| rows | matched |
| 5 | 4 |
| id | id |
| 0 | 0 |
| 1 | NULL |
| 2 | 2 |
| 4 | 4 |
| 4 | 4 |
| NULL | 3 |
| rows | left_rows | right_rows |
| 6 | 5 | 5 |
| id | id |
| 0 | NULL |
| 1 | NULL |
| 2 | 2 |
| 4 | 4 |
| 4 | 4 |
| id | id |
| NULL | 0 |
| NULL | 2 |
| NULL | 3 |
| 4 | 4 |
| 4 | 4 |
| id | id |
| 0 | 0 |
| 1 | NULL |
| 2 | NULL |
| 4 | 4 |
| 4 | 4 |
| id | id |
| 0 | 0 |
| 4 | 4 |
| 4 | 4 |
| id | id |
| NULL | 3 |
| id | id | id |
| 2 | 2 | 2 |
| 4 | 4 | 4 |
| 4 | 4 | 4 |
| id | id | id |
| 0 | 0 | NULL |
| 1 | NULL | NULL |
| 2 | 2 | 2 |
| 4 | 4 | 4 |
| 4 | 4 | 4 |
| cnt |
| 12 |
| cnt |
| 6 |
| rows | matched | total |
| 5 | 4 | 1901 |
| id | cnt |
| 0 | 1 |
| 2 | 1 |
| 4 | 2 |
| NULL | 1 |
| id | id |
| NULL | 3 |
| 4 | 4 |
| 4 | 4 |
| 2 | 2 |
| 1 | NULL |
| 0 | 0 |
| id | id |
| 0 | 0 |
| 1 | NULL |
| 2 | 2 |
| id | id |
| 0 | NULL |
| 1 | NULL |
| 2 | NULL |
| 4 | NULL |
| id | id |
| NULL | 0 |
| NULL | 2 |
| NULL | 3 |
| NULL | 4 |
| NULL | 4 |
| cnt |
| 0 |
| id | id | sv |
| 2 | 2 | 2000 |
| 4 | 4 | 4000 |
failure
failure
failure
failure
failure"""
    results.append(run_testpoint("JOIN全功能回归", "tp12_join_db", tp12_sqls, tp12_expected))

    # ==================== 测试点13: 扩展 JOIN 语义 ====================
    # NATURAL 验证公共列合并与 FULL COALESCE；SEMI/ANTI 验证存在性语义；
    # LATERAL 用逐左行 Top-1/聚合直接检查右子计划是否按外层行重新执行。
    tp13_sqls = [
        "create table nx_l (id int, lv int)",
        "create table nx_r (id int, rv int)",
        "create table nx_z (z int)",
        "create table nx_s (id int, sv int)",
        "create table nx_e (id int)",
        "create table nx_c4 (k char(4), v int)",
        "create table nx_c8 (k char(8), w int)",
        "create table nx_m1 (a int, b int, ml int)",
        "create table nx_m2 (b int, a int, mr int)",
        "insert into nx_l values (1,10)",
        "insert into nx_l values (2,20)",
        "insert into nx_l values (4,40)",
        "insert into nx_r values (2,200)",
        "insert into nx_r values (3,300)",
        "insert into nx_r values (4,400)",
        "insert into nx_r values (4,401)",
        "insert into nx_z values (9)",
        "insert into nx_s values (2,2000)",
        "insert into nx_s values (3,3000)",
        "insert into nx_s values (5,5000)",
        "insert into nx_c4 values ('abc',1)",
        "insert into nx_c8 values ('abc',2)",
        "insert into nx_c8 values ('abcdef',3)",
        "insert into nx_m1 values (1,10,110)",
        "insert into nx_m1 values (2,20,220)",
        "insert into nx_m2 values (10,1,1001)",
        "insert into nx_m2 values (20,9,9020)",
        "insert into nx_m2 values (99,2,2099)",

        "select * from nx_l natural join nx_r order by id,rv",
        "select id,l.id,r.id from nx_l l natural full join nx_r r order by id,r.rv",
        "select id,count(*) c from nx_l natural full join nx_r group by id order by id",
        "select id,count(*) c from nx_l natural full join nx_r group by id having id>2 order by id",
        "select count(*) c from nx_l natural join nx_z",
        "select id,l.id,r.id from nx_l l natural right join nx_r r order by id,r.rv",
        "select id,lv,rv,sv from (nx_l natural full join nx_r) natural full join nx_s order by id,rv",
        "select * from nx_c4 natural full join nx_c8 order by k",
        "select * from nx_m1 natural full join nx_m2 order by a,b",
        "select * from nx_c4 semi join nx_c8 on nx_c4.k=nx_c8.k",

        "select * from nx_l semi join nx_r on nx_l.id=nx_r.id order by nx_l.id",
        "select * from nx_l anti join nx_r on nx_l.id=nx_r.id order by nx_l.id",
        "select * from nx_l right semi join nx_r on nx_l.id=nx_r.id order by nx_r.id,nx_r.rv",
        "select * from nx_l right anti join nx_r on nx_l.id=nx_r.id order by nx_r.id",
        "select * from nx_e e right semi join nx_r r on e.id=r.id order by r.id,r.rv",
        "select * from nx_e e right anti join nx_r r on e.id=r.id order by r.id,r.rv",
        "select nx_r.id from nx_l semi join nx_r on nx_l.id=nx_r.id",
        "select * from nx_l semi join nx_e on nx_l.id=nx_e.id",
        "select * from nx_l anti join nx_e on nx_l.id=nx_e.id order by nx_l.id",
        "select count(*) c from (nx_l l full join nx_r r on l.id=r.id) "
        "semi join nx_s s on l.id=s.id",
        "select count(*) c from (nx_l l full join nx_r r on l.id=r.id) "
        "anti join nx_s s on l.id=s.id",

        "select x.rv from nx_l l cross join lateral "
        "(select r.rv from nx_r r where r.id=l.id order by r.rv desc limit 1) x order by x.rv",
        "select l.id,x.rv from nx_l l left join lateral "
        "(select r.rv from nx_r r where r.id=l.id) x on true order by l.id,x.rv",
        "select l.id,x.c from nx_l l cross join lateral "
        "(select count(*) c from nx_r r where r.id=l.id) x order by l.id",
        "select l.id,x.rv from nx_l l left join lateral "
        "(select r.rv from nx_r r where l.id<>0.5) x on true where x.rv=200 order by l.id",
        "select l.id,x.rv from nx_l l inner join lateral "
        "(select r.rv from nx_r r where r.id=l.id) x on x.rv>400 order by l.id,x.rv",
        "select l.id,x.rv from nx_l l left join lateral "
        "(select r.rv from nx_r r where r.id=l.id) x on x.rv>999 order by l.id",
        "select * from nx_l l right join lateral "
        "(select * from nx_r r where r.id=l.id) x on l.id=x.id",
        "select * from nx_l l semi join nx_z z on true order by l.id",
        "select * from nx_l l anti join nx_z z on true",
        "select l.id,z.z from (nx_l l semi join nx_r r on l.id=r.id) left join nx_z z on true order by l.id",
        "select count(*) c from nx_l l semi join nx_r r on l.id=r.id having r.id>0",
        "select count(*) c from nx_l l semi join nx_r r on l.id=r.id having count(r.id)>0",
        "select count(l.id) c from nx_l l semi join nx_r r on l.id=r.id having count(l.id)>0",
        "select x.v from nx_l l cross join lateral "
        "(select r.id v,r.rv v from nx_r r where r.id=l.id) x",
    ]
    tp13_expected = """| id | lv | rv |
| 2 | 20 | 200 |
| 4 | 40 | 400 |
| 4 | 40 | 401 |
| id | id | id |
| 1 | 1 | NULL |
| 2 | 2 | 2 |
| 3 | NULL | 3 |
| 4 | 4 | 4 |
| 4 | 4 | 4 |
| id | c |
| 1 | 1 |
| 2 | 1 |
| 3 | 1 |
| 4 | 2 |
| id | c |
| 3 | 1 |
| 4 | 2 |
| c |
| 3 |
| id | id | id |
| 2 | 2 | 2 |
| 3 | NULL | 3 |
| 4 | 4 | 4 |
| 4 | 4 | 4 |
| id | lv | rv | sv |
| 1 | 10 | NULL | NULL |
| 2 | 20 | 200 | 2000 |
| 3 | NULL | 300 | 3000 |
| 4 | 40 | 400 | NULL |
| 4 | 40 | 401 | NULL |
| 5 | NULL | NULL | 5000 |
| k | v | w |
| abc | 1 | 2 |
| abcdef | NULL | 3 |
| a | b | ml | mr |
| 1 | 10 | 110 | 1001 |
| 2 | 20 | 220 | NULL |
| 2 | 99 | NULL | 2099 |
| 9 | 20 | NULL | 9020 |
| k | v |
| abc | 1 |
| id | lv |
| 2 | 20 |
| 4 | 40 |
| id | lv |
| 1 | 10 |
| id | rv |
| 2 | 200 |
| 4 | 400 |
| 4 | 401 |
| id | rv |
| 3 | 300 |
| id | rv |
| id | rv |
| 2 | 200 |
| 3 | 300 |
| 4 | 400 |
| 4 | 401 |
failure
| id | lv |
| id | lv |
| 1 | 10 |
| 2 | 20 |
| 4 | 40 |
| c |
| 1 |
| c |
| 4 |
| rv |
| 200 |
| 401 |
| id | rv |
| 1 | NULL |
| 2 | 200 |
| 4 | 400 |
| 4 | 401 |
| id | c |
| 1 | 0 |
| 2 | 1 |
| 4 | 2 |
| id | rv |
| 1 | 200 |
| 2 | 200 |
| 4 | 200 |
| id | rv |
| 4 | 401 |
| id | rv |
| 1 | NULL |
| 2 | NULL |
| 4 | NULL |
failure
| id | lv |
| 1 | 10 |
| 2 | 20 |
| 4 | 40 |
| id | lv |
| id | z |
| 2 | 9 |
| 4 | 9 |
failure
failure
| c |
| 2 |
failure"""
    results.append(run_testpoint("NATURAL/SEMI/ANTI/LATERAL JOIN回归",
                                 "tp13_extended_join_db", tp13_sqls, tp13_expected))

    # 汇总
    print(f"\n{'='*60}")
    print("测试汇总")
    print(f"{'='*60}")
    names = [
        "尝试建表",
        "单表插入与条件查询",
        "单表更新与条件查询",
        "单表删除与条件查询",
        "连接查询",
        "单独使用聚合函数",
        "聚合函数加分组统计",
        "健壮性测试",
        "边界情况测试",
        "JOIN聚合测试",
        "大规模聚合综合测试",
        "JOIN全功能回归",
        "NATURAL/SEMI/ANTI/LATERAL JOIN回归",
    ]
    for name, ok in zip(names, results):
        status = "PASS" if ok else "FAIL"
        print(f"  [{status}] {name}")

    passed = sum(results)
    total = len(results)
    print(f"\n总计: {passed}/{total} 通过")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
