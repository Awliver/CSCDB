
## 常规锁

在 PostgreSQL 中，常规锁（Regular Lock）是数据库实现并发控制与访问冲突检测的基础机制，主要用于保护**用户级资源，例如表、行、事务、advisory locks 等**。当多个会话同时访问或修改同一资源时，常规锁通过协调不同锁模式（如 `AccessShareLock`、`RowExclusiveLock`、`AccessExclusiveLock` 等）之间的兼容性，确保操作顺序的正确性和数据的一致性。

常规锁具备以下几个显著特点：

- 支持多种锁模式，以精细控制并发访问；
- 存储在共享内存的锁表中，包括 LOCK（资源级）与 PROCLOCK（进程持有状态）；
- 每个后端进程维护本地锁表（LOCALLOCK），用于追踪锁的引用计数和资源拥有者；
- 可被 PostgreSQL 死锁检测机制识别；
- 用户可见，通过 `pg_locks` 视图可实时查询锁状态；
- 支持自动释放，在事务结束或子事务回滚时自动清理相关锁。

与之相对的，前面我们介绍了轻量锁 LWLock。LWLock 是一种低开销、面向内核实现的互斥机制，用于保护诸如 buffer pool、WAL 缓冲区、元数据结构等**共享内存数据结构的并发访问**。它们通常具有更细粒度，只支持共享/排他两种模式，不出现在 SQL 层，也不参与死锁检测，是 PostgreSQL 内部用于高性能并发控制的重要工具。

简而言之，常规锁服务于数据库语义正确性，保障用户操作的并发一致性；轻量锁服务于系统层同步，确保 PostgreSQL 内核模块在高并发下的线程安全。两种锁机制相辅相成，分别站在语义层与实现层，共同构建了 PostgreSQL 的并发控制体系。

### 1. 常规锁类型

PostgreSQL 将常规锁分成 8 个不同的等级，编号从 1 到 8，每种锁的编号和功能如下：

`AccessShareLock`(1)：当对一个表对象进行读取（SELECT）时添加该锁，仅与 `AccessExclusiveLock` 相冲突，属于最低级别的锁。PostgreSQL 采用 MVCC 的方式对数据进行读取，且会在硬盘中保存同一个 Tuple 的多个旧版本，同时只要还有更老的快照对该表进行读取，那么 VACUUM FULL 会等待该事务结束再进行深度清理工作，将不再需要的 DEAD Tuples 全部删除并释放 Heap Table File 空间。因此，只要表结构不发生变化（对应于 `AccessExclusiveLock`）， SELECT 总是能够得到正确答案。

`RowShareLock`(2)：和 `AccessShareLock` 一样属于读锁，当我们使用 `SELECT ... FOR UPDATE/SHARE` 时会在表对象中添加该锁，该锁与 `ExclusiveLock` 和 `AccessExclusiveLock` 冲突。当并发的更新物化视图时，会在视图上添加 `ExclusiveLock`，以阻止带有共享/排它锁的行读取，因为此时并不能保证该物化事务的数据不发生改变。

`RowExclusiveLock`(3)：当对数据对象进行 INSERT、UPDATE 和 DELETE 时即添加该锁，此时不允许对数据对象本身进行修改。例如当我们向表中插入数据时，不允许在表中创建触发器（CREATE TRIGGER）、更不允许修改表结构（ALTER、TRUNCATE、VACUUM FULL）。

`ShareUpdateExclusiveLock`(4)：该锁通常用于不带 FULL 的 VACUUM，ANALYZE，CREATE INDEX CONCURRENTLY，和 REINDEX CONCURRENTLY 中，和自身相冲突。

`ShareLock`(5)：主要用于创建索引（CREATE INDEX），和自身并不冲突，因为同一时间是允许两个事务对表创建不同的索引的。

`ShareRowExclusiveLock`(6)：该锁不允许表对象中存在修改，但允许任意形式的读取。因此和 `AccessShareLock`、`RowShareLock` 读锁是相容的。当我们为表对象创建触发器（CREATE TRIGGER）时将会获取该锁。同时，外键约束检查也是一种触发器，因此在使用 `ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY` 时也会阻止对该表对象的修改。

```sql
postgres=# BEGIN;
postgres=*# ALTER TABLE orders ADD CONSTRAINT fk_orders_user
FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE;

postgres=*# SELECT locktype,mode,granted FROM pg_locks WHERE relation = 'orders'::regclass;
 locktype |         mode          | granted 
----------+-----------------------+---------
 relation | AccessShareLock       | t
 relation | ShareRowExclusiveLock | t
(2 rows)
```

`ExclusiveLock`(7)：前面已经提到过，当对物化视图执行 `REFRESH MATERIALIZED VIEW CONCURRENTLY` 时获取该锁，仅允许不带任何锁定的读取（`AccessShareLock`）。正常来讲，更新物化视图时需要添加 `AccessExclusiveLock`，阻塞一切类型的读取，因为此时视图更新发生更新，无法保证读取一致性。但并发更新物化视图的方式通过唯一索引比较式的对物化视图进行更新，使得并发读取成为可能。

