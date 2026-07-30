

## PostgreSQL 轻量锁（LWLock）的实现

在 PostgreSQL 中，锁从自底而上可以被分为 4 个层次：

1. 由硬件提供的原子操作，如原子的对 32/64 位整数的读取和写入，TAS 和 CAS 操作。PostgreSQL 对这些方法进行了进一步的封装，构成了 `pg_atomic_fetch_xxx` 和 `pg_atomic_compare_exchange_xxx` 两大类方法。
2. PostgreSQL 自行实现的自旋锁，基于上述原子操作实现，主要用于锁的获取和释放操作中的极短临界区。
3. 轻量锁（Light-Weight Lock），本质上是一种读写锁，用于保护几乎所有共享内存中的数据读取和写入，同时也是常规锁的基础实现，在 PostgreSQL 中大范围使用。可以说一但轻量锁的运行效率降低，数据库的整体性能都会受到影响。
4. 常规锁（Regular Lock），例如表锁、Tuple 锁、等数据库维度的对象锁，能够被用户感知。在轻量锁的基础之上提供了死锁检测、视图观测等诸多方便的功能。

![alt text](image-2.png)


### 1. 原子操作 TAS/CAS

TAS 表示 Test-And-Set，直接修改一个内存位置的值，并返回修改之前的值，并通过修改之前的值来进行进行判断。TAS 是一种古老的同步原语，或者说是一种方法，系统通常不会有直接的 API 实现。在获取队列锁中我们会使用 TAS 的方式来完成，详细内容可见下方讨论。

CAS 则是无锁并发结构的重要基石，可以说如果没有原子性的 Compare And Swap 操作，就没有无锁数据结构的诞生。CAS 通常表示一种乐观并发的思想，我们不断的对某一个内存位置的值进行轮询，判断其值是否被其它进程/线程改变，若该值与期望值不一样，则重新获取内存值并进行新一轮的尝试。相对于互斥量而言，CAS 的操作代价非常小，但在严重锁争抢时可能会带来更多的 CPU 空转消耗。

GNU 提供了一系列的 CAS 操作，比如对目标内存进行加法、减法、或、与、非的原子操作，根据返回值和函数语义的不同可以分为两大类：

一类是直接对内存进行操作并返回旧值或新值，此时相当于 TAS 语义，例如下方的一系列方法：

```cpp
/* 将 value 加到 *ptr 上，结果更新到 *ptr，并返回操作之前 *ptr 的值 */
type __sync_fetch_and_add (type *ptr, type value, ...)

/* 将 *ptr 减去 value，结果更新到 *ptr，并返回操作之前 *ptr 的值 */
type __sync_fetch_and_sub (type *ptr, type value, ...)

/* 将 *ptr 与 value 执行或操作，结果更新到 *ptr，并返回操作之前 *ptr 的值 */
type __sync_fetch_and_or (type *ptr, type value, ...)

/* 将 *ptr 与 value 执行与操作，结果更新到 *ptr，并返回操作之前 *ptr 的值 */
type __sync_fetch_and_and (type *ptr, type value, ...)

/* 将 *ptr 与 value 执行异或操作，结果更新到 *ptr，并返回操作之前 *ptr 的值 */
type __sync_fetch_and_xor (type *ptr, type value, ...)

/* 将 value 加到 *ptr 上，结果更新到 *ptr，并返回操作之后新 *ptr 的值 */
type __sync_add_and_fetch (type *ptr, type value, ...)

/* 将 *ptr 减去 value，结果更新到 *ptr，并返回操作之后 *ptr 的值 */
type __sync_sub_and_fetch (type *ptr, type value, ...)

......
```

另一类则是先判断目标内存值与期望值是否相同，如果相同则执行相关操作并返回 true，否则直接返回 false：

```cpp
/* 比较 *ptr 与 oldval 的值，如果两者相等，则将 newval 更新到 *ptr 并返回 true；否则返回 false */
bool __sync_bool_compare_and_swap (type *ptr, type oldval, type newval, ...)

/* 比较 *ptr 与 oldval 的值，如果两者相等，则将 newval 更新到 *ptr 并返回操作之前 *ptr 的值 */
type __sync_val_compare_and_swap (type *ptr, type oldval, type newval, ...)
```

详细内容可参考 GCC 官方文档：[Legacy __sync Built-in Functions for Atomic Memory Access](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fsync-Builtins.html)

但 `__sync` 系列的 API 目前很少被使用，在 GCC 4.1 (2006年) 便存在了。在后续的版本更迭中，GCC 引入了 `__atomic` 系列的 API，提供了更为丰富的功能，例如可以指定更加精细的内存序、支持 weak CAS 等。目前在 PostgreSQL 中使用了下方两个 API：

```cpp
/* 直接将 val 写入 *ptr 中, 并返回操作之前 *ptr 的值 */
type __atomic_exchange_n (type *ptr, type val, int memorder);

/* 比较 *ptr 和 *expected 的值：
 * 1) 若两者相等，则执行 read-modify-write 操作，将 *ptr 的值修改为 desired 并返回 true
 * 2) 若两者不相等，则执行 read 操作，将 *ptr 写入 *expected 供调用方重试，并返回 false
 */
bool __atomic_compare_exchange_n (type *ptr, 
								  type *expected, 
								  type desired, 
								  bool weak, 
								  int success_memorder, 
								  int failure_memorder);
```

详细内容可参考 GCC 官方文档：[Built-in Functions for Memory Model Aware Atomic Operations](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)

