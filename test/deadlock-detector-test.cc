#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <chrono>

typedef uint64_t ib_uint64_t;
typedef unsigned long int	ulint;
static const ulint MAX_STACK_SIZE = 4096;

#define NUM_TRANSACTIONS 200
#define NUM_LOCKS 5

/* Each Transaction contains a vector of locks */
struct trx_t {
    std::vector<struct lock_t *> locks;
};

struct lock_t {
    struct trx_t *trx;
};

/* Global vector of Transactions */
std::vector<struct trx_t *> trxs;

class DeadlockChecker {
public:
	DeadlockChecker(
		const trx_t*	trx,
		const lock_t*	wait_lock,
		ib_uint64_t	mark_start)
		:
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

    void 
    pop(const lock_t*& lock, ulint& heap_no) {
		return;
	}

    bool 
    push(const lock_t*	lock, ulint heap_no) {
		return true;
	}

    /* Returns true only 10% of the time */
    bool 
    lock_has_to_wait(const lock_t*	lock1, const lock_t* lock2) {
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
    std::this_thread::sleep_for (std::chrono::milliseconds(1));
    trx_t* rand_trx = trxs.at(rand() % NUM_TRANSACTIONS);
    return rand_trx->locks.at(rand() % NUM_LOCKS);
}

/* Sleeps for 1ms and returns a random lock from a random trx */
const lock_t*
DeadlockChecker::get_first_lock(ulint* heap_no) {
    std::this_thread::sleep_for (std::chrono::milliseconds(1));
    trx_t* rand_trx = trxs.at(rand() % NUM_TRANSACTIONS);
    return rand_trx->locks.at(rand() % NUM_LOCKS);
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


const trx_t*
check_and_resolve(const lock_t* lock, trx_t* trx) {
	const trx_t*	victim_trx;

	/* Try and resolve as many deadlocks as possible. */
	do {
        DeadlockChecker	checker(trx, lock, 0);
        victim_trx = checker.search();
	} while (victim_trx != NULL);

	return(victim_trx);
}

void initialize_data() {

    /* Create NUM_TRANSACTIONS Transactions each holding NUM_LOCKS locks */
    for (int i = 0; i < NUM_TRANSACTIONS; i++) {
        struct trx_t* trx = new trx_t();

        for (int j = 0; j < NUM_LOCKS; j++) {
            struct lock_t* lock = new lock_t();
            lock->trx = trx;
            trx->locks.push_back(lock);
        }
        trxs.push_back(trx);
    }
}

/* Simulates work by mySQL by sleeping */
void perform_work() {
    std::this_thread::sleep_for (std::chrono::seconds(1));
}

int main() {   
    initialize_data();

    /* Randomly start with the first trx and lock */
    trx_t* trx = trxs.at(0);
    lock_t* lock = trx->locks.at(0);
    int counter = 0;
    for(;;) {

        perform_work();
        counter += 1;

        int secret = rand() % 5;

        /* Calls the deadlock detector 1/5 times on average */
        if (secret == 0) {
            auto t1 = std::chrono::high_resolution_clock::now();
            check_and_resolve(trx->locks.at(0), trx);
            auto t2 = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( t2 - t1 ).count();
            std::cout << "Deadlock Detector called after " << counter << " seconds, ran for " << duration << " milliseconds" << std::endl;
            counter = 0;
        }
    }
}