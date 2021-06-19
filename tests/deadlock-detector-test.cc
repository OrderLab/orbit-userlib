/*
 * Mock deadlock checker using orbit abstraction
 *
 * This can be used to simulate performance improvements when using orbit.
 * Users can run this program w/ and w/o `use_orbit` to compare execution
 * performance between synchronous version and async orbit version.
 */

#include "orbit.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <unistd.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

using namespace std::chrono;

typedef uint64_t ib_uint64_t;
typedef unsigned long int	ulint;
static const ulint MAX_STACK_SIZE = 4096;

#define NUM_TRANSACTIONS 200
#define NUM_LOCKS 5

/* Each Transaction contains a vector of locks */
struct trx_t {
	// std::vector<struct lock_t *> locks;
	struct lock_t **locks;
	int modifiable_field;
};

struct lock_t {
	struct trx_t *trx;
};

/* Global vector of Transactions */
// struct trx_t **trxs;

class DeadlockChecker {
public:
	DeadlockChecker(
		trx_t **trxs_,
		const trx_t*	trx,
		const lock_t*	wait_lock,
		ib_uint64_t	mark_start)
		:
		trxs(trxs_),
		m_cost(),
		m_start(trx),
		m_too_deep(),
		m_wait_lock(wait_lock),
		m_mark_start(mark_start),
		m_n_elems()
	{
	}

	const trx_t* search();

private:
	const lock_t* get_next_lock(const lock_t* lock, ulint heap_no);

	const lock_t* get_first_lock(ulint* heap_no);

	/** DFS state information, used during deadlock checking. */
	struct state_t {
		const lock_t*	m_lock;		/*!< Current lock */
		const lock_t*	m_wait_lock;	/*!< Waiting for lock */
		ulint		m_heap_no;	/*!< heap number if rec lock */
	};

	/** Used in deadlock tracking. Protected by lock_sys->mutex. */
	static ib_uint64_t	s_lock_mark_counter;

	struct trx_t		**trxs;

	/** Calculation steps thus far. It is the count of the nodes visited. */
	ulint			m_cost;

	/** Joining transaction that is requesting a lock in an
	  incompatible mode */
	const trx_t*		m_start;

	/** TRUE if search was too deep and was aborted */
	bool			m_too_deep;

	/** Lock that trx wants */
	const lock_t*		m_wait_lock;

	/**  Value of lock_mark_count at the start of the deadlock check. */
	ib_uint64_t		m_mark_start;

	/** Number of states pushed onto the stack */
	size_t			m_n_elems;

	/** This is to avoid malloc/free calls. */
	static state_t		s_states[MAX_STACK_SIZE];

	void pop(const lock_t*& lock, ulint& heap_no) {
		return;
	}

	bool push(const lock_t*	lock, ulint heap_no) {
		return true;
	}

	/* Returns true only 10% of the time */
	bool lock_has_to_wait(const lock_t* lock1, const lock_t* lock2) {
		int secret = rand() % 10;
		if (secret == 0)
			return true;
		return false;
	}

	void check_trx_state(trx_t* trx) {
		return;
	}

	bool is_too_deep() {
		return false;
	}
};

/* Sleeps for 1ms and returns a random lock from a random trx */
const lock_t*
DeadlockChecker::get_next_lock(const lock_t* lock, ulint heap_no) {
	return get_first_lock(&heap_no);
}

/* Sleeps for 1ms and returns a random lock from a random trx */
const lock_t* __attribute__((optimize("O0")))
DeadlockChecker::get_first_lock(ulint* heap_no) {
	//std::this_thread::sleep_for (std::chrono::nanoseconds(1));
	/* This can yield ~30% overhead on the testing machine. */
	for (int i = 0; i < 2000; ++i)
		;
	trx_t* rand_trx = trxs[rand() % NUM_TRANSACTIONS];
	return rand_trx->locks[rand() % NUM_LOCKS];
}

const trx_t*
DeadlockChecker::search()
{
	check_trx_state(m_wait_lock->trx);

	/* Look at the locks ahead of wait_lock in the lock queue. */
	ulint		heap_no;
	const lock_t* lock = get_first_lock(&heap_no);

	for (;;)  {

		while (m_n_elems > 0 && lock == NULL) {
			pop(lock, heap_no);
			lock = get_next_lock(lock, heap_no);
		}

		if (lock == NULL)
			break;

		else if (lock == m_wait_lock) {
			lock = NULL;

		} else if (!lock_has_to_wait(m_wait_lock, lock)) {
			lock = get_next_lock(lock, heap_no);

		} else if (lock->trx == m_start) {
			return m_start;

		} else if (is_too_deep()) {
			m_too_deep = true;
			return(m_start);
		} else {
			lock = get_next_lock(lock, heap_no);
		}
	}

	return(0);
}

