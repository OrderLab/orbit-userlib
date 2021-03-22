#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <pthread.h>

#define UNIV_LINUX 1
#define HAVE_CLOCK_GETTIME 1

// extern int holder_signal;
// struct lock_sys_t;
// extern lock_sys_t * lock_sys;

#define ut_a assert
#define ut_ad(x)
#define ut_d(x)
#define UNIV_NOTHROW

typedef unsigned long ulint;
typedef ulint lock_word_t;

#define TAS(l, n)			os_atomic_test_and_set((l), (n))
#define CAS(l, o, n)		os_atomic_val_compare_and_swap((l), (o), (n))
# define os_rmb	__atomic_thread_fence(__ATOMIC_ACQUIRE)
# define os_wmb	__atomic_thread_fence(__ATOMIC_RELEASE)

#define os_thread_yield sched_yield

inline
lock_word_t
os_atomic_test_and_set(
	volatile lock_word_t*	ptr,
	lock_word_t		new_val)
{
	lock_word_t	ret;

	/* Silence a compiler warning about unused ptr. */
	(void) ptr;

#if defined(__powerpc__) || defined(__aarch64__)
	__atomic_exchange(ptr, &new_val,  &ret, __ATOMIC_SEQ_CST);
#else
	__atomic_exchange(ptr, &new_val,  &ret, __ATOMIC_RELEASE);
#endif

	return(ret);
}

ulint ut_delay(ulint delay)	/*!< in: delay in microseconds on 100 MHz Pentium */
{
	ulint	i, j;

	j = 0;

	for (i = 0; i < delay * 50; i++) {
		j += i;
		__asm__ __volatile__ ("pause"); // x86
	}

	return(j);
}



typedef pthread_mutex_t sys_mutex_t;

struct OSMutex {

	/** Constructor */
	OSMutex()
		UNIV_NOTHROW
	{
		ut_d(m_freed = true);
	}

	/** Create the mutex by calling the system functions. */
	void init()
		UNIV_NOTHROW
	{
		ut_ad(m_freed);

		int	ret = pthread_mutex_init(&m_mutex, NULL);
		ut_a(ret == 0);

		ut_d(m_freed = false);
	}

	/** Destructor */
	~OSMutex() { }

	/** Destroy the mutex */
	void destroy()
		UNIV_NOTHROW
	{
		ut_ad(innodb_calling_exit || !m_freed);

		int	ret;

		ret = pthread_mutex_destroy(&m_mutex);

		if (ret != 0) {

			std::cerr
				<< "Return value " << ret << " when calling "
				<< "pthread_mutex_destroy()." << std::endl;
		}
		ut_d(m_freed = true);
	}

	/** Release the mutex. */
	void exit()
		UNIV_NOTHROW
	{
		ut_ad(innodb_calling_exit || !m_freed);
		int	ret = pthread_mutex_unlock(&m_mutex);
		ut_a(ret == 0);
	}

	/** Acquire the mutex. */
	void enter()
		UNIV_NOTHROW
	{
		ut_ad(innodb_calling_exit || !m_freed);
		int	ret = pthread_mutex_lock(&m_mutex);
		ut_a(ret == 0);
	}

	/** @return true if locking succeeded */
	bool try_lock()
		UNIV_NOTHROW
	{
		ut_ad(innodb_calling_exit || !m_freed);
		return(pthread_mutex_trylock(&m_mutex) == 0);
	}

	/** Required for os_event_t */
	operator sys_mutex_t*()
		UNIV_NOTHROW
	{
		return(&m_mutex);
	}

private:
#ifdef UNIV_DEBUG
	/** true if the mutex has been freed/destroyed. */
	bool			m_freed;
#endif /* UNIV_DEBUG */

	sys_mutex_t		m_mutex;
};

typedef OSMutex EventMutex;


/** The number of microsecnds in a second. */
static const uint64_t MICROSECS_IN_A_SECOND = 1000000;

/** The number of nanoseconds in a second. */
static const uint64_t NANOSECS_IN_A_SECOND = 1000 * MICROSECS_IN_A_SECOND;

