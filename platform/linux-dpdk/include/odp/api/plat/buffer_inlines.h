/* Copyright (c) 2019-2022, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef ODP_PLAT_BUFFER_INLINES_H_
#define ODP_PLAT_BUFFER_INLINES_H_

#include <odp/api/event_types.h>
#include <odp/api/pool_types.h>

#include <odp/api/abi/buffer.h>

#include <odp/api/plat/event_inline_types.h>

#include <rte_mbuf.h>
#if defined(__PPC64__) && defined(bool)
	#undef bool
	#define bool _Bool
#endif
#if defined(__PPC64__) && defined(vector)
	#undef vector
#endif

/** @cond _ODP_HIDE_FROM_DOXYGEN_ */

extern const _odp_event_inline_offset_t _odp_event_inline_offset;

#ifndef _ODP_NO_INLINE
	/* Inline functions by default */
	#define _ODP_INLINE static inline
	#define odp_buffer_from_event __odp_buffer_from_event
	#define odp_buffer_to_event __odp_buffer_to_event
	#define odp_buffer_addr __odp_buffer_addr
	#define odp_buffer_pool __odp_buffer_pool
	#define odp_buffer_free __odp_buffer_free
	#define odp_buffer_free_multi __odp_buffer_free_multi
#else
	#define _ODP_INLINE
#endif

_ODP_INLINE odp_buffer_t odp_buffer_from_event(odp_event_t ev)
{
	return (odp_buffer_t)ev;
}

_ODP_INLINE odp_event_t odp_buffer_to_event(odp_buffer_t buf)
{
	return (odp_event_t)buf;
}

_ODP_INLINE void *odp_buffer_addr(odp_buffer_t buf)
{
	return _odp_event_hdr_field(buf, void *, base_data);
}

_ODP_INLINE odp_pool_t odp_buffer_pool(odp_buffer_t buf)
{
	return (odp_pool_t)(uintptr_t)_odp_event_hdr_field(buf, void *, pool);
}

_ODP_INLINE void odp_buffer_free(odp_buffer_t buf)
{
	rte_mbuf_raw_free((struct rte_mbuf *)buf);
}

_ODP_INLINE void odp_buffer_free_multi(const odp_buffer_t buf[], int num)
{
	for (int i = 0; i < num; i++)
		rte_mbuf_raw_free((struct rte_mbuf *)buf[i]);
}

/** @endcond */

#endif
