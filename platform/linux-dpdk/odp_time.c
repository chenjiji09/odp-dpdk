/* Copyright (c) 2013-2018, Linaro Limited
 * Copyright (c) 2021-2022, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp_posix_extensions.h>

#include <time.h>
#include <string.h>
#include <inttypes.h>

#include <odp/api/time.h>
#include <odp/api/hints.h>
#include <odp_debug_internal.h>
#include <odp_init_internal.h>
#include <odp/api/plat/time_inlines.h>

#include <rte_config.h>
#include <rte_atomic.h>
#include <rte_cycles.h>

ODP_STATIC_ASSERT(_ODP_TIMESPEC_SIZE >= (sizeof(struct timespec)),
		  "_ODP_TIMESPEC_SIZE too small");

#include <odp/visibility_begin.h>

_odp_time_global_t _odp_time_glob;

/*
 * Posix timespec based functions
 */

static inline uint64_t time_spec_diff_nsec(struct timespec *t2,
					   struct timespec *t1)
{
	struct timespec diff;
	uint64_t nsec;

	diff.tv_sec  = t2->tv_sec  - t1->tv_sec;
	diff.tv_nsec = t2->tv_nsec - t1->tv_nsec;

	if (diff.tv_nsec < 0) {
		diff.tv_nsec += ODP_TIME_SEC_IN_NS;
		diff.tv_sec  -= 1;
	}

	nsec = (diff.tv_sec * ODP_TIME_SEC_IN_NS) + diff.tv_nsec;

	return nsec;
}

odp_time_t _odp_timespec_cur(void)
{
	int ret;
	odp_time_t time;
	struct timespec sys_time;
	struct timespec *start_time;

	start_time = (struct timespec *)(uintptr_t)&_odp_time_glob.timespec;

	ret = clock_gettime(CLOCK_MONOTONIC_RAW, &sys_time);
	if (odp_unlikely(ret != 0))
		_ODP_ABORT("clock_gettime failed\n");

	time.nsec = time_spec_diff_nsec(&sys_time, start_time);

	return time;
}

#include <odp/visibility_end.h>

static inline uint64_t time_spec_res(void)
{
	int ret;
	struct timespec tres;

	ret = clock_getres(CLOCK_MONOTONIC_RAW, &tres);
	if (odp_unlikely(ret != 0))
		_ODP_ABORT("clock_getres failed\n");

	return ODP_TIME_SEC_IN_NS / (uint64_t)tres.tv_nsec;
}

static inline odp_time_t time_spec_from_ns(uint64_t ns)
{
	odp_time_t time;

	time.nsec = ns;

	return time;
}

/*
 * HW time counter based functions
 */

static inline uint64_t time_hw_res(void)
{
	return _odp_time_glob.hw_freq_hz;
}

static inline odp_time_t time_hw_from_ns(uint64_t ns)
{
	odp_time_t time;
	uint64_t count;
	uint64_t freq_hz = _odp_time_glob.hw_freq_hz;
	uint64_t sec = 0;

	if (ns >= ODP_TIME_SEC_IN_NS) {
		sec = ns / ODP_TIME_SEC_IN_NS;
		ns  = ns - sec * ODP_TIME_SEC_IN_NS;
	}

	count  = sec * freq_hz;
	count += (ns * freq_hz) / ODP_TIME_SEC_IN_NS;

	time.count = count;

	return time;
}

/*
 * Common functions
 */

static inline uint64_t time_res(void)
{
	if (_odp_time_glob.use_hw)
		return time_hw_res();

	return time_spec_res();
}

static inline odp_time_t time_from_ns(uint64_t ns)
{
	if (_odp_time_glob.use_hw)
		return time_hw_from_ns(ns);

	return time_spec_from_ns(ns);
}

static inline odp_time_t time_cur_dpdk(void)
{
	odp_time_t time;

	time.u64 = rte_get_timer_cycles() - _odp_time_glob.hw_start;

	return time;
}