/** Native condition variable */
typedef pthread_cond_t		os_cond_t;

/** InnoDB condition variable. */
struct os_event {
	os_event(const char* name) UNIV_NOTHROW;

	~os_event() UNIV_NOTHROW;

	friend void os_event_global_init();
	friend void os_event_global_destroy();

	/**
	Destroys a condition variable */
	void destroy() UNIV_NOTHROW
	{
		int	ret = pthread_cond_destroy(&cond_var);
		ut_a(ret == 0);

		mutex.destroy();
	}

	/** Set the event */
	void set() UNIV_NOTHROW
	{
		mutex.enter();

		if (!m_set) {
			broadcast();
		}

		mutex.exit();
	}

	int64_t reset() UNIV_NOTHROW
	{
		mutex.enter();

		if (m_set) {
			m_set = false;
		}

		int64_t	ret = signal_count;

		mutex.exit();

		return(ret);
	}

	/**
	Waits for an event object until it is in the signaled state.

	Typically, if the event has been signalled after the os_event_reset()
	we'll return immediately because event->m_set == true.
	There are, however, situations (e.g.: sync_array code) where we may
	lose this information. For example:

	thread A calls os_event_reset()
	thread B calls os_event_set()   [event->m_set == true]
	thread C calls os_event_reset() [event->m_set == false]
	thread A calls os_event_wait()  [infinite wait!]
	thread C calls os_event_wait()  [infinite wait!]

	Where such a scenario is possible, to avoid infinite wait, the
	value returned by reset() should be passed in as
	reset_sig_count. */
	void wait_low(int64_t reset_sig_count) UNIV_NOTHROW;

	/** @return true if the event is in the signalled state. */
	bool is_set() const UNIV_NOTHROW
	{
		return(m_set);
	}

private:
	/**
	Initialize a condition variable */
	void init() UNIV_NOTHROW
	{

		mutex.init();

		int	ret;

		ret = pthread_cond_init(&cond_var, &cond_attr);
		ut_a(ret == 0);
	}

	/**
	Wait on condition variable */
	void wait() UNIV_NOTHROW
	{
		int	ret;

		ret = pthread_cond_wait(&cond_var, mutex);
		ut_a(ret == 0);
	}

	/**
	Wakes all threads waiting for condition variable */
	void broadcast() UNIV_NOTHROW
	{
		m_set = true;
		++signal_count;

		int	ret;

		ret = pthread_cond_broadcast(&cond_var);
		ut_a(ret == 0);
	}

	/**
	Wakes one thread waiting for condition variable */
	void signal() UNIV_NOTHROW
	{
		int	ret;

		ret = pthread_cond_signal(&cond_var);
		ut_a(ret == 0);
	}

private:
	bool			m_set;		/*!< this is true when the
						event is in the signaled
						state, i.e., a thread does
						not stop if it tries to wait
						for this event */
	int64_t			signal_count;	/*!< this is incremented
						each time the event becomes
						signaled */
	EventMutex		mutex;		/*!< this mutex protects
						the next fields */


	os_cond_t		cond_var;	/*!< condition variable is
						used in waiting for the event */
	/** Attributes object passed to pthread_cond_* functions.
	Defines usage of the monotonic clock if it's available.
	Initialized once, in the os_event::global_init(), and
	destroyed in the os_event::global_destroy(). */
	static pthread_condattr_t cond_attr;

	/** True iff usage of the monotonic clock has been successfuly
	enabled for the cond_attr object. */
	static bool cond_attr_has_monotonic_clock;

	static bool global_initialized;

protected:
	// Disable copying
	os_event(const os_event&);
	os_event& operator=(const os_event&);
};

typedef struct os_event* os_event_t;

pthread_condattr_t os_event::cond_attr;
bool os_event::cond_attr_has_monotonic_clock (false);
bool os_event::global_initialized (false);

