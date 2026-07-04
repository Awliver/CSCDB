#!/usr/bin/env python3
"""
RMDB P5 聚合函数回归测试集
覆盖: 基本聚合、GROUP BY、HAVING、ORDER BY、LIMIT、JOIN、表别名、无AS别名

使用:
    python3 test/edge_cases/test_regression_p5.py

返回码: 0 = 全部通过, 1 = 有失败
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "build")
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
PORT = 8765

def wait_for_port(port, timeout=5):
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            s.connect(("127.0.0.1", port))
            s.close()
            return True
        except Exception:
            time.sleep(0.2)
    return False

def send_sql(sock, sql):
    sql = sql.strip()
    if not sql.endswith(';'):
        sql += ';'
    payload = sql.encode('utf-8') + b'\x00'
    sock.sendall(payload)
    data = b""
    while True:
        try:
            sock.settimeout(5.0)
            chunk = sock.recv(8192)
            if not chunk:
                break
            data += chunk
            if b'\x00' in data:
                break
        except socket.timeout:
            break
    parts = data.split(b'\x00')
    return parts[0].decode('utf-8', errors='replace') if parts else ""

def count_data_lines(reply):
    lines = reply.strip().splitlines()
    data_lines = [l for l in lines if l.strip().startswith('|')]
    return max(0, len(data_lines) - 1) if data_lines else 0

def run_tests():
    DB_NAME = "test_regression_db"
    DB_DIR = os.path.join(BUILD_DIR, "test_dbs", DB_NAME)

    if os.path.isdir(DB_DIR):
        shutil.rmtree(DB_DIR)

    os.makedirs(os.path.join(BUILD_DIR, "test_dbs"), exist_ok=True)
    db_path = os.path.join(BUILD_DIR, "test_dbs", DB_NAME)
    proc = subprocess.Popen(
        [SERVER_BIN, db_path],
        cwd=BUILD_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if not wait_for_port(PORT, timeout=6):
        out, err = proc.communicate(timeout=2)
        print(f"[FATAL] Server failed: {err.decode()}")
        return False

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect(("127.0.0.1", PORT))

    # ========== 建表插数 ==========
    setup = [
        "CREATE TABLE grade (course char(20), id int, score float);",
        "CREATE TABLE empty_grade (course char(20), id int, score float);",
        "CREATE TABLE student (id int, name char(10));",
        "CREATE TABLE score (student_id int, course char(10), score float);",
        "INSERT INTO grade VALUES ('DS', 1, 95.0);",
        "INSERT INTO grade VALUES ('DS', 2, 93.5);",
        "INSERT INTO grade VALUES ('CN', 1, 99.0);",
        "INSERT INTO grade VALUES ('CN', 2, 88.5);",
        "INSERT INTO student VALUES (1, 'Alice');",
        "INSERT INTO student VALUES (2, 'Bob');",
        "INSERT INTO score VALUES (1, 'Math', 95.0);",
        "INSERT INTO score VALUES (1, 'Physics', 92.0);",
        "INSERT INTO score VALUES (2, 'Math', 88.0);",
    ]
    for sql in setup:
        send_sql(sock, sql)

    # ========== 测试用例定义 ==========
    # (ID, 描述, SQL, 期望数据行数, 是否已知失败)
    tests = []

    # --- 基础聚合 ---
    tests.extend([
        ("A01", "COUNT(*)", "SELECT COUNT(*) AS cnt FROM grade", 1, False),
        ("A02", "COUNT(列)", "SELECT COUNT(course) AS cnt FROM grade", 1, False),
        ("A03", "MAX", "SELECT MAX(score) AS mx FROM grade", 1, False),
        ("A04", "MIN", "SELECT MIN(score) AS mn FROM grade", 1, False),
        ("A05", "SUM", "SELECT SUM(score) AS sm FROM grade", 1, False),
        ("A06", "AVG", "SELECT AVG(score) AS av FROM grade", 1, False),
        ("A07", "多聚合", "SELECT COUNT(*) AS cnt, MAX(score) AS mx, MIN(score) AS mn, SUM(score) AS sm, AVG(score) AS av FROM grade", 1, False),
    ])

    # --- 空表默认值 ---
    tests.extend([
        ("B01", "空表COUNT", "SELECT COUNT(*) AS cnt FROM empty_grade", 1, False),
        ("B02", "空表MAX", "SELECT MAX(score) AS mx FROM empty_grade", 1, False),
        ("B03", "空表AVG", "SELECT AVG(score) AS av FROM empty_grade", 1, False),
        ("B04", "空表多聚合", "SELECT COUNT(*) AS cnt, MAX(score) AS mx FROM empty_grade", 1, False),
    ])

    # --- GROUP BY ---
    tests.extend([
        ("C01", "GROUP BY int", "SELECT id, COUNT(*) AS cnt FROM grade GROUP BY id", 2, False),
        ("C02", "GROUP BY char", "SELECT course, COUNT(*) AS cnt FROM grade GROUP BY course", 2, False),
        ("C03", "多列GROUP BY", "SELECT course, id, COUNT(*) AS cnt FROM grade GROUP BY course, id", 4, False),
        ("C04", "GROUP BY + MAX/MIN", "SELECT course, MAX(score) AS mx, MIN(score) AS mn FROM grade GROUP BY course", 2, False),
        ("C05", "GROUP BY + SUM/AVG", "SELECT course, SUM(score) AS sm, AVG(score) AS av FROM grade GROUP BY course", 2, False),
    ])

    # --- HAVING ---
    tests.extend([
        ("D01", "HAVING过滤", "SELECT course, COUNT(*) AS cnt FROM grade GROUP BY course HAVING COUNT(*) > 1", 2, False),
        ("D02", "HAVING别名", "SELECT course, COUNT(*) AS cnt FROM grade GROUP BY course HAVING cnt > 1", 2, False),
        ("D03", "HAVING过滤所有", "SELECT course, COUNT(*) AS cnt FROM grade GROUP BY course HAVING COUNT(*) > 10", 0, False),
        ("D04", "HAVING聚合条件", "SELECT course, AVG(score) AS av FROM grade GROUP BY course HAVING AVG(score) > 90", 2, False),
    ])

    # --- ORDER BY ---
    tests.extend([
        ("E01", "ORDER BY列", "SELECT id FROM grade ORDER BY id", 4, False),
        ("E02", "ORDER BY DESC", "SELECT id FROM grade ORDER BY id DESC", 4, False),
        ("E03", "ORDER BY聚合别名", "SELECT course, COUNT(*) AS cnt FROM grade GROUP BY course ORDER BY cnt", 2, False),
        ("E04", "ORDER BY聚合别名DESC", "SELECT course, COUNT(*) AS cnt FROM grade GROUP BY course ORDER BY cnt DESC", 2, False),
        ("E05", "多列ORDER BY", "SELECT course, id FROM grade ORDER BY course, id", 4, False),
    ])

    # --- LIMIT ---
    tests.extend([
        ("F01", "LIMIT基础", "SELECT id FROM grade LIMIT 2", 2, False),
        ("F02", "LIMIT超界", "SELECT id FROM grade LIMIT 100", 4, False),
        ("F03", "LIMIT 0", "SELECT id FROM grade LIMIT 0", 0, False),
        ("F04", "聚合+LIMIT", "SELECT COUNT(*) AS cnt FROM grade LIMIT 1", 1, False),
        ("F05", "GROUP BY+LIMIT", "SELECT course, COUNT(*) AS cnt FROM grade GROUP BY course LIMIT 1", 1, False),
        ("F06", "ORDER BY+LIMIT", "SELECT id FROM grade ORDER BY id LIMIT 2", 2, False),
        ("F07", "空表+LIMIT", "SELECT COUNT(*) AS cnt FROM empty_grade LIMIT 1", 1, False),
    ])

    # --- JOIN + 聚合 ---
    tests.extend([
        ("G01", "JOIN+COUNT", "SELECT student.name, COUNT(*) AS cnt FROM student, score WHERE student.id = score.student_id GROUP BY student.name", 2, False),
        ("G02", "JOIN+SUM", "SELECT student.name, SUM(score.score) AS sm FROM student, score WHERE student.id = score.student_id GROUP BY student.name", 2, False),
        ("G03", "JOIN+AVG", "SELECT student.name, AVG(score.score) AS av FROM student, score WHERE student.id = score.student_id GROUP BY student.name", 2, False),
        ("G04", "JOIN+WHERE+聚合", "SELECT student.name, COUNT(*) AS cnt FROM student, score WHERE student.id = score.student_id AND score.score > 90 GROUP BY student.name", 1, False),
    ])

    # --- 健壮性 ---
    tests.extend([
        ("H01", "非聚合列不在GROUP BY中", "SELECT id, score FROM grade GROUP BY course", 0, False),  # 应失败
        ("H02", "WHERE中聚合函数", "SELECT id, MAX(score) AS mx FROM grade WHERE MAX(score) > 90 GROUP BY id", 0, False),  # 应失败
    ])

    # --- 无AS列别名 (已知盲区) ---
    tests.extend([
        ("I01", "无AS别名基础", "SELECT COUNT(*) cnt FROM grade", 1, True),
        ("I02", "无AS别名+GROUP BY", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course", 2, True),
        ("I03", "无AS别名+HAVING", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course HAVING cnt > 1", 2, True),
        ("I04", "无AS别名+ORDER BY聚合别名", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course ORDER BY cnt", 2, True),
        ("I05", "无AS列别名+ORDER BY", "SELECT id i FROM grade ORDER BY i", 4, True),
        ("I06", "无AS列别名+WHERE", "SELECT id i FROM grade WHERE id > 1", 2, True),
    ])

    # --- 表别名 (已知盲区) ---
    tests.extend([
        ("J01", "表别名SELECT", "SELECT a.id FROM grade a", 4, True),
        ("J02", "表别名WHERE", "SELECT a.id FROM grade a WHERE a.id = 1", 1, True),
        ("J03", "表别名ORDER BY", "SELECT a.id FROM grade a ORDER BY a.id", 4, True),
        ("J04", "表别名GROUP BY", "SELECT a.course, COUNT(*) FROM grade a GROUP BY a.course", 2, True),
        ("J05", "JOIN表别名", "SELECT a.id, b.score FROM grade a, score b WHERE a.id = b.student_id", 3, True),
        ("J06", "JOIN聚合表别名", "SELECT a.name, COUNT(*) FROM student a, score b WHERE a.id = b.student_id GROUP BY a.name", 2, True),
        ("J07", "JOIN聚合+ORDER BY表别名", "SELECT a.name, SUM(b.score) s FROM student a, score b WHERE a.id = b.student_id GROUP BY a.name ORDER BY s", 2, True),
    ])

    # ========== 运行测试 ==========
    passed = 0
    failed = 0
    known_failures = 0
    new_failures = []

    print(f"\n{'ID':<6} {'描述':<28} {'期望':<6} {'实际':<6} {'状态':<8} {'备注'}")
    print("-" * 80)

    for tid, desc, sql, expected, known_bug in tests:
        r = send_sql(sock, sql)
        actual = count_data_lines(r)
        
        if actual == expected:
            status = "PASS"
            passed += 1
        elif known_bug:
            status = "KNOWN"
            known_failures += 1
        else:
            status = "FAIL"
            failed += 1
            new_failures.append((tid, desc, sql, expected, actual, r))
        
        marker = "  " if status == "PASS" else "<<"
        note = "(已知盲区)" if status == "KNOWN" else ""
        print(f"{marker} {tid:<5} {desc:<27} {expected:<6} {actual:<6} {status:<8} {note}")

    # 清理
    send_sql(sock, "DROP TABLE grade;")
    send_sql(sock, "DROP TABLE empty_grade;")
    send_sql(sock, "DROP TABLE student;")
    send_sql(sock, "DROP TABLE score;")
    sock.close()
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

    # ========== 报告 ==========
    print("-" * 80)
    total = len(tests)
    print(f"总计: {passed}/{total} 通过, {failed}/{total} 新失败, {known_failures}/{total} 已知盲区")
    print("=" * 80)

    if new_failures:
        print("\n[新失败详情] (这些是不应该失败的)")
        for tid, desc, sql, exp, act, r in new_failures:
            print(f"\n{tid} {desc}")
            print(f"  SQL: {sql}")
            print(f"  期望: {exp} 行, 实际: {act} 行")
            for line in r.strip().splitlines()[:5]:
                print(f"  {line}")
        return False

    if known_failures > 0:
        print("\n[已知盲区汇总]")
        print("1. 表别名不支持: FROM table alias 后，alias.column 引用返回空")
        print("2. 无AS列别名不支持: SELECT col alias ORDER BY alias 报错")
        print("修复后可重新运行此测试验证。")

    return failed == 0

if __name__ == "__main__":
    success = run_tests()
    sys.exit(0 if success else 1)