由于存在不同版本、不同类型的原子操作，PostgreSQL 中 `atomics/generics-gcc.h` 宏组织稍显混乱，但基本原理并未发生改变。当我们看到 `pg_atomic_fetch_and_xxx` 时就可以知道这是一个 TAS 操作，直接对 `*ptr` 做加法/减法/或/与/非，并返回旧值。`pg_atomic_compare_exchange_xx` 则是 CAS 操作，先判断目标值是否与期望值相同，若相同则更新成新值。再加上目标值的类型为 32/64 位这一维度的信息，便构成了 PostgreSQL 所有的原子操作。

下面对 `port/atomics.h` 中最常用的原子方法签名做一个简单的梳理：

```cpp
/* 初始化 */
void pg_atomic_init_u32(volatile pg_atomic_uint32 *ptr, uint32 val);
void pg_atomic_init_u64(volatile pg_atomic_uint64 *ptr, uint64 val);

/* 原子读取与写入 */
uint32 pg_atomic_read_u32(volatile pg_atomic_uint32 *ptr);
uint64 pg_atomic_read_u64(volatile pg_atomic_uint64 *ptr);
void pg_atomic_write_u32(volatile pg_atomic_uint32 *ptr, uint32 val);
void pg_atomic_write_u64(volatile pg_atomic_uint64 *ptr, uint64 val);

/* 直接将 val 写入 *ptr 中, 并返回操作之前 *ptr 的值，没有 compare 过程 */
uint32 pg_atomic_exchange_u32(volatile pg_atomic_uint32 *ptr, uint32 newval);
uint64 pg_atomic_exchange_u64(volatile pg_atomic_uint64 *ptr, uint64 newval);

/* 比较 *ptr 和 *expected 的值在决定是否写入，返回布尔值 */
bool pg_atomic_compare_exchange_u32(volatile pg_atomic_uint32 *ptr,
							   uint32 *expected, uint32 newval);
bool pg_atomic_compare_exchange_u64(volatile pg_atomic_uint64 *ptr,
							   uint64 *expected, uint64 newval);

/* 下方函数表示对 *ptr 进行加法、减法、与、或操作，并返回 *ptr 更新之前的值 */
uint32 pg_atomic_fetch_add_u32(volatile pg_atomic_uint32 *ptr, int32 add_);
uint64 pg_atomic_fetch_add_u64(volatile pg_atomic_uint64 *ptr, int64 add_);

uint32 pg_atomic_fetch_sub_u32(volatile pg_atomic_uint32 *ptr, int32 sub_);
uint64 pg_atomic_fetch_sub_u64(volatile pg_atomic_uint64 *ptr, int64 sub_);

uint32 pg_atomic_fetch_and_u32(volatile pg_atomic_uint32 *ptr, uint32 and_);
uint64 pg_atomic_fetch_and_u64(volatile pg_atomic_uint64 *ptr, uint64 and_);

uint32 pg_atomic_fetch_or_u32(volatile pg_atomic_uint32 *ptr, uint32 or_);
uint64 pg_atomic_fetch_or_u64(volatile pg_atomic_uint64 *ptr, uint64 or_);

/* 下方函数表示对 *ptr 进行加法、减法操作，并返回 *ptr 更新之后的值 */
uint32 pg_atomic_add_fetch_u32(volatile pg_atomic_uint32 *ptr, int32 add_);
uint64 pg_atomic_add_fetch_u64(volatile pg_atomic_uint64 *ptr, int64 add_);

uint32 pg_atomic_sub_fetch_u32(volatile pg_atomic_uint32 *ptr, int32 sub_);
uint64 pg_atomic_sub_fetch_u64(volatile pg_atomic_uint64 *ptr, int64 sub_);
```

方法名中便隐含了操作顺序，例如 `_fetch_add_` 就表示先获取值再进行加法，那么返回值就是 `*ptr` 执行加法之前的值；`_add_fetch_` 则表示先进行加法再取值，那么返回值就是 `*ptr` 执行加法之后的值。`_exchange_` 不带 compare，则直接写值；`_compare_exchange_` 带有 compare，即先 compare 再 exchange，其中便隐含了判断。

### 2. 自旋锁

自旋锁是 PostgreSQL 自行实现的一个指数退避的自旋锁。其内部维护了一些基本状态和观测量：

```cpp
typedef struct
{
	/* 统计当前调用过程中执行了多少次自旋循环，即 CPU 空转次数 */
	int         spins;

	/* 统计执行了多少次自旋退避操作； */
	int			delays;
	/* 当前自旋退避的延迟时间，通常是一个指数增长的值 */
	int			cur_delay;

	/* 表示调用 init_local_spin_delay() 的源码文件名、行数和函数名，用于调试 */
	const char *file;
	int			line;
	const char *func;
} SpinDelayStatus;
```

自旋锁由 `SpinDelayStatus` 所表示，其中 `spins` 统计该自旋锁空转了多少次。PostgreSQL 的自旋策略并不是死等，而是自适应的。当 `spins` 的值小于默认值 100 时会进行忙等自旋，也就是 CPU 空转，什么都不做。当 `spins` 超过 100 时，说明锁竞争非常激励，此时继续忙等待不是一个好的策略，转而执行退避（sleep）操作，而后重试。

`delays` 则统计当前自旋锁执行了多少次退避操作，当该值超过阈值时（默认 1000），说明程序或者说 OS 环境可能出现了问题，进程将抛出 PANIC 错误并退出。PostgreSQL 限制了自旋锁的最大退避时间，也就是 `cur_delay` 的最大值为 1000 ms，以该值来计算当 `delays` 达到 1000 时，进程将会睡眠接近 1000s，在这个时间内如果还未能获取自旋锁，那么一定是出现了某些异常。