enum update_type { UT_NONE, UT_SEND, UT_COMMIT, UT_SENDV, };

struct checker_args {
	lock_t *lock;
	trx_t *trx;
	trx_t **trxs;
	update_type ut;
};

struct orbit_scratch scratch;

/* Original definition:
 * const trx_t* check_and_resolve(const lock_t* lock, trx_t* trx);
 */
unsigned long check_and_resolve(void *args_) {
	const trx_t *victim_trx;
	checker_args *args = (checker_args*)args_;

	/* This can yield ~30% overhead on the testing machine. */
	srand(223);
	/* Try and resolve as many deadlocks as possible. */
	do {
		DeadlockChecker checker(args->trxs, args->trx, args->lock, 0);
		victim_trx = checker.search();
	} while (victim_trx != NULL);

	if (likely(args->ut == UT_NONE)) {
		/* Do nothing */
	} else if (args->ut == UT_SEND) {
		char buffer[sizeof(struct orbit_update) + sizeof(int)];
		struct orbit_update *update = (struct orbit_update*)buffer;

		update->ptr = NULL;
		update->length = sizeof(int);
		*(unsigned int *)update->data = 0xdeadbeef;
		orbit_send(update);
	} else if (args->ut == UT_COMMIT) {
		args->trx->modifiable_field = 100;
		args->trxs[20]->modifiable_field = 100;

		std::cout << "In orbit, checking finished\n";

		orbit_commit();
	} else if (args->ut == UT_SENDV) {
		/* Mock modification */
		args->trx->modifiable_field = 100;
		orbit_scratch_push_update(&scratch, &args->trx->modifiable_field, 4);

		args->trxs[20]->modifiable_field = 100;
		orbit_scratch_push_update(&scratch, &args->trxs[20]->modifiable_field, 4);

		std::cout << "In orbit, checking finished\n";

		int ret = orbit_sendv(&scratch);
		int err = ret == -1 ? errno : 0;
		std::cout << "In orbit, sendv returned " << ret
			<< " errno=" << strerror(err) << std::endl;
	} else {
		std::cerr << "Invalid update type: " << args->ut << std::endl;
	}

	return (unsigned long)victim_trx;
}

struct trx_t **initialize_data(struct orbit_pool *pool) {
	struct trx_t **trxs = (struct trx_t**)orbit_pool_alloc(pool,
			sizeof(*trxs) * NUM_TRANSACTIONS);

	/* Create NUM_TRANSACTIONS Transactions each holding NUM_LOCKS locks */
	for (int i = 0; i < NUM_TRANSACTIONS; ++i) {
		struct trx_t* trx = (struct trx_t*) orbit_pool_alloc(pool, sizeof(*trx));
		trx->locks = (struct lock_t**)orbit_pool_alloc(pool,
				sizeof(*trx->locks) * NUM_LOCKS);
		trx->modifiable_field = 0;

		for (int j = 0; j < NUM_LOCKS; ++j) {
			struct lock_t* lock = (struct lock_t*)
				orbit_pool_alloc(pool, sizeof(*lock));
			lock->trx = trx;
			trx->locks[j] = lock;
		}
		trxs[i] = trx;
	}

	return trxs;
}

/* Simulates work by mySQL by sleeping */
void perform_work() {
	std::this_thread::sleep_for (std::chrono::milliseconds(1));
}