/**
Waits for an event object until it is in the signaled state.

Typically, if the event has been signalled after the os_event_reset()
we'll return immediately because event->m_set == true.
There are, however, situations (e.g.: sync_array code) where we may
lose this information. For example:

thread A calls os_event_reset()
thread B calls os_event_set()   [event->m_set == true]
thread C calls os_event_reset() [event->m_set == false]
thread A calls os_event_wait()  [infinite wait!]
thread C calls os_event_wait()  [infinite wait!]

Where such a scenario is possible, to avoid infinite wait, the
value returned by reset() should be passed in as
reset_sig_count. */
void
os_event::wait_low(
	int64_t		reset_sig_count) UNIV_NOTHROW
{
	mutex.enter();

	if (!reset_sig_count) {
		reset_sig_count = signal_count;
	}

	while (!m_set && signal_count == reset_sig_count) {

		wait();

		/* Spurious wakeups may occur: we have to check if the
		event really has been signaled after we came here to wait. */
	}

	mutex.exit();
}

/** Constructor */
os_event::os_event(const char* name) UNIV_NOTHROW
{
	ut_a(global_initialized);
	init();

	m_set = false;

	/* We return this value in os_event_reset(),
	which can then be be used to pass to the
	os_event_wait_low(). The value of zero is
	reserved in os_event_wait_low() for the case
	when the caller does not want to pass any
	signal_count value. To distinguish between
	the two cases we initialize signal_count
	to 1 here. */

	signal_count = 1;
}

/** Destructor */
os_event::~os_event() UNIV_NOTHROW
{
	destroy();
}


void os_event_global_init(void) {
	int ret = pthread_condattr_init(&os_event::cond_attr);
	ut_a(ret == 0);

#ifdef UNIV_LINUX /* MacOS does not have support. */
#ifdef HAVE_CLOCK_GETTIME
	ret = pthread_condattr_setclock(&os_event::cond_attr, CLOCK_MONOTONIC);
	if (ret == 0) {
	  os_event::cond_attr_has_monotonic_clock = true;
  }
#endif /* HAVE_CLOCK_GETTIME */

#ifndef UNIV_NO_ERR_MSGS
  if (!os_event::cond_attr_has_monotonic_clock) {
    fprintf(stderr, "CLOCK_MONOTONIC is unsupported, so do not change the"
	          " system time when MySQL is running !\n");
  }
#endif /* !UNIV_NO_ERR_MSGS */

#endif /* UNIV_LINUX */
	os_event::global_initialized = true;
}

void os_event_global_destroy(void) {
	ut_a(os_event::global_initialized);

	os_event::cond_attr_has_monotonic_clock = false;
#ifdef UNIV_DEBUG
	const int ret =
#endif /* UNIV_DEBUG */
		pthread_condattr_destroy(&os_event::cond_attr);
	ut_ad(ret == 0);

	os_event::global_initialized = false;
}



/** Mutex states. */
enum mutex_state_t {
	/** Mutex is free */
	MUTEX_STATE_UNLOCKED = 0,

	/** Mutex is acquired by some thread. */
	MUTEX_STATE_LOCKED = 1,

	/** Mutex is contended and there are threads waiting on the lock. */
	MUTEX_STATE_WAITERS = 2
};


struct TTASEventMutex {

	TTASEventMutex()
		UNIV_NOTHROW
		:
		m_lock_word(MUTEX_STATE_UNLOCKED),
		m_waiters(),
		m_event()
	{
		/* Check that lock_word is aligned. */
		ut_ad(!((ulint) &m_lock_word % sizeof(ulint)));
	}

	~TTASEventMutex()
		UNIV_NOTHROW
	{
		ut_ad(m_lock_word == MUTEX_STATE_UNLOCKED);
	}

	/** Called when the mutex is "created". Note: Not from the constructor
	but when the mutex is initialised.
	@param[in]	id		Mutex ID
	@param[in]	filename	File where mutex was created
	@param[in]	line		Line in filename */
	void init(
		const char*	filename,
		uint32_t	line)
		UNIV_NOTHROW
	{
		ut_a(m_event == 0);
		ut_a(m_lock_word == MUTEX_STATE_UNLOCKED);

		m_event = new os_event(filename);
	}