自旋锁的使用如下方代码所示，首先通过 `init_local_spin_delay()` 初始化一个自旋锁，而后调用 `perform_spin_delay()` 进行自旋，当条件满足退出自旋时调用 `finish_spin_delay()` 进行一些数据更新动作：

```cpp
{
	SpinDelayStatus delayStatus;
	init_local_spin_delay(&delayStatus);

	while (不满足条件，需要自旋)
	{
		perform_spin_delay(&delayStatus);
		/* 重新获取状态并进行新一轮判断 */
		old_state = pg_atomic_read_u32(...);
	}

	finish_spin_delay(&delayStatus);
}
```

`init_local_spin_delay()` 的逻辑相对简单，只是将 `SpinDelayStatus` 的值赋予初值，因此我们直接来看 `perform_spin_delay()` 的实现：

```cpp
#define MIN_SPINS_PER_DELAY 10
#define MAX_SPINS_PER_DELAY 1000
#define MIN_DELAY_USEC		1000L
#define MAX_DELAY_USEC		1000000L

#define DEFAULT_SPINS_PER_DELAY  100

void
perform_spin_delay(SpinDelayStatus *status)
{
	/* 只有在 spins >= DEFAULT_SPINS_PER_DELAY 时才进入退避逻辑 */
	if (++(status->spins) >= spins_per_delay)
	{
		/* 退避次数超过 1000，直接 PANIC 退出 */
		if (++(status->delays) > NUM_DELAYS)
			s_lock_stuck(status->file, status->line, status->func);

		/* cur_delay 设置为 MIN_DELAY_USEC，个人认为在 init_local_spin_delay() 中设置更好 */
		if (status->cur_delay == 0)
			status->cur_delay = MIN_DELAY_USEC;

		/* 睡眠，让出 CPU */
		pg_usleep(status->cur_delay);

		/* 随机增加退避值为 [1.0, 2.0) 倍，+0.5 是为了人为四舍五入 */
		status->cur_delay += (int) (status->cur_delay *
									pg_prng_double(&pg_global_prng_state) + 0.5);

		/* 超过最大值时从 MIN_DELAY_USEC 重新开始 */
		if (status->cur_delay > MAX_DELAY_USEC)
			status->cur_delay = MIN_DELAY_USEC;

		/* 重置 spins 的值，下次会首先空转 100 次，若还没有取得自旋锁则继续执行退避 */
		status->spins = 0;
	}
}
```

注意到在执行完一次退避操作，即 `pg_usleep()` 之后，会将 `spins` 重置为 0。因此下一次再进行自旋时，又会回到忙等待模式，让 CPU 空转 100 次后再进入新的退避模式中，如下图所示：

![alt text](image.png)

当不再需要自旋锁时，调用 `finish_spin_delay()` 更新 `spins_per_delay` 这一全局变量，做收尾处理并维护调试/统计信息。

```cpp
void
finish_spin_delay(SpinDelayStatus *status)
{
	if (status->cur_delay == 0)
	{
		/* we never had to delay */
		if (spins_per_delay < MAX_SPINS_PER_DELAY)
			spins_per_delay = Min(spins_per_delay + 100, MAX_SPINS_PER_DELAY);
	}
	else
	{
		if (spins_per_delay > MIN_SPINS_PER_DELAY)
			spins_per_delay = Max(spins_per_delay - 1, MIN_SPINS_PER_DELAY);
	}
}
```

其目的就在于根据当前系统环境自适应 busy waiting 的次数。在多核 CPU 上自旋通常比较有效，因为另一个 CPU 很快能释放锁，此时我们更希望多的自旋，更少的 sleep；在单核 CPU 上自旋几乎没用，因为持锁进程无法同时运行，此时我们希望更多的 sleep，更少的自旋以节省资源。因此我们需要根据当次的自旋结果来自适应的调整 `spins_per_delay` 的值。

- 如果大部分时间能直接获取到锁，说明多核并发环境有效，快速增加 `spins_per_delay`
- 如果经常需要延迟才能拿到锁，说明锁竞争激烈或者单核，缓慢减少 `spins_per_delay`

### 3. 轻量锁

在有了原子操作和自旋锁的基本实现之后，再加上 OS 提供的信号量机制，便可以完整的实现一个效率非常高的互斥锁了。首先来看 `LWLock` 基本结构体：

```cpp
typedef struct LWLock
{
	/* tranche ID 表示该轻量锁的分组 ID，或者说轻量锁所保护对象的类型 */
	uint16		tranche;

	/* 轻量锁核心实现，通过 state 表示共享锁锁的状态、等待队列锁状态 */
	pg_atomic_uint32 state;

	/* 双向链表构成的等待队列 */
	proclist_head waiters;

#ifdef LOCK_DEBUG
	/* DEBUG 信息，当前轻量锁的等待队列大小 */
	pg_atomic_uint32 nwaiters;
	/* 最后释放独占锁的进程 */
	struct PGPROC *owner;
#endif
} LWLock;
```

`tranche` 用来表示 该 LWLock 所属的锁分组（tranche）ID。在 PostgreSQL 中会有很多个 LWLock，这些锁的用途不止一个，例如缓冲区管理，WAL 的写入，事务 ID 的生成，OID 的生成....为了能区分它们的用途、在日志/调试中方便显示、或在性能统计中按类型聚合，PostgreSQL 给 LWLock 引入了 tranche 的概念：每个 tranche 表示一个“锁类型分组”。一组 LWLocks 虽然各自封锁不同的内容，但是它们的功能相同或者相近，那么这些轻量锁就会被分配至同一个 tranche 中。

分组由枚举变量 `BuiltinTrancheIds` 表示，截取一些代码片段作为例子：