int run(update_type ut, int N, bool do_check, bool use_orbit, int call_rate) {
	assert(ut != UT_NONE);

	orbit_scratch_create(&scratch, 4096);
	struct orbit_pool *pool = orbit_pool_create(4096 * 16);

	struct trx_t **trxs = initialize_data(pool);

	struct orbit_module *m = orbit_create("test_module", check_and_resolve);

	struct checker_args args = { trxs[0]->locks[0], trxs[0], trxs, UT_NONE, };


	struct orbit_task task;
	union orbit_result result;
	struct orbit_update *update = (struct orbit_update*)
		malloc(sizeof(struct orbit_update) + ORBIT_BUFFER_MAX);

	auto t1 = high_resolution_clock::now();

	for(int count = 0; count < N; ++count) {

		perform_work();

		// int secret = rand() % 5;
		/* Calls the deadlock detector 1/5 times on average */
		if (!(do_check && (count + 1) % call_rate == 0))
			continue;

		// args.trx = trxs[rand() % NUM_TRANSACTIONS];
		// args.lock = args.trx->locks[rand() % NUM_LOCKS];
		if (use_orbit) {
			int ret;
			bool last = (count == N - 1);

			if (last) {
				args.ut = ut;
				ret = orbit_call_async(m, 0, 1, &pool,
						&args, sizeof(args), &task);
			} else {
				ret = orbit_call_async(m, ORBIT_NORETVAL,
					1, &pool, &args, sizeof(args), NULL);
			}

			if (unlikely(ret != 0)) {
				std::cout << "orbit async call failed "
					<< ret << std::endl;
				return 1;
			}
		} else {
			// orbit_call(m, pool, args);
			check_and_resolve(&args);
		}

		/* Print progress */
		if (unlikely(1 && (count + 1) % 1000 == 0)) {
			auto t2 = high_resolution_clock::now();
			auto duration = duration_cast<nanoseconds>(t2 - t1).count();
			std::cout << count << " " << duration << std::endl;
		}
	}

	auto t2 = high_resolution_clock::now();

	int ret;
	if (do_check && use_orbit) {
		switch (ut) {
		case UT_SEND:
			ret = orbit_recv(&task, update); break;
		case UT_COMMIT:
		case UT_SENDV:
			ret = orbit_recvv(&result, &task); break;
			// FIXME: recvv again?
		default:
			std::cerr << "Unknown ut: " << ut << std::endl;
			return 1;
		}
	}

	auto t3 = high_resolution_clock::now();

	if (do_check && use_orbit) {
		switch (ut) {
		case UT_SEND:
		case UT_COMMIT:
			if (ret != 0) {
				std::cout << "ob recv returned " << ret << std::endl;
				return 1;
			}
			break;
		case UT_SENDV:
			if (ret != 1) {
				std::cout << "ob recv returned " << ret << std::endl;
				return 1;
			}
			orbit_apply(&result.scratch, false);
			break;
		default:
			std::cerr << "Unknown ut: " << ut << std::endl;
			return 1;
		}

		for (int i = 0; i < NUM_TRANSACTIONS; ++i) {
			if (trxs[i]->modifiable_field == 100) {
				std::cout << "\tFound trx " << i
					<< " modified to 100.\n";
			}
		}
	}

	auto duration1 = duration_cast<nanoseconds>(t2 - t1).count();
	auto duration2 = duration_cast<nanoseconds>(t3 - t1).count();

	std::cout << "Run " << N << " times takes " << duration1 << " ns, "
		<< ((double)N/duration1*1000000000) << " ops\n";
	std::cout << "Run " << N << " times takes " << duration2 << " ns, "
		<< ((double)N/duration2*1000000000) << " ops\n";

	return 0;
}


int main(int argc, char *argv[]) {
	int N, do_check, use_orbit, call_rate;
	update_type ut = UT_NONE;

	if (argc != 6
		|| sscanf(argv[2], "%d", &N) != 1
		|| sscanf(argv[3], "%d", &do_check) != 1
		|| sscanf(argv[4], "%d", &use_orbit) != 1
		|| sscanf(argv[5], "%d", &call_rate) != 1
	)
		goto usage;

	if (N < 1 || call_rate < 1)
		goto usage;

	if (!strcmp(argv[1], "async"))
		ut = UT_SEND;
	else if (!strcmp(argv[1], "commit"))
		ut = UT_COMMIT;
	else if (!strcmp(argv[1], "sendv"))
		ut = UT_SENDV;

	if (ut != UT_NONE) {
		std::cout << "Run with N=" << N << " do_check=" << !!do_check
			<< " use_orbit=" << !!use_orbit
			<< " call_rate=" << call_rate << std::endl;
		return run(ut, N, !!do_check, !!use_orbit, call_rate);
	}

usage:
	std::cerr << "Usage: " << argv[0] << " <async|commit|sendv>"
		" <iterations> <do_check> <use_orbit> <call_rate>\n"
		"Note: `iterations` and `call_rate` must be larger than 0\n"
		"Tested commands:\n"
		"\t" << argv[0] << " async 50000 1 <1|0> 5\n"
		"\t" << argv[0] << " <commit|sendv> 1 1 1 1\n";
	return 1;
}