`AccessExclusiveLock`(7)：最高级别的锁，当对表对象本身内容进行修改时将会添加此锁，例如 DROP TABLE，TRUNCATE，REINDEX，CLUSTER，VACUUM FULL 和 REFRESH MATERIALIZED VIEW (without CONCURRENTLY) 等，该锁与其他锁模式均不相容。

以上 8 种常规锁的冲突性矩阵如下表所示：

| # | LockMode                  | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---------------------------|---|---|---|---|---|---|---|---|
| 1 | AccessShareLock           |   |   |   |   |   |   |   | X |
| 2 | RowShareLock              |   |   |   |   |   |   | X | X |
| 3 | RowExclusiveLock          |   |   |   |   | X | X | X | X |
| 4 | ShareUpdateExclusiveLock  |   |   |   | X | X | X | X | X |
| 5 | ShareLock                 |   |   | X | X |   | X | X | X |
| 6 | ShareRowExclusiveLock     |   |   | X | X | X | X | X | X |
| 7 | ExclusiveLock             |   | X | X | X | X | X | X | X |
| 8 | AccessExclusiveLock       | X | X | X | X | X | X | X | X |


`AccessShareLock`（SELECT）、`RowShareLock`（SELECT FOR SHARE/UPDATE） 和 `RowExclusiveLock`（UPDATE） 通常被称之为“弱锁”，它们更多的是起到意向锁的作用，对表对象本身无任何修改，最多修改行数据，因此三者是完全相容的。在系统运行的绝大多数时刻都是对表对象添加这三个锁，因此在实现中 PostgreSQL 着重对这些“弱锁”进行了优化，称之为 Fast Path，详见后续章节。

`ShareUpdateExclusiveLock` 和 `ShareLock` 类似，均允许任意类型的读取，但是 `ShareLock` 不允许对表中数据进行修改。其中 CREATE INDEX CONCURRENTLY 会获取 `ShareUpdateExclusiveLock`，而 CREATE INDEX (without CONCURRENTLY) 则会获取 `ShareLock`，它们两者之间的唯一区别就是在创建索引期间能否修改表中数据。

`ShareRowExclusiveLock` 和 `ExclusiveLock` 也比较类似，它们都不允许表对象存在任何修改，但对普通的读取相容。`ExclusiveLock` 通常只用于并发式的更新物化视图中。

PostgreSQL 使用“给冲突锁对应的 LOCKMAST 设置为 1”的方式来表示两种锁相冲突，保存在全局变量 `LockConflicts` 数组中。在使用时通过与操作判断结果是否为 0 便可知两把锁是否相容：

```cpp
#define LOCKBIT_ON(lockmode) (1 << (lockmode))

static const LOCKMASK LockConflicts[] = {
	0,

	/* AccessShareLock */
	LOCKBIT_ON(AccessExclusiveLock),

	/* RowShareLock */
	LOCKBIT_ON(ExclusiveLock) | LOCKBIT_ON(AccessExclusiveLock),

	/* RowExclusiveLock */
	LOCKBIT_ON(ShareLock) | LOCKBIT_ON(ShareRowExclusiveLock) |
	LOCKBIT_ON(ExclusiveLock) | LOCKBIT_ON(AccessExclusiveLock),

	/* ...... */
}
```

### 2. 常规锁的内存结构

常规锁的本质就是“哪个进程持有了哪些锁，在等待哪个锁的释放”，同时还需要在这之上建立锁冲突机制、死锁检测机制，以及在事务锁中最为重要的两阶段锁获取和释放，即在事务结束时释放该事务获取到的所有常规锁。为了实现这些信息的完整追踪，系统必须为锁状态跟踪：
1. 记录每个进程持有的锁
2. 记录每个进程正在等待的锁
3. 建立进程间的依赖关系（等待图），用于死锁检测与调试。

对于一个锁对象而言，我们需要记录它的类型、加锁对象、等待队列有哪些进程，以及这些进程在等待的锁模式是什么，PostgreSQL 使用 `LOCK` 结构体表示：

```cpp
typedef struct LOCK
{
	/* 锁对象的唯一标识符 */
	LOCKTAG		tag;
	/* 该对象被添加的锁模式 */
	LOCKMASK	grantMask;
	/* 哪些进程持有了该锁，PROCLOCK 组成的双向链表 */
	dlist_head	procLocks;

	/* 等待队列中进程所等候的锁模式 */
	LOCKMASK	waitMask;
	/* 等待队列，PGPROC 组成的双向链表 */
	dclist_head waitProcs;

	/* 每种锁模式下，总共请求该锁的次数（包括已授予 + 正在等待） */
	int			requested[MAX_LOCKMODES];
	/* 所有锁模式请求数的总和，sum(requested[])，用于快速判断是否有剩余请求 */
	int			nRequested;

	/* 每种锁模式下，已经授予的锁数，不包括正在等待的 */
	int			granted[MAX_LOCKMODES];
	/* 所有锁模式中已授予锁的总数，sum(granted[])，可用于判断锁是否空闲、是否有竞争等 */
	int			nGranted;
} LOCK;
```