```cpp
typedef enum BuiltinTrancheIds
{
	LWTRANCHE_XACT_BUFFER = NUM_INDIVIDUAL_LWLOCKS,
	/* 用于保护 WAL 日志的写入 */
	LWTRANCHE_WAL_INSERT,
	/* 用于保护本地 Fastpath Lock */
	LWTRANCHE_LOCK_FASTPATH,
	/* 用于保护位于共享内存中的 SLRU 缓存结构 */
	LWTRANCHE_XACT_SLRU,
}	BuiltinTrancheIds;
```

在 `src/include/storage/lwlocklist.h` 中保存了当前全部 LWLocks，在编译 PostgreSQL 时会自动的生成一个头文件 `src/backend/storage/lmgr/lwlocknames.h`，并通过 `CreateLWLocks()` 自动化的创建这些预定义的轻量锁，并保存在共享内存 `MainLWLockArray` 中。分组信息由 `LWLockRegisterTranche()` 方法写入，感兴趣的读者可自行查看该函数内容。

`state` 是轻量锁最为核心，也是最为精妙的实现。通过对其内部 32 个比特位的划分完成了共享锁、排它锁、等待队列锁和其它并发安全的内容，是一个很好的学习案例。现在我们来看一下其内部比特位的划分：

![alt text](image-3.png)

其中 0~23 位表示共享锁以及持有共享锁的进程数量。第 0 位被标记为 `LW_VAL_SHARED`，用于快速判断当前锁是否为共享锁。当共享锁被其它进程持有时，若当前进程也想取得该共享锁的话，直接对 `state` 进行原子自增操作即可，表示持有共享锁的进程又多了一个。整体 24 位的共享锁计数器也足够系统使用，毕竟最大进程数被限制在 `2^23-1`。

第 24 位 `LW_VAL_EXCLUSIVE` 用于表示排它锁以及排它锁是否被进程持有。其值为 1 时表示有进程正在持有该排它锁，否则表示空闲。前 25 位共同组成了 `LW_LOCK_MASK`，表示是否有进程持有该锁，这在获取排它锁时非常有用。因为排它锁不仅和排它锁互斥，还和共享锁互斥。在有了 `LW_LOCK_MASK` 之后，只需判断 `state & LW_LOCK_MASK` 的值是否大于 0 即可判断该锁是否被其它进程持有了。

第 28 位为 `LW_FLAG_LOCKED`，用于等待队列的临界区保护。当进程无法获取到自己想要的锁模式时需要将自身加入等待队列中进行等待，此时便需要修改双向链表，是一个非进程安全的操作，因此需要锁保护。该锁使用 TAS 的方式加锁和释放，而前面的互斥/共享锁则使用 CAS 的方式实现。

第 29 位为 `LW_FLAG_RELEASE_OK`，用于限制锁释放进程对等待队列中的进程进行唤醒。当持有者释放锁时，如果有等待者需要被唤醒，PostgreSQL 会进入一段“批量唤醒等待者”的流程，这个过程是不受 `LW_FLAG_LOCKED` 队列锁保护的。可能考虑到唤醒动作（Semaphore）带有系统调用，很有可能收到当前系统运行时状态（内存、CPU）的影响，故 PostgreSQL 选择在不持有任何等待队列保护锁的情况下调用 `semop()` 增加信号量的值以唤醒等待进程，因此就有了 `LW_FLAG_RELEASE_OK` 标志位。唤醒方通过原子的设置该标志位为 0，表示已经有进程对该等待队列进行等待进程唤醒了，只有在被唤醒进程主动更新该标志位为 1 时，唤醒方才可以继续对该等待队列进行唤醒操作，防止同一个进程被唤醒两次而导致信号量的值错误。

第 30 位表示该锁是否有等待进程在等待队列中。

所有标志位定义如下代码所示：

```cpp
#define LW_FLAG_HAS_WAITERS			((uint32) 1 << 30)
#define LW_FLAG_RELEASE_OK			((uint32) 1 << 29)
#define LW_FLAG_LOCKED				((uint32) 1 << 28)

/* 排它锁与共享锁标志位，事实上 (1 << 25)-1 全部表示共享锁 */
#define LW_VAL_EXCLUSIVE			((uint32) 1 << 24)
#define LW_VAL_SHARED				1

/* state & LW_LOCK_MASK > 0 表示该锁被持有 */
#define LW_LOCK_MASK				((uint32) ((1 << 25)-1))
/* state & LW_SHARED_MASK > 0 表示该锁被持有并且是一个共享锁 */
#define LW_SHARED_MASK				((uint32) ((1 << 24)-1))
```

轻量锁的取锁入口为 `LWLockAcquire()`，其本质上是对 `state` 状态修改、等待队列修改和调用系统信号量函数进行等待和重试的封装。因此我们想来看对 `state` 状态中各个比特位的操作函数，最后再来进行整合。

#### 3.2 互斥锁与共享锁的获取

互斥锁与共享锁的获取主要操作 `LW_VAL_EXCLUSIVE` 和 `LW_VAL_SHARED`，因为是 bit 的实现，因此加锁操作可以直接进行加、减法。假设当前 `state` 的值为 `old_state`，根据当前想要获取的锁模式和 `old_state` 的值，我们可以有如下 4 种情况：