	/** This is the real desctructor. This mutex can be created in BSS and
	its desctructor will be called on exit(). We can't call
	os_event_destroy() at that stage. */
	void destroy()
		UNIV_NOTHROW
	{
		ut_ad(m_lock_word == MUTEX_STATE_UNLOCKED);

		/* We have to free the event before InnoDB shuts down. */
		delete m_event;
		m_event = 0;
	}

	/** Try and lock the mutex. Note: POSIX returns 0 on success.
	@return true on success */
	bool try_lock()
		UNIV_NOTHROW
	{
		return(tas_lock());
	}

	/** Release the mutex. */
	void exit()
		UNIV_NOTHROW
	{
		/* A problem: we assume that mutex_reset_lock word
		is a memory barrier, that is when we read the waiters
		field next, the read must be serialized in memory
		after the reset. A speculative processor might
		perform the read first, which could leave a waiting
		thread hanging indefinitely.

		Our current solution call every second
		sync_arr_wake_threads_if_sema_free()
		to wake up possible hanging threads if they are missed
		in mutex_signal_object. */

		tas_unlock();

		if (m_waiters != 0) {
			// holder_signal = 3;
			signal();
		} else {
			// holder_signal = 5;
		}
	}

	/** Acquire the mutex.
	@param[in]	max_spins	max number of spins
	@param[in]	max_delay	max delay per spin
	@param[in]	filename	from where called
	@param[in]	line		within filename */
	void enter(
		uint32_t	max_spins,
		uint32_t	max_delay,
		const char*	filename,
		uint32_t	line)
		UNIV_NOTHROW
	{
		if (!try_lock()) {
			spin_and_try_lock(max_spins, max_delay, filename, line);
		}
	}

	/** @return the lock state. */
	lock_word_t state() const
		UNIV_NOTHROW
	{
		return(m_lock_word);
	}

	/** @return true if locked by some thread */
	bool is_locked() const
		UNIV_NOTHROW
	{
		return(m_lock_word != MUTEX_STATE_UNLOCKED);
	}

private:
	/** Wait in the sync array.
	@param[in]	filename	from where it was called
	@param[in]	line		line number in file
	@param[in]	spin		retry this many times again
	@return true if the mutex acquisition was successful. */
	bool wait(
		const char*	filename,
		uint32_t	line,
		uint32_t	spin)
		UNIV_NOTHROW;

	/** Spin and wait for the mutex to become free.
	@param[in]	max_spins	max spins
	@param[in]	max_delay	max delay per spin
	@param[in,out]	n_spins		spin start index
	@return true if unlocked */
	bool is_free(
		uint32_t	max_spins,
		uint32_t	max_delay,
		uint32_t&	n_spins) const
		UNIV_NOTHROW
	{
		ut_ad(n_spins <= max_spins);

		/* Spin waiting for the lock word to become zero. Note
		that we do not have to assume that the read access to
		the lock word is atomic, as the actual locking is always
		committed with atomic test-and-set. In reality, however,
		all processors probably have an atomic read of a memory word. */

		do {
			if (!is_locked()) {
				return(true);
			}

			// ut_delay(ut_rnd_interval(0, max_delay));
			ut_delay(max_delay / 2);

			++n_spins;

		} while (n_spins < max_spins);

		return(false);
	}

	/** Spin while trying to acquire the mutex
	@param[in]	max_spins	max number of spins
	@param[in]	max_delay	max delay per spin
	@param[in]	filename	from where called
	@param[in]	line		within filename */
	void spin_and_try_lock(
		uint32_t	max_spins,
		uint32_t	max_delay,
		const char*	filename,
		uint32_t	line)
		UNIV_NOTHROW
	{
		uint32_t	n_spins = 0;
		uint32_t	n_waits = 0;
		const uint32_t	step = max_spins;

		os_rmb;

		for (;;) {

			/* If the lock was free then try and acquire it. */

			if (is_free(max_spins, max_delay, n_spins)) {

				if (try_lock()) {

					break;
				} else {

					continue;
				}

			} else {
				max_spins = n_spins + step;
			}

			++n_waits;

			os_thread_yield();

			/* The 4 below is a heuristic that has existed for a
			very long time now. It is unclear if changing this
			value will make a difference.

			NOTE: There is a delay that happens before the retry,
			finding a free slot in the sync arary and the yield
			above. Otherwise we could have simply done the extra
			spin above. */

			if (wait(filename, line, 4)) {

				n_spins += 4;

				break;
			}
		}

		/* Waits and yields will be the same number in our
		mutex design */
	}