这里简单的对 `LOCK` 结构体中的字段进行了重排，以便于更好的理解。`LOCK` 是理解常规锁的重要结构，因此我们使用较多的篇幅来对每一个字段逐一讨论分析。

首先，PostgreSQL 使用 `LOCKTAG` 来作为对象的唯一标识，其中保存了能够区分该对象的唯一标识和被加锁对象的类型，如表、页面或元组等，该标识符将会作为哈希健用于在哈希表中快速获取某个资源对象上的常规锁：

```cpp
typedef struct LOCKTAG
{
	uint32		locktag_field1; /* a 32-bit ID field */
	uint32		locktag_field2; /* a 32-bit ID field */
	uint32		locktag_field3; /* a 32-bit ID field */
	uint16		locktag_field4; /* a 16-bit ID field */
	uint8		locktag_type;	/* see enum LockTagType */
	uint8		locktag_lockmethodid;	/* lockmethod indicator */
} LOCKTAG;
```

其中 locktag_field1 到 locktag_field4 由调用方决定填入哪些值，对于一个表对象而言，二元组 `(dboid, reloid)` 便可以唯一确定，再将 `locktag_type` 写入 `LOCKTAG_RELATION` 表示这是一个表对象：

```cpp
/* (DatabaseOid, RelationOid) 唯一确定一张表 */
#define SET_LOCKTAG_RELATION(locktag,dboid,reloid) \
	((locktag).locktag_field1 = (dboid), \
	 (locktag).locktag_field2 = (reloid), \
	 (locktag).locktag_field3 = 0, \
	 (locktag).locktag_field4 = 0, \
	 (locktag).locktag_type = LOCKTAG_RELATION, \
	 (locktag).locktag_lockmethodid = DEFAULT_LOCKMETHOD)
```

又比如在表示一个页面时除了 DatabaseOid 和 RelationOid 以外，再加上 BlockNumber 就可以定位唯一的页面，其类型为 `LOCKTAG_PAGE`：

```cpp
/* (DatabaseOid, RelationOid, BlockNumber) 唯一确定一张表的某一个页面 */
#define SET_LOCKTAG_PAGE(locktag,dboid,reloid,blocknum) \
	((locktag).locktag_field1 = (dboid), \
	 (locktag).locktag_field2 = (reloid), \
	 (locktag).locktag_field3 = (blocknum), \
	 (locktag).locktag_field4 = 0, \
	 (locktag).locktag_type = LOCKTAG_PAGE, \
	 (locktag).locktag_lockmethodid = DEFAULT_LOCKMETHOD)
```

常见的 `LockTagType` 及其含义如下表所示：

| 枚举值                      | 含义说明 |
|-----------------------------|----------|
| `LOCKTAG_RELATION`          | 表级锁。用于整个关系（表），最常见的锁类型，如 `SELECT`、`INSERT`、`LOCK TABLE` 等操作都会用到。 |
| `LOCKTAG_RELATION_EXTEND`   | 表扩展锁。控制对 relation 扩展（如追加新页面），防止多个进程同时 extend（常用于并发 insert）。 |
| `LOCKTAG_DATABASE_FROZEN_IDS` | 控制 `pg_database.datfrozenxid` 的访问。用于防止多个进程同时修改数据库的“冻结事务 ID”。 |
| `LOCKTAG_PAGE`              | 页级锁。用于单个数据页（Block），例如某些行锁升级或特殊访问模式可能使用。 |
| `LOCKTAG_TUPLE`             | 行级锁。表示一个物理元组的锁，理论支持但实际 PostgreSQL 行锁通过 tuple header 控制，不常用此类型。 |
| `LOCKTAG_TRANSACTION`       | 事务锁。用于等待某个事务提交或回滚，例如外键约束中父子事务间的等待关系。 |
| `LOCKTAG_VIRTUALTRANSACTION` | 虚拟事务锁。用于等待某个虚拟事务完成，例如避免元组被并发清理（VACUUM 等）。 |
| `LOCKTAG_SPECULATIVE_TOKEN` | 推测插入锁。用于 INSERT ... ON CONFLICT 的推测性插入过程，确保唯一性验证的正确性。 |
| `LOCKTAG_OBJECT`            | 非关系型数据库对象锁。例如扩展、语言、角色、表空间等 catalog 对象。 |
| `LOCKTAG_USERLOCK`          | 用户自定义锁（早期 contrib/userlock 扩展用），现代 PostgreSQL 通常不再使用。 |
| `LOCKTAG_ADVISORY`          | 咨询锁。通过 `pg_advisory_lock` 等函数获取的用户自定义锁，不参与数据库约束，只用于应用层同步。 |
| `LOCKTAG_APPLY_TRANSACTION` | 用于逻辑复制中，表示正在应用的远程事务，避免冲突写入 |

回到 `LOCK` 中，`LOCKMASK` 表示当前对象已经持有的锁模式，根据 1~8 号锁的持锁情况，对 `LOCKMASK` 执行 `1 << LOCKMODE` 并执行或操作即可。`LOCKMASK` 中是有可能多个 bit 位被设置为 1 的，这些 bit 位所表示的锁模式全部相容。