1. 获取锁模式为共享，且 `old_state & LW_VAL_EXCLUSIVE == 0`，即该锁没有以排它锁的方式被其它进程获取，那么我们就可以尝试将 `old_state += LW_VAL_SHARED` 并以 CAS 的方式更新内存值。
2. 获取锁模式为共享，且 `old_state & LW_VAL_EXCLUSIVE == 1`，即该锁以排它锁的方式被其它进程正在持有，直接返回。
3. 获取锁模式为排它，且 `old_state & LW_LOCK_MASK == 0`，即该锁没有被任何进程持有，那么我们就可以尝试将 `old_state += LW_VAL_EXCLUSIVE` 并以 CAS 的方式更新。
4. 获取锁模式为排它，且 `old_state & LW_LOCK_MASK != 0`，即该锁正在被其它进程持有，此时无论是共享锁还是排它锁我们都需等待，因此直接返回。

具体实现如下：

```cpp
static bool
LWLockAttemptLock(LWLock *lock, LWLockMode mode)
{
	uint32		old_state;

	Assert(mode == LW_EXCLUSIVE || mode == LW_SHARED);

	/* 通过 32bit 原子函数获取 lock->state 状态，
	 * 但其实一般机器 32 bit 整型读取本就是原子的 */
	old_state = pg_atomic_read_u32(&lock->state);

	/* 尝试加锁，直到得到确切的结果：未能成功加锁/成功加锁 */
	while (true)
	{
		uint32		desired_state;
		bool		lock_free;

		desired_state = old_state;

		if (mode == LW_EXCLUSIVE)
		{
			/* 获取独占锁时检查 LW_LOCK_MASK，只有该锁未被任何人持有时才可获取 */
			lock_free = (old_state & LW_LOCK_MASK) == 0;
			if (lock_free)
				/* 如果空闲，将期望状态累加 LW_VAL_EXCLUSIVE，表示“打算将锁置为独占持有”。*/
				desired_state += LW_VAL_EXCLUSIVE;
		}
		else
		{
			/* 获取共享锁时检查 LW_VAL_EXCLUSIVE，只要没有独占，就可以共享 */
			lock_free = (old_state & LW_VAL_EXCLUSIVE) == 0;
			if (lock_free)
				/* 如果可用，将期望状态累加 LW_VAL_SHARED，表示“打算将锁置为共享持有”。*/
				desired_state += LW_VAL_SHARED;
		}

		/*
		 * CAS 操作，尝试取锁。比较 lock->state 是否仍然等于我们之前读到的 old_state
		 *  1) 如果相等，就把它更新成 desired_state，并返回 true 表示 CAS 成功；
		 *  2) 如果不相等，则更新 old_state 为锁的新状态，并继续循环。
		 */
		if (pg_atomic_compare_exchange_u32(&lock->state,
										   &old_state, desired_state))
		{
			/* 如果之前判断锁是空闲的，此时我们已顺利占有锁 → 返回 false 表示成功。*/
			if (lock_free)
			{
				if (mode == LW_EXCLUSIVE)
					lock->owner = MyProc;

				return false;
			}
			/* 否则说明锁并不空闲，但我们做了一次无意义的 CAS → 返回 true 表示锁已被别人持有。*/
			else
				return true;
		}
	}
	pg_unreachable();
}
```

当 `LWLockAttemptLock()` 返回 true 时，并不表示锁被获取，而是代表锁没有被获取，调用进程必须将自身加入等待队列中进行等待，直到被其它进程唤醒。而在加入等待队列时，必须先获取到等待队列锁，亦即 `LW_FLAG_LOCKED`。

#### 3.3 获取与释放等待队列锁

队列锁的获取是一个标准的 Set-And-Test 自旋锁模式，通过原子的 CAS 操作来完成队列锁的获取。我们已经知道了 `pg_atomic_fetch_or_u32()` 会做两件事：
1. 将 `LW_FLAG_LOCKED` bit flag 或操作到 `lock->state` 上
2. 返回该操作前的状态

即无论当前是什么状态，队列锁有没有被其它进程获取，都将修改 `lock->state` 的 `LW_FLAG_LOCKED`，其正确性讨论如下：
1. 若 `LW_FLAG_LOCKED` 本来就是 0，那么该操作相当于加锁，并且会返回操作前的旧值。那么此时 `old_state & LW_FLAG_LOCKED` 的值为 0，我们也就知道没有其它进程持有队列锁，直接返回即可。
2. 若 `LW_FLAG_LOCKED` 的原有值为 1，那么该操作相当于没有修改任何数据，同时返回的 `old_state` 中 `LW_FLAG_LOCKED` 为 1，我们就知道队列锁正在被其它进程锁持有，后续应进行等待（自旋）操作。

因此，我们并不需要先探测是否有其它进程持有队列锁，因为“探测-检测-设置”这个过程也不是原子，那么我们干脆使用 CAS 提供的原子语义对状态直接进行修改。

```cpp
static void
LWLockWaitListLock(LWLock *lock)
{
	uint32		old_state;

	while (true)
	{
		/* Test-and-set，直接给 lock->state 干上 LW_FLAG_LOCKED */
		old_state = pg_atomic_fetch_or_u32(&lock->state, LW_FLAG_LOCKED);

		/* 如果旧值的 LW_FLAG_LOCKED 为 0，说明队列锁未被其它进程持有，直接返回 */
		if (!(old_state & LW_FLAG_LOCKED))
			break;				/* got lock */

		/* 队列锁被其它进程持有，自旋等待。该锁被持有的时间理应相当短 */
		{
			SpinDelayStatus delayStatus;
			init_local_spin_delay(&delayStatus);

			while (old_state & LW_FLAG_LOCKED)
			{
				/* 自旋，也就是调用 sleep() 让出 CPU */
				perform_spin_delay(&delayStatus);
				/* 睡醒了再获取 LW_FLAG_LOCKED 的值，判断是否为 1 */
				old_state = pg_atomic_read_u32(&lock->state);
			}

			finish_spin_delay(&delayStatus);
		}

		/* 此时 LW_FLAG_LOCKED 的值为 0，但不能保证队列锁不被其它进程再次获取，故继续循环 */
	}
}
```