static inline odp_time_t time_cur_dpdk_strict(void)
{
	odp_time_t time;

	rte_mb();
	time.u64 = rte_get_timer_cycles() - _odp_time_glob.hw_start;

	return time;
}

static inline uint64_t time_res_dpdk(void)
{
	return rte_get_timer_hz();
}

static inline void time_wait_until(odp_time_t time)
{
	odp_time_t cur;

	do {
		cur = _odp_time_cur();
	} while (odp_time_cmp(time, cur) > 0);
}

odp_time_t odp_time_local_from_ns(uint64_t ns)
{
	return time_from_ns(ns);
}

odp_time_t odp_time_global_from_ns(uint64_t ns)
{
	return time_from_ns(ns);
}

uint64_t odp_time_local_res(void)
{
	return _odp_time_glob.handler.time_res();
}

uint64_t odp_time_global_res(void)
{
	return _odp_time_glob.handler.time_res();
}

void odp_time_wait_ns(uint64_t ns)
{
	odp_time_t cur = _odp_time_cur();
	odp_time_t wait = time_from_ns(ns);
	odp_time_t end_time = odp_time_sum(cur, wait);

	time_wait_until(end_time);
}

void odp_time_wait_until(odp_time_t time)
{
	time_wait_until(time);
}

static odp_bool_t is_invariant_tsc_supported(void)
{
	FILE  *file;
	char  *line = NULL;
	size_t len = 0;
	odp_bool_t nonstop_tsc  = false;
	odp_bool_t constant_tsc = false;
	odp_bool_t ret = false;

	file = fopen("/proc/cpuinfo", "rt");
	while (getline(&line, &len, file) != -1) {
		if (strstr(line, "flags") != NULL) {
			if (strstr(line, "constant_tsc") != NULL)
				constant_tsc = true;
			if (strstr(line, "nonstop_tsc") != NULL)
				nonstop_tsc = true;

			if (constant_tsc && nonstop_tsc)
				ret = true;
			else
				ret = false;

			free(line);
			fclose(file);
			return ret;
		}
	}
	free(line);
	fclose(file);
	return false;
}

static inline odp_bool_t is_dpdk_timer_cycles_support(void)
{
	if (is_invariant_tsc_supported() == true)
		return true;

#ifdef RTE_LIBEAL_USE_HPET
	if (rte_eal_hpet_init(1) == 0)
		return true;
#endif
	return false;
}

int _odp_time_init_global(void)
{
	struct timespec *timespec;
	int ret = 0;
	_odp_time_global_t *global = &_odp_time_glob;

	memset(global, 0, sizeof(_odp_time_global_t));

	if (is_dpdk_timer_cycles_support()) {
		_odp_time_glob.handler.time_cur = time_cur_dpdk;
		_odp_time_glob.handler.time_cur_strict = time_cur_dpdk_strict;
		_odp_time_glob.handler.time_res = time_res_dpdk;
		_odp_time_glob.hw_freq_hz = time_res_dpdk();
		_odp_time_glob.use_hw = 1;
		_odp_time_glob.hw_start = rte_get_timer_cycles();
		if (_odp_time_glob.hw_start == 0)
			return -1;
		else
			return 0;
	}

	_odp_time_glob.handler.time_cur = _odp_time_cur_gen;
	_odp_time_glob.handler.time_cur_strict = _odp_time_cur_gen_strict;
	_odp_time_glob.handler.time_res = time_res;

	if (_odp_cpu_has_global_time()) {
		global->use_hw = 1;
		global->hw_freq_hz  = _odp_cpu_global_time_freq();

		if (global->hw_freq_hz == 0)
			return -1;

		_ODP_PRINT("HW time counter freq: %" PRIu64 " hz\n\n", global->hw_freq_hz);

		global->hw_start = _odp_cpu_global_time();
		return 0;
	}

	timespec = (struct timespec *)(uintptr_t)global->timespec;
	timespec->tv_sec  = 0;
	timespec->tv_nsec = 0;

	ret = clock_gettime(CLOCK_MONOTONIC_RAW, timespec);

	return ret;
}

int _odp_time_term_global(void)
{
	return 0;
}