`procLocks` 则是由 `PROCLOCK` 所组成的双向链表，用于表示哪些进程持有了该对象上的锁模式。注意这里并不是 `PGPROC` 组成的队列，而是 `PROCLOCK`。在常规锁的实现中，锁和进程之间是一个多对多关系，某个资源对象上的锁可能被多个进程同时持有，例如多个进程同时对一张表进行读取，那么该表上的 `AccessShareLock` 就在多个进程中。同时，一个进程也可以持有多个锁，即使是最简单的 SELECT 查询，也会涉及多张 catalog 表。因此，PostgreSQL 为了保存这个 Many To Many 的关系，引入了 `PROCLOCK`，其目的就是为了建立锁和进程之间的关系：

```cpp
typedef struct PROCLOCKTAG
{
	/* 持有的锁和持有锁的进程联合构成唯一标识符 */
	LOCK	   *myLock;
	PGPROC	   *myProc;
} PROCLOCKTAG;

typedef struct PROCLOCK
{
	/* tag */
	PROCLOCKTAG tag;

	/* groupLeader 用于 parallel 查询 */
	PGPROC	   *groupLeader;

	/* 当前进程在该锁上持有的锁模式 */
	LOCKMASK	holdMask;

	/* 在释放锁时标记哪些锁模式将被释放，通常用于
	 * LockReleaseAll() 中的批量释放流程 */
	LOCKMASK	releaseMask;

	/* 该 PROCLOCK 挂在 LOCK.procLocks 链表中的节点，便于从一个锁找出所有持有者 */
	dlist_node	lockLink;
	/* 该 PROCLOCK 挂在 PGPROC.proclocks 链表中的节点，便于从进程找到所有持有的锁 */
	dlist_node	procLink;
} PROCLOCK;
```

在后续内容我们还会对 `PROCLOCK` 进行详细说明，此处有一个大致印象即可。

`LOCK` 中的 `waitMask` 和 `waitProcs` 组成了等待队列，在常规锁释放时将会遍历该队列对等待进程进行唤醒，同时这也是死锁检测的重要依据之一。

接下来的 `requested[10]` 和 `granted[10]` 两个数组主要起到辅助作用，在后续的锁获取、释放以及死锁检测中将会详细描述。

各结构体关系如下图所示：

![alt text](image-3.png)

`LOCK` 和 `PROCLOCK` 均保存在共享内存中的哈希表中，`LOCK` 通过 `LOCKTAG`，也就是被保护对象作为哈希健。而 `PROCLOCK` 则是通过二元组 `(LOCK, PGPROC)` 作为哈希健，用于唯一确定哪个具体的进程持有了哪一把锁。共享内存空间在 `InitLocks()` 函数中进行初始化，一个是 `LockMethodLockHash`，另一个则是 `LockMethodProcLockHash`。

通常我们会将 `LockMethodLockHash` 称之为主锁表，位于共享内存，仅保存锁信息。与之对应的还有进程锁表 `LockMethodLocalHash`，其中保存的是 `LOCALLOCK`，为进程私有。

进程在请求或释放锁时，可能会多次对同一个锁对象加锁，因此需要一个变量来精确某个进程对某个锁的持有次数，以便支持正确的引用计数释放，而在 `LOCK` 中尽管有 `granted` 来追踪持锁次数，但是并没有保存其中的进程信息。此外，PostgreSQL 支持子事务、函数调用、SPI 查询等嵌套资源管理模型，每次加锁都需要记录归属的资源拥有者（ResourceOwner），在事务回滚或者资源释放时，能够正确释放持有的锁。`PROCLOCK` 是进程这个维度的信息，而我们需要事务维度的信息。同时，加锁/解锁是高频操作，如果每次都操作共享内存中的 `LOCK` 结构，可能会带来激烈的争用。

基于上述种种需求，本地表锁应运而生。当一个事务获取到了某个常规锁，根据事务严格两阶段锁协议，该锁必须要在事务结束时才能释放，那么当该事务再次对同一个对象获取同一个类型的锁时，也就不需要再做冲突检测了，直接将这个锁记录在本地即可，无需再去更新共享内存中的主锁表。

```cpp
typedef struct LOCALLOCKTAG
{
	/* 本地锁表的标识符，加锁对象与加锁模式 */
	LOCKTAG		lock;
	LOCKMODE	mode;
} LOCALLOCKTAG;

typedef struct LOCALLOCKOWNER
{
	/* 资源管理器 ResourceOwner 以及持锁次数 */
	struct ResourceOwnerData *owner;
	int64		nLocks;
} LOCALLOCKOWNER;

typedef struct LOCALLOCK
{
	/* 本地锁表的标识符，作为 Hash Key */
	LOCALLOCKTAG tag;

	/* LOCKTAG 的哈希结果缓存，避免每次使用时计算 */
	uint32		hashcode;

	/* 锁对象和锁进程对象 */
	LOCK	   *lock;
	PROCLOCK   *proclock;

	/* 引用计数，统计本地有多少次持有该锁 */
	int64		nLocks;

	/* 当前 ResourceOwner 数量，可以认为是动态数组的 used 指针 */
	int			numLockOwners;
	/* lockOwners 数组容量，numLockOwners >= maxLockOwners 时动态扩容 */
	int			maxLockOwners;
	/* 动态数组，保存各个 ResourceOwner 对象 */
	LOCALLOCKOWNER *lockOwners;

	/* Fast Path Lock 标记位，表示保护对象上是否有强锁 */
	bool		holdsStrongLockCount;
	/* 该锁对应的 LOCK 是否已经从共享主锁表中被清除 */
	bool		lockCleared;
} LOCALLOCK;
```

### 3. 常规锁加锁路径优化——Fast Path Lock

前面我们已经提到过“弱锁”和“强锁”的概念，其中“弱锁”特指 `AccessShareLock`（SELECT）、`RowShareLock`（SELECT FOR SHARE/UPDATE） 和 `RowExclusiveLock` (UPDATE/DELETE)，用于 DML 操作中，并且这三种类型的锁模式并不互相冲突，因为它们并不会对表结构本身进行修改。同时，这也是数据库在运行中使用最为频繁的三种锁模式，因为绝大多数的 SQL 都是对表数据进行查询和修改，只会在很少的时间内进行 VACUUM、索引建立和表结构修改。

因此，为了优化“弱锁”加锁路径，PostgreSQL 引入了 Fast Path Lock，即快速路径机制。传统上，锁操作都需要在**共享内存主锁表（LockMethodLockHash）**中定位或创建一个 `LOCK` 结构体，并加锁访问该共享表。然而，频繁的小锁请求（尤其是无冲突时）会造成不必要的开销。那么，当进程请求弱锁时，如果该对象上没有持有相冲突的锁（即没有“强锁”），那么锁信息不会写入主锁表（`LOCK`、`PROCLOCK`）中，而是只记录在本进程私有的结构中，称为 Fast Path Lock Bits。

那么，当前进程如何得知其它事务有没有在该表对象上添加了强锁呢？只需要在共享内存中引入一个标记位数组即可。当有事务对表对象添加强锁时，更新共享内存中该表的标记位，那么其它进程在尝试 Fast Path Lock 之前检查该标记位，如果发现带申请锁的表对象上已经有了强锁的话，则放弃 Fast Path Lock 加锁路径，向主锁表中进行锁申请。该共享内存结构通过 `FastPathStrongRelationLockData` 表示，其结构非常简单：

```cpp
/* 总计 1 << 10 = 1024 个 slot */
#define FAST_PATH_STRONG_LOCK_HASH_BITS			10
#define FAST_PATH_STRONG_LOCK_HASH_PARTITIONS \
	(1 << FAST_PATH_STRONG_LOCK_HASH_BITS)

/* 这里的 hashcode 就是 LOCKTAG 的哈希结果 */
#define FastPathStrongLockHashPartition(hashcode) \
	((hashcode) % FAST_PATH_STRONG_LOCK_HASH_PARTITIONS)

typedef struct
{
	/* 自旋锁，用于保护 count 数组的查询和修改 */
	slock_t		mutex;
	/* 标记位数组，大小固定为 1024，可能产生哈希冲突导致 False Positive */
	uint32		count[FAST_PATH_STRONG_LOCK_HASH_PARTITIONS];
} FastPathStrongRelationLockData;
```

`FastPathStrongRelationLockData` 被分成了 1024 个哈希槽位，其中每个槽中的 `count[i]` 表示当前表对象被多少个进程申请了强锁，并不表示实际获取到强锁的进程数量，再加锁时只是简单的对计数器加一，释放时减一，该数组并不会对强锁进行冲突检测。当 `count[i]` 不为 0 时，添加弱锁时就不可以使用 Fast Path Lock，而是必须经过主锁表。

Fast Path Lock 机制主要由两个结构完成，一个是 `uint64` 类型的 `fpLockBits`，用于保存每种弱锁的持锁情况；一个是与之对应的 `Oid` 数组 `fpRelId`，用于保存具体是哪个表对象持有了弱锁。

Fast Path Lock 采用位图编码的方式来记录某张表上是否持有 `AccessShareLock`、`RowShareLock` 和 `RowExclusiveLock`，因此 PostgreSQL 使用 3 个 bit 一组的方式，将 64 位无符号整型划分成最多 21 个 slots。目前只使用了前 16 个 slots，也就是能够保存 16 个不同表对象持有弱锁的情况。整体结构如下图所示：

![alt text](image.png)

在每个 slot 中，第 0 位保存 `AccessShareLock`，第 1 位保存 `RowShareLock`，第 3 位则保存 `RowExclusiveLock`。当需要设置 Fast Path Lock 时，首先以位图数组的方式遍历 `fpLockBits` 寻找一个空闲槽位 unused_slot，而后根据 LOCKMODE 的值将对应 bit 位设置为 1，同时更新 `fpRelId` 数组对应 slot 的值为当前表对象的 OID。

```cpp
/* 对应 AccessShareLock、RowShareLock 和 RowExclusiveLock 3 种锁 */
#define FAST_PATH_BITS_PER_SLOT			3
/* AccessShareLock 的 LOCKMODE 为 1，但是 bit 从 0 开始计数，因此需要减去一个偏移量 */
#define FAST_PATH_LOCKNUMBER_OFFSET		1

/* unused slot 判定，每个 slot 中有 3 个弱锁，故 (1<<3)-1 */
#define FAST_PATH_MASK					((1 << FAST_PATH_BITS_PER_SLOT) - 1)

/* n 为 slot 编号，首先通过右移操作获取对应 slot 的值，再与 FAST_PATH_MASK 进行 '&' 操作，
 * 通过结果是否为 0 即可判断编号为 n 的 slot 是否空闲（未被占用）*/
#define FAST_PATH_GET_BITS(proc, n) \
	(((proc)->fpLockBits >> (FAST_PATH_BITS_PER_SLOT * n)) & FAST_PATH_MASK)

/* n 为 slot 编号，l 为 LOCKMODE */
#define FAST_PATH_BIT_POSITION(n, l) \
	(AssertMacro((l) >= FAST_PATH_LOCKNUMBER_OFFSET), \
	 AssertMacro((l) < FAST_PATH_BITS_PER_SLOT+FAST_PATH_LOCKNUMBER_OFFSET), \
	 AssertMacro((n) < FP_LOCK_SLOTS_PER_BACKEND), \
	 ((l) - FAST_PATH_LOCKNUMBER_OFFSET + FAST_PATH_BITS_PER_SLOT * (n)))

#define FAST_PATH_SET_LOCKMODE(proc, n, l) \
	 (proc)->fpLockBits |= UINT64CONST(1) << FAST_PATH_BIT_POSITION(n, l)

#define FAST_PATH_CLEAR_LOCKMODE(proc, n, l) \
	 (proc)->fpLockBits &= ~(UINT64CONST(1) << FAST_PATH_BIT_POSITION(n, l))

#define FAST_PATH_CHECK_LOCKMODE(proc, n, l) \
	 ((proc)->fpLockBits & (UINT64CONST(1) << FAST_PATH_BIT_POSITION(n, l)))

static bool
FastPathGrantRelationLock(Oid relid, LOCKMODE lockmode)
{
	uint32		f;
	/* 宏 FP_LOCK_SLOTS_PER_BACKEND 的值目前为 16，unused_slot 初始化成一个界外值 */
	uint32		unused_slot = FP_LOCK_SLOTS_PER_BACKEND;

	/* 以数组的方式遍历 fpLockBits 位图 */
	for (f = 0; f < FP_LOCK_SLOTS_PER_BACKEND; f++)
	{
		/* 判断当前 slot 是否为空 */
		if (FAST_PATH_GET_BITS(MyProc, f) == 0)
			unused_slot = f;
		/* 或者当前已经持有了该 relid 的弱锁，此时根据 f 这个 slot 编号更新对应 bit 即可 */
		else if (MyProc->fpRelId[f] == relid)
		{
			Assert(!FAST_PATH_CHECK_LOCKMODE(MyProc, f, lockmode));
			FAST_PATH_SET_LOCKMODE(MyProc, f, lockmode);
			return true;
		}
	}

	/* 找到了一个空闲 slot，更新 fpRelId 数组和 fpLockBits 比特位 */
	if (unused_slot < FP_LOCK_SLOTS_PER_BACKEND)
	{
		MyProc->fpRelId[unused_slot] = relid;
		FAST_PATH_SET_LOCKMODE(MyProc, unused_slot, lockmode);
		++FastPathLocalUseCount;
		return true;
	}

	/* 此时 slot 已满，并且 relid 在其中未有对应项 */
	return false;
}
```

上述宏定义稍微有些复杂，我们来拆解一下：

首先，`FAST_PATH_BITS_PER_SLOT` 表示一个 slot 中存在多少个 bit 位需要进行管理，我们有 3 种弱锁需要保存，因此 `FAST_PATH_BITS_PER_SLOT` 的值就被定义为 3。当我们知道了 slot 的编号，假设为 `n`，那么 `n * FAST_PATH_BITS_PER_SLOT` 就是该 slot 的起始位置，亦即 `AccessShareLock` 的值。再往前一位就是 `RowShareLock`，往前两位就是 `RowExclusiveLock`。

![alt text](image-1.png)

通过 `fpLockBits >> (3 * n)` 便可将 slot n 的三个 bit 位“挪移”到前 3 位，此时和 `(1 << 3) - 1` 进行与操作，便可快速判断 slot n 中的 3 个 bit 是否有值，其效果和我们一位一位的判断是一样的，但其效率更高。

在设置对应的 bit 位时，只需要根据 LOCKMODE 和 slot 的编号进行移位操作即可，由于`AccessShareLock` 的 LOCKMODE 为 1，但是 bit 从 0 开始计数，因此需要减去一个偏移量。因此在寻找一个特定比特位时，我们使用 `n * 3 + (LOCKMODE - 1)`，对应上方的 `FAST_PATH_BIT_POSITION` 宏定义。