队列锁的释放由 `LWLockWaitListUnlock()` 实现，只需要原子更新 `LW_FLAG_LOCKED` 的值为 0 即可：

```cpp
static void
LWLockWaitListUnlock(LWLock *lock)
{
	uint32		old_state;

	old_state = pg_atomic_fetch_and_u32(&lock->state, ~LW_FLAG_LOCKED);

	/* 防御性检查，杜绝对未加锁的队列锁进行释放 */
	Assert(old_state & LW_FLAG_LOCKED);
}
```

#### 3.4 加入和移出等待队列

加入等待队列是一个非常简单的操作，只需要在获取到队列锁后将当前进程对象 `PGPROC` 添加至等待队列末尾即可，这表明轻量锁是一个“先入先得”的队列锁。

```cpp
static void
LWLockQueueSelf(LWLock *lock, LWLockMode mode)
{
	/* 边界情况检查 */
	if (MyProc == NULL)
		elog(PANIC, "cannot wait without a PGPROC structure");
	if (MyProc->lwWaiting != LW_WS_NOT_WAITING)
		elog(PANIC, "queueing for lock while waiting on another one");

	/* 获取 Lock->state 中的 LW_FLAG_LOCKED 队列锁 */
	LWLockWaitListLock(lock);

	/* 设置 LW_FLAG_HAS_WAITERS，表明当前锁存在等待者 */
	pg_atomic_fetch_or_u32(&lock->state, LW_FLAG_HAS_WAITERS);

	MyProc->lwWaiting = LW_WS_WAITING;
	MyProc->lwWaitMode = mode;

	/* LW_WAIT_UNTIL_FREE 目前用于 WAL 中，请参考 WAL 章节部分 */
	if (mode == LW_WAIT_UNTIL_FREE)
		proclist_push_head(&lock->waiters, MyProcNumber, lwWaitLink);
	else
		proclist_push_tail(&lock->waiters, MyProcNumber, lwWaitLink);

	/* 释放等待队列锁 */
	LWLockWaitListUnlock(lock);
}
```

当将自身 `PGPROC` 加入等待队列时，更新进程 `lwWaiting` 的状态为 `LW_WS_WAITING`，这个值在进程间的协同工作中起到非常重要的作用。当进程加入等待队列之前，将该状态修改为 `LW_WS_WAITING` 并等待在信号量上进行睡眠。当其它进程唤醒该进程时，修改该状态为 `LW_WS_NOT_WAITING`。被唤醒进程被唤醒时检查该值是否为 `LW_WS_NOT_WAITING`，如果是则表明是被正常唤醒的，因此尝试取锁。若不是，那么说明该进程被操作系统“虚假”唤醒了，可能是因为系统其它中断所致，因此需要继续睡眠：

```cpp
/* 进程可能因为系统信号、中断的原因被唤醒，此时并不代表当前进程获取到锁，因此必须循环检查 */
for (;;)
{
	/* 在信号量进行等待，直到其它进程调用 PGSemaphoreUnlock(waiter->sem) */
	PGSemaphoreLock(proc->sem);
	/* 在 LWLockWakeup() 中，被唤醒的进程 lwWaiting 会被设置为 LW_WS_NOT_WAITING */
	if (proc->lwWaiting == LW_WS_NOT_WAITING)
		break;
	extraWaits++;
}
```

移出队列的逻辑要比入队的逻辑更加复杂，其原因在于这其中可能会存在多种竟态的可能性。对于入队来说，只需要简单的获取队列锁然后把自己添加至队列中即可。但在出队时，由于前面已经将自身进程加入过等待队列，那么就存在着其它进程尝试唤醒过当前进程的可能。那么此时由于当前进程并未实际的进入睡眠（即没有调用 `PGSemaphoreLock()` 在信号量上等待），那么此时就需要手动修正这次“无用唤醒”。

![alt text](image-1.png)

```cpp
static void
LWLockDequeueSelf(LWLock *lock)
{
	bool		on_waitlist;

	/* 首先获取等待队列锁，防止其它进程同时操作 */
	LWLockWaitListLock(lock);

	/* 在 LWLockAcquire() 将 proc 加入等待队列后，在下次尝试取锁时可能已经被其它进程唤醒，
	 * 此时 lwWaiting 将会被唤醒进程修改为 LW_WS_NOT_WAITING，故需要进一步判断 */
	on_waitlist = MyProc->lwWaiting == LW_WS_WAITING;
	if (on_waitlist)
		/* 如果仍在等待队列中，移除之 */
		proclist_delete(&lock->waiters, MyProcNumber, lwWaitLink);

	/* 当队列为空时，修改 LW_FLAG_HAS_WAITERS 为 0 */
	if (proclist_is_empty(&lock->waiters) &&
		(pg_atomic_read_u32(&lock->state) & LW_FLAG_HAS_WAITERS) != 0)
	{
		pg_atomic_fetch_and_u32(&lock->state, ~LW_FLAG_HAS_WAITERS);
	}

	LWLockWaitListUnlock(lock);

	if (on_waitlist)
		MyProc->lwWaiting = LW_WS_NOT_WAITING;
	else
	{
		/* 此时，我们并不在等待队列中，这就说明有其它进程尝试唤醒过当前进程，需要修正信号量 */
		int			extraWaits = 0;

		/* 被唤醒者修改 LW_FLAG_RELEASE_OK 为 true，使得唤醒方可继续进行其工作 */
		pg_atomic_fetch_or_u32(&lock->state, LW_FLAG_RELEASE_OK);

		/* 理论上来讲 PGSemaphoreLock() 应该是立即返回，循环结束后修正信号量 */
		for (;;)
		{
			PGSemaphoreLock(MyProc->sem);
			if (MyProc->lwWaiting == LW_WS_NOT_WAITING)
				break;
			extraWaits++;
		}
		while (extraWaits-- > 0)
			PGSemaphoreUnlock(MyProc->sem);
	}
}
```