	/** @return the value of the m_waiters flag */
	lock_word_t waiters() UNIV_NOTHROW
	{
		return(m_waiters);
	}

	/** Note that there are threads waiting on the mutex */
	void set_waiters() UNIV_NOTHROW
	{
		m_waiters = 1;
		os_wmb;
	}

	/** Note that there are no threads waiting on the mutex */
	void clear_waiters() UNIV_NOTHROW
	{
		m_waiters = 0;
		os_wmb;
	}

	/** Try and acquire the lock using TestAndSet.
	@return	true if lock succeeded */
	bool tas_lock() UNIV_NOTHROW
	{
		return(TAS(&m_lock_word, MUTEX_STATE_LOCKED)
			== MUTEX_STATE_UNLOCKED);
	}

	/** In theory __sync_lock_release should be used to release the lock.
	Unfortunately, it does not work properly alone. The workaround is
	that more conservative __sync_lock_test_and_set is used instead. */
	void tas_unlock() UNIV_NOTHROW
	{
		TAS(&m_lock_word, MUTEX_STATE_UNLOCKED);
	}

	/** Wakeup any waiting thread(s). */
	void signal() UNIV_NOTHROW;

private:
	/** Disable copying */
	TTASEventMutex(const TTASEventMutex&);
	TTASEventMutex& operator=(const TTASEventMutex&);

	/** lock_word is the target of the atomic test-and-set instruction
	when atomic operations are enabled. */
	lock_word_t		m_lock_word;

	/** Set to 0 or 1. 1 if there are (or may be) threads waiting
	in the global wait array for this mutex to be released. */
	lock_word_t		m_waiters;

	/** Used by sync0arr.cc for the wait queue */
	os_event_t		m_event;
};

bool
TTASEventMutex::wait(
	const char*	filename,
	uint32_t	line,
	uint32_t	spin)
	UNIV_NOTHROW
{
	// sync_cell_t*	cell;
	// sync_array_t*	sync_arr;

	/* sync_arr = sync_array_get_and_reserve_cell(
		this,
		(m_policy.get_id() == LATCH_ID_BUF_BLOCK_MUTEX
		 || m_policy.get_id() == LATCH_ID_BUF_POOL_ZIP)
		? SYNC_BUF_BLOCK
		: SYNC_MUTEX,
		filename, line, &cell); */
	int64_t signal_count = m_event->reset();

	/* The memory order of the array reservation and
	the change in the waiters field is important: when
	we suspend a thread, we first reserve the cell and
	then set waiters field to 1. When threads are released
	in mutex_exit, the waiters field is first set to zero
	and then the event is set to the signaled state. */

	set_waiters();

	/* Try to reserve still a few times. */

	for (uint32_t i = 0; i < spin; ++i) {

		if (try_lock()) {

			// sync_array_free_cell(sync_arr, cell);

			/* Note that in this case we leave
			the waiters field set to 1. We cannot
			reset it to zero, as we do not know if
			there are other waiters. */

			return(true);
		}
	}

	/* Now we know that there has been some thread
	holding the mutex after the change in the wait
	array and the waiters field was made.  Now there
	is no risk of infinite wait on the event. */

	m_event->wait_low(signal_count);
	// sync_array_wait_event(sync_arr, cell);

	return(false);
}


/** Wakeup any waiting thread(s). */

void
TTASEventMutex::signal() UNIV_NOTHROW
{
	clear_waiters();

	/* The memory order of resetting the waiters field and
	signaling the object is important. See LEMMA 1 above. */
	m_event->set();

	// sync_array_object_signalled();
}


int main() {
	return 0;
}