当我们理解了 `fpLockBits` 如何获取和设置值以后，获取 Fast Path Lock 就很简单了，直接遍历位图数组，要么寻找到一个空闲槽位，要么 relid 在 `fpRelId` 数组中。


### 4. 常规锁的加锁

首先，如果申请的锁模式为 `AccessShareLock`、`RowShareLock` 或 `RowExclusiveLock` 的话，则尝试进入 Fast Path Lock 模式，将锁保存在 `PGPROC->fpLockBits` 对应的 slot 中，此时有两种可能：

1. `FastPathStrongRelationLockData` 中对应哈希槽的值为 0，也就是该对象上没有被添加任何的弱锁，并且 `PGPROC->fpLockBits` 中仍有空闲槽位，则进入 Fast Path Lock 加锁模式。
2. `FastPathStrongRelationLockData` 中对应哈希槽的值不为 0，即该对象被其它进程添加了强锁，或者 `PGPROC->fpLockBits` 没有空闲槽位，则进入主锁表加锁。

如果申请的锁模式为强锁，即 `ShareUpdateExclusiveLock` 以后的锁，则无法进入快速加锁模式，同时还需要将进程本地保存的所有弱锁转移至主锁表，在主锁表中进行冲突检测。

在上述过程中，无论是强锁还是弱锁，是否经过了 Fast Path Lock 路径优化，都需要获取或创建一个本地锁，也就是 `LOCALLOCK` 对象，我们需要将资源拥有者 Resource Owner 和常规锁对象进行绑定，以便实现事务结束时的锁释放。

主锁表、FastPathStrongRelationLockData、进程锁表和 Fast Path Lock 的关系如下图所示：

![alt text](image-2.png)

图中虚线部分表示“可能性”，在常规锁加锁中，可能用到快速路径优化，也可能在主锁表进行，但本地锁表是必然需要的。

上述流程通过 `LockAcquireExtended` 实现，在该函数中就是对本地锁表、快速路径和主锁表的封装，虽然函数主体很长，但逻辑很清晰：

```cpp
LockAcquireResult
LockAcquireExtended(const LOCKTAG *locktag,
					LOCKMODE lockmode,
					bool sessionLock,
					bool dontWait,
					bool reportMemoryError,
					LOCALLOCK **locallockp)
{
	/* Session Lock 的生命周期要大于普通事务常规锁 */
	if (sessionLock)
		owner = NULL;
	else
		owner = CurrentResourceOwner;

	/* 创建本地锁 LOCALLOCK Entry，尝试现在本地哈希表中寻找 */
	MemSet(&localtag, 0, sizeof(localtag));
	localtag.lock = *locktag;
	localtag.mode = lockmode;

	locallock = (LOCALLOCK *) hash_search(LockMethodLocalHash,
										  &localtag,
										  HASH_ENTER, &found);

	/* 本地哈希表未找到，创建之 */
	if (!found)
	{
		/* 省去对 locallock 中字段的赋值代码 */
		locallock->lockOwners = (LOCALLOCKOWNER *)
			MemoryContextAlloc(TopMemoryContext,
							   locallock->maxLockOwners * sizeof(LOCALLOCKOWNER));
	}
	else
	{
		/* 确保 lockOwners 数组容量足够，若不足，则 2 倍扩容，无上限 */
		if (locallock->numLockOwners >= locallock->maxLockOwners)
		{
			int			newsize = locallock->maxLockOwners * 2;

			locallock->lockOwners = (LOCALLOCKOWNER *)
				repalloc(locallock->lockOwners, newsize * sizeof(LOCALLOCKOWNER));
			locallock->maxLockOwners = newsize;
		}
	}
	hashcode = locallock->hashcode;

	if (locallockp)
		*locallockp = locallock;

	/* 如果当前进程已经持有了该锁，即本地锁的计数数量大于 0，可直接返回 */
	if (locallock->nLocks > 0)
	{
		/* 此时，GrantLockLocal 会对该本地锁的计数数量加一 */
		GrantLockLocal(locallock, owner);
		if (locallock->lockCleared)
			return LOCKACQUIRE_ALREADY_CLEAR;
		else
			return LOCKACQUIRE_ALREADY_HELD;
	}

	/* 当在 relid 上添加弱锁，并且 Fast Path Lock Slot 足够时，尝试 Fast Path Lock */
	if (EligibleForRelationFastPath(locktag, lockmode) &&
		FastPathLocalUseCount < FP_LOCK_SLOTS_PER_BACKEND)
	{
		/* 计算当前锁对象在 FastPathStrongRelationLocks 数组中的索引下标 */
		uint32		fasthashcode = FastPathStrongLockHashPartition(hashcode);
		bool		acquired;

		LWLockAcquire(&MyProc->fpInfoLock, LW_EXCLUSIVE);

		/* 当有其它进程对该对象添加了强锁时，不允许使用 Fast Path Lock */
		if (FastPathStrongRelationLocks->count[fasthashcode] != 0)
			acquired = false;
		else
			acquired = FastPathGrantRelationLock(locktag->locktag_field2,
												 lockmode);
		LWLockRelease(&MyProc->fpInfoLock);

		/* Fast Path Lock 取锁成功，更新本地锁表，记录 owner */
		if (acquired)
		{
			locallock->lock = NULL;
			locallock->proclock = NULL;
			GrantLockLocal(locallock, owner);
			return LOCKACQUIRE_OK;
		}
	}

	/* 获取强锁，此时必须将 relid 对应的本地 Fast Path Lock 转移至共享内存中的主锁表 */
	if (ConflictsWithRelationFastPath(locktag, lockmode))
	{
		uint32		fasthashcode = FastPathStrongLockHashPartition(hashcode);

		/* 此时会更新 FastPathStrongRelationLocks->count[fasthashcode]，
		 * 若此刻有其它进程尝试获取弱锁，则会进入主锁表，而不是 Fast Path */
		BeginStrongLockAcquire(locallock, fasthashcode);

		/* 遍历 allProcs 数组，收集所有 relid 上的弱锁并转移至主锁表，
		 * 该函数只会在共享内存不足时才会返回 false，因此绝大多数情况都会成功 */
		if (!FastPathTransferRelationLocks(lockMethodTable, locktag,
										   hashcode))
		{
			/* 回滚 BeginStrongLockAcquire() 做的所有操作，并在需要时抛出 OOM Error */
			AbortStrongLockAcquire();
			if (locallock->nLocks == 0)
				RemoveLocalLock(locallock);
			if (locallockp)
				*locallockp = NULL;
			if (reportMemoryError)
				ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY),
						 errmsg("out of shared memory"));
			else
				return LOCKACQUIRE_NOT_AVAIL;
		}
	}

	/* 此时，要么本地 Fast Path Lock 容量不足，要么是获取强锁，并且已经将弱锁转移至主锁表中。
	 * 无论哪种情况，都需要对主锁表进行操作 */
	partitionLock = LockHashPartitionLock(hashcode);
	LWLockAcquire(partitionLock, LW_EXCLUSIVE);

	/* 在主锁表中查找或创建一个常规锁对象，返回 PROCLOCK */
	proclock = SetupLockInTable(lockMethodTable, MyProc, locktag,
								hashcode, lockmode);
	if (!proclock)
	{
		/* OOM 的情况，与上方 Abort 流程一样 */
		AbortStrongLockAcquire();
		/* ...... */
	}
	locallock->proclock = proclock;
	lock = proclock->tag.myLock;
	locallock->lock = lock;

	/* 锁冲突检测 */
	if (lockMethodTable->conflictTab[lockmode] & lock->waitMask)
		found_conflict = true;
	else
		found_conflict = LockCheckConflicts(lockMethodTable, lockmode,
											lock, proclock);

	if (!found_conflict)
	{
		/* No conflict with held or previously requested locks */
		GrantLock(lock, proclock, lockmode);
		GrantLockLocal(locallock, owner);
	}
	else
	{
		/*
		 * Set bitmask of locks this process already holds on this object.
		 */
		MyProc->heldLocks = proclock->holdMask;


		WaitOnLock(locallock, owner, dontWait);


		/*
		 * Check the proclock entry status. If dontWait = true, this is an
		 * expected case; otherwise, it will only happen if something in the
		 * ipc communication doesn't work correctly.
		 */
		if (!(proclock->holdMask & LOCKBIT_ON(lockmode)))
		{
			AbortStrongLockAcquire();

			if (dontWait)
			{
				/*
				 * We can't acquire the lock immediately.  If caller specified
				 * no blocking, remove useless table entries and return
				 * LOCKACQUIRE_NOT_AVAIL without waiting.
				 */
				if (proclock->holdMask == 0)
				{
					uint32		proclock_hashcode;

					proclock_hashcode = ProcLockHashCode(&proclock->tag,
														 hashcode);
					dlist_delete(&proclock->lockLink);
					dlist_delete(&proclock->procLink);
					if (!hash_search_with_hash_value(LockMethodProcLockHash,
													 &(proclock->tag),
													 proclock_hashcode,
													 HASH_REMOVE,
													 NULL))
						elog(PANIC, "proclock table corrupted");
				}
				else
					PROCLOCK_PRINT("LockAcquire: NOWAIT", proclock);
				lock->nRequested--;
				lock->requested[lockmode]--;
				LOCK_PRINT("LockAcquire: conditional lock failed",
						   lock, lockmode);
				Assert((lock->nRequested > 0) &&
					   (lock->requested[lockmode] >= 0));
				Assert(lock->nGranted <= lock->nRequested);
				LWLockRelease(partitionLock);
				if (locallock->nLocks == 0)
					RemoveLocalLock(locallock);
				if (locallockp)
					*locallockp = NULL;
				return LOCKACQUIRE_NOT_AVAIL;
			}
			else
			{
				LWLockRelease(partitionLock);
				elog(ERROR, "LockAcquire failed");
			}
		}
	}

	/* 简单更新 StrongLockInProgress 的值为 NULL */
	FinishStrongLockAcquire();

	LWLockRelease(partitionLock);
	return LOCKACQUIRE_OK;
}
```

该函数流程图如下所示：

![alt text](image-5.png)