#### 3.5 获取轻量锁

`LWLockAcquire()` 的流程相对简单，没有什么太复杂的地方。基本流程就是尝试取锁，若取锁成功则将其加入 `held_lwlocks` 数组中。若尝试失败则将自身加入等待队列中，使用信号量的方式等待其它进程在释放锁时唤醒，期间注意处理虚假唤醒问题。

```cpp
bool
LWLockAcquire(LWLock *lock, LWLockMode mode)
{
	PGPROC	   *proc = MyProc;
	bool		result = true;
	int			extraWaits = 0;

	/* 暂停处理任何信号/中断 */
	HOLD_INTERRUPTS();

	for (;;)
	{
		bool		mustwait;

		/* 非阻塞的加锁，若 mustwait 返回 false，则取锁成功，此时 lock->state 已经被设置 */
		mustwait = LWLockAttemptLock(lock, mode);

		/* 取锁成功，执行后续逻辑 */
		if (!mustwait)
			break;

		/* 将自身加入锁等待队列中 */
		LWLockQueueSelf(lock, mode);

		/* 睡眠前再次尝试加锁，此时锁可能已经被释放 */
		mustwait = LWLockAttemptLock(lock, mode);

		/* 取锁成功，将自身从等待队列中移除后执行取锁成功逻辑 */
		if (!mustwait)
		{
			LWLockDequeueSelf(lock);
			break;
		}

		/* 进程可能因为系统信号、中断的原因被唤醒，此时并不代表当前进程获取到锁，因此必须循环检查 */
		for (;;)
		{
			/* 在信号量进行等待，直到其它进程调用 PGSemaphoreUnlock(waiter->sem) */
			PGSemaphoreLock(proc->sem);
			/* 在 LWLockWakeup() 中，被唤醒的进程 lwWaiting 会被设置为 LW_WS_NOT_WAITING */
			if (proc->lwWaiting == LW_WS_NOT_WAITING)
				break;
			extraWaits++;
		}

		/* 进程被正常唤醒，更新 LW_FLAG_RELEASE_OK 的值为 1，允许其它进程继续唤醒 */
		pg_atomic_fetch_or_u32(&lock->state, LW_FLAG_RELEASE_OK);

		/* Now loop back and try to acquire lock again. */
		result = false;
	}

	/* Add lock to list of locks held by this backend */
	held_lwlocks[num_held_lwlocks].lock = lock;
	held_lwlocks[num_held_lwlocks++].mode = mode;

	/* 进程可能因为系统信号、中断的原因被唤醒从而多次调用 PGSemaphoreLock(),
	 * 因此需要手动修正底层的 Semaphore，即调用 PGSemaphoreUnlock()。*/
	while (extraWaits-- > 0)
		PGSemaphoreUnlock(proc->sem);

	return result;
}
```

#### 3.6 锁的释放

锁的释放相较于锁的获取要稍显复杂，原因在于在释放锁时，需要根据一系列的条件判断是否需要唤醒等待在该锁上的进程。一般来说，在下列情况均满足的条件下需要唤醒等待进程：
1. 等待队列不为空（`LW_FLAG_HAS_WAITERS`）并且没有其它进程对同一个等待队列进行唤醒操作（`LW_FLAG_RELEASE_OK`）
2. 释放的锁为排它锁，或者是释放最后一把共享锁，即 `lock->state & LW_LOCK_MASK == 0`

`LWLock->state` 的设计使得我们在释放锁时可直接对其进行减法操作，当 `state & LW_LOCK_MASK` 的值为 0 时，即表示该锁未被任何人持有，也就表示被释放的锁要么是排它锁，要么是最后一把共享锁。


```cpp
void
LWLockRelease(LWLock *lock)
{
	LWLockMode	mode;

	/* 此处变量名应为 newstate，表示更新后的状态 */
	uint32		oldstate;
	bool		check_waiters;
	int			i;

	/* 从 held_lwlocks 数组中寻找该 Lock 移除，需处理数组空洞 */
	for (i = num_held_lwlocks; --i >= 0;)
		if (lock == held_lwlocks[i].lock)
			break;

	mode = held_lwlocks[i].mode;
	num_held_lwlocks--;
	/* 处理数组空洞，将后续元素向前移动一个单位 */
	for (; i < num_held_lwlocks; i++)
		held_lwlocks[i] = held_lwlocks[i + 1];

	/* lock->state 的设计使得我们在释放锁时直接做减法即可。*/
	if (mode == LW_EXCLUSIVE)
		oldstate = pg_atomic_sub_fetch_u32(&lock->state, LW_VAL_EXCLUSIVE);
	else
		oldstate = pg_atomic_sub_fetch_u32(&lock->state, LW_VAL_SHARED);

	/* 可以唤醒等待进程的情况，见上方讨论 */
	if ((oldstate & (LW_FLAG_HAS_WAITERS | LW_FLAG_RELEASE_OK)) ==
		(LW_FLAG_HAS_WAITERS | LW_FLAG_RELEASE_OK) &&
		(oldstate & LW_LOCK_MASK) == 0)
		check_waiters = true;
	else
		check_waiters = false;

	/* 唤醒等待进程 */
	if (check_waiters)
		LWLockWakeup(lock);

	/* 恢复 PostgreSQL 信号处理机制 */
	RESUME_INTERRUPTS();
}
```


#### 3.7 唤醒等待队列进程

唤醒等待队列中的进程大体被分为 3 个步骤：

1. 首先获取等待队列锁，防止其它进程继续向其中写入进程或唤醒进程。紧接着遍历等待队列，根据条件将能够唤醒的进程添加至临时链表中。当等待进程的期望锁模式为 `LW_EXCLUSIVE` 时，只能唤醒这一个进程。若等待进程的期望锁模式为 `LW_SHARED` 时，便可以唤醒多个想要获取共享锁的进程。
2. 释放队列锁，以及根据是否有进程要被唤醒来更新 `LW_FLAG_RELEASE_OK` 的值，以阻止释放等待队列锁后其余进程继续对同一个进程继续进行唤醒。
3. 对临时链表中的进程依次调用 `PGSemaphoreUnlock()` 进行唤醒，但在此之前先更新 `lwWaiting` 的值为 `LW_WS_NOT_WAITING`，这样一来被唤醒进程就知道此次唤醒并不是操作系统的一次虚假唤醒，因此再次尝试获取期望的锁模式。同时为了避免 CPU 的指令重排，必须让更新 `lwWaiting` 的值在调用 `PGSemaphoreUnlock()` 之前发生，我们还需要在这之前添加写屏障。

```cpp
static void
LWLockWakeup(LWLock *lock)
{
	bool		new_release_ok;
	bool		wokeup_somebody = false;
	proclist_head wakeup;
	proclist_mutable_iter iter;

	proclist_init(&wakeup);

	new_release_ok = true;

	/* 由于要对等待队列做修改，因此首先获取队列锁 */
	LWLockWaitListLock(lock);

	proclist_foreach_modify(iter, &lock->waiters, lwWaitLink)
	{
		PGPROC	   *waiter = GetPGProcByNumber(iter.cur);

		/* 当唤醒过一个进程，并且当前等待者的锁模式为排它锁时，无法唤醒该进程 */
		if (wokeup_somebody && waiter->lwWaitMode == LW_EXCLUSIVE)
			continue;

		/* 从等待队列中移除，添加至本地临时链表 wakeup 中 */
		proclist_delete(&lock->waiters, iter.cur, lwWaitLink);
		proclist_push_tail(&wakeup, iter.cur, lwWaitLink);

		/* 在 LW_WAIT_UNTIL_FREE 模式下，等待者不需要获取锁 */
		if (waiter->lwWaitMode != LW_WAIT_UNTIL_FREE)
		{
			/* 设置 new_release_ok = false, 表示暂时不允许其它进程唤醒该锁中的等待进程 */
			new_release_ok = false;
			wokeup_somebody = true;
		}

		Assert(waiter->lwWaiting == LW_WS_WAITING);
		waiter->lwWaiting = LW_WS_PENDING_WAKEUP;

		/* 由于排它锁和其它任何类型的锁都互斥，因此当等待进程的锁模式为 LW_EXCLUSIVE 时，
		 * 只需要唤醒这一个等待进程即可。 */
		if (waiter->lwWaitMode == LW_EXCLUSIVE)
			break;
	}

	{
		uint32		old_state;
		uint32		desired_state;

		/* 由于在 LWLockRelease() 中我们已经释放了该锁，因此其它进程可对该锁进行任意的状态修改，
		 * 因此如果我们想要更新 */
		old_state = pg_atomic_read_u32(&lock->state);
		while (true)
		{
			desired_state = old_state;

			/* 仅在 LW_WAIT_UNTIL_FREE 模式下，允许其它进程继续唤醒等待进程 */
			if (new_release_ok)
				desired_state |= LW_FLAG_RELEASE_OK;
			/* 否则，阻止其它进程唤醒，直到被唤醒者更新了 LW_FLAG_RELEASE_OK */
			else
				desired_state &= ~LW_FLAG_RELEASE_OK;

			if (proclist_is_empty(&wakeup))
				desired_state &= ~LW_FLAG_HAS_WAITERS;

			/* 释放队列锁 */
			desired_state &= ~LW_FLAG_LOCKED;

			if (pg_atomic_compare_exchange_u32(&lock->state, &old_state,
											   desired_state))
				break;
		}
	}

	/* 遍历 wakeup 链表依次唤醒 */
	proclist_foreach_modify(iter, &wakeup, lwWaitLink)
	{
		PGPROC	   *waiter = GetPGProcByNumber(iter.cur);

		proclist_delete(&wakeup, iter.cur, lwWaitLink);

		/* 在唤醒进程前必须将其状态更新为 LW_WS_NOT_WAITING，故添加写屏障 */
		pg_write_barrier();
		waiter->lwWaiting = LW_WS_NOT_WAITING;
		PGSemaphoreUnlock(waiter->sem);
	}
}
```

等待者被唤醒后，并不是直接拿到了所需要的锁，而是通过重试的方式再次尝试取锁。当进程 A 在释放锁 a 时准备唤醒进程 C，如果不对其它唤醒进程做限制的话，就有可能出现进程 B 释放锁 a 时唤醒了进程 D，此时就会出现多个进程同时抢锁的情况。更进一步的，如果等待者较多，还会引起“惊群”问题。因此，通过设置 `new_release_ok` 的值来决定是否对 `LW_FLAG_RELEASE_OK` 进行设置，从而最终决定锁释放者是否能够唤醒等待队列中的进程。