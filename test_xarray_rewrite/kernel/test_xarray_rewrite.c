// SPDX-License-Identifier: GPL-2.0+
/*
 * test_xarray.c: Test the XArray API
 * Copyright (c) 2017-2018 Microsoft Corporation
 * Author: Matthew Wilcox <willy@infradead.org>
 */

#include <linux/xarray.h>
#include <linux/module.h>
#include "ktf.h" 
struct array_context {
    struct ktf_context k;
    struct xarray *xa;
};

static unsigned int tests_run;
static unsigned int tests_passed;

#ifndef XA_DEBUG
# ifdef __KERNEL__
void xa_dump(const struct xarray *xa) { }
# endif
#undef XA_BUG_ON
#define XA_BUG_ON(xa, x) do {					\
	tests_run++;						\
	if (x) {						\
		printk("BUG at %s:%d\n", __func__, __LINE__);	\
		xa_dump(xa);					\
		dump_stack();					\
	} else {						\
		tests_passed++;					\
	}							\
} while (0)
#endif

static void *xa_store_index(struct ktf_test *self, struct xarray *xa, unsigned long index, gfp_t gfp)
{
	return xa_store(xa, index, xa_mk_value(index & LONG_MAX), gfp);
}

static void xa_alloc_index(struct ktf_test *self, struct xarray *xa, unsigned long index, gfp_t gfp)
{
	u32 id = 0;

	EXPECT_FALSE(xa_alloc(xa, &id, UINT_MAX, xa_mk_value(index & LONG_MAX),
				gfp) != 0);
	EXPECT_FALSE(id != index);
}

static void xa_erase_index(struct ktf_test *self, struct xarray *xa, unsigned long index)
{
	EXPECT_FALSE(xa_erase(xa, index) != xa_mk_value(index & LONG_MAX));
	EXPECT_FALSE(xa_load(xa, index) != NULL);
}

/*
 * If anyone needs this, please move it to xarray.c.  We have no current
 * users outside the test suite because all current multislot users want
 * to use the advanced API.
 */
static void *xa_store_order(struct ktf_test *self, struct xarray *xa, unsigned long index,
		unsigned order, void *entry, gfp_t gfp)
{
	XA_STATE_ORDER(xas, xa, index, order);
	void *curr;

	do {
		xas_lock(&xas);
		curr = xas_store(&xas, entry);
		xas_unlock(&xas);
	} while (xas_nomem(&xas, gfp));

	return curr;
}

TEST(test_xarray_rewrite, check_xa_err) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	EXPECT_FALSE(xa_err(xa_store_index(self, xa, 0, GFP_NOWAIT)) != 0);
	EXPECT_FALSE(xa_err(xa_erase(xa, 0)) != 0);
#ifndef __KERNEL__
	/* The kernel does not fail GFP_NOWAIT allocations */
	EXPECT_FALSE(xa_err(xa_store_index(self, xa, 1, GFP_NOWAIT)) != -ENOMEM);
	EXPECT_FALSE(xa_err(xa_store_index(self, xa, 1, GFP_NOWAIT)) != -ENOMEM);
#endif
	EXPECT_FALSE(xa_err(xa_store_index(self, xa, 1, GFP_KERNEL)) != 0);
	EXPECT_FALSE(xa_err(xa_store(xa, 1, xa_mk_value(0), GFP_KERNEL)) != 0);
	EXPECT_FALSE(xa_err(xa_erase(xa, 1)) != 0);
// kills the test-suite :-(
//	XA_BUG_ON(xa, xa_err(xa_store(xa, 0, xa_mk_internal(0), 0)) != -EINVAL);
}

TEST(test_xarray_rewrite, check_xas_retry) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	XA_STATE(xas, xa, 0);
	void *entry;

	xa_store_index(self, xa, 0, GFP_KERNEL);
	xa_store_index(self, xa, 1, GFP_KERNEL);

	rcu_read_lock();
	EXPECT_FALSE(xas_find(&xas, ULONG_MAX) != xa_mk_value(0));
	xa_erase_index(self, xa, 1);
	EXPECT_FALSE(!xa_is_retry(xas_reload(&xas)));
	EXPECT_FALSE(xas_retry(&xas, NULL));
	EXPECT_FALSE(xas_retry(&xas, xa_mk_value(0)));
	xas_reset(&xas);
	EXPECT_FALSE(xas.xa_node != XAS_RESTART);
	EXPECT_FALSE(xas_next_entry(&xas, ULONG_MAX) != xa_mk_value(0));
	EXPECT_FALSE(xas.xa_node != NULL);

	EXPECT_FALSE(xa_store_index(self, xa, 1, GFP_KERNEL) != NULL);
	EXPECT_FALSE(!xa_is_internal(xas_reload(&xas)));
	xas.xa_node = XAS_RESTART;
	EXPECT_FALSE(xas_next_entry(&xas, ULONG_MAX) != xa_mk_value(0));
	rcu_read_unlock();

	/* Make sure we can iterate through retry entries */
	xas_lock(&xas);
	xas_set(&xas, 0);
	xas_store(&xas, XA_RETRY_ENTRY);
	xas_set(&xas, 1);
	xas_store(&xas, XA_RETRY_ENTRY);

	xas_set(&xas, 0);
	xas_for_each(&xas, entry, ULONG_MAX) {
		xas_store(&xas, xa_mk_value(xas.xa_index));
	}
	xas_unlock(&xas);

	xa_erase_index(self, xa, 0);
	xa_erase_index(self, xa, 1);
}

TEST(test_xarray_rewrite, check_xa_load) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	unsigned long i, j;

	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 1024; j++) {
			void *entry = xa_load(xa, j);
			if (j < i)
				EXPECT_FALSE(xa_to_value(entry) != j);
			else
				EXPECT_FALSE(entry);
		}
		EXPECT_FALSE(xa_store_index(self, xa, i, GFP_KERNEL) != NULL);
	}

	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 1024; j++) {
			void *entry = xa_load(xa, j);
			if (j >= i)
				EXPECT_FALSE(xa_to_value(entry) != j);
			else
				EXPECT_FALSE(entry);
		}
		xa_erase_index(self, xa, i);
	}
	EXPECT_FALSE(!xa_empty(xa));
}

static noinline void check_xa_mark_1(struct ktf_test *self, struct xarray *xa, unsigned long index)
{
	unsigned int order;
	unsigned int max_order = IS_ENABLED(CONFIG_XARRAY_MULTI) ? 8 : 1;

	/* NULL elements have no marks set */
	EXPECT_FALSE(xa_get_mark(xa, index, XA_MARK_0));
	xa_set_mark(xa, index, XA_MARK_0);
	EXPECT_FALSE(xa_get_mark(xa, index, XA_MARK_0));

	/* Storing a pointer will not make a mark appear */
	EXPECT_FALSE(xa_store_index(self, xa, index, GFP_KERNEL) != NULL);
	EXPECT_FALSE(xa_get_mark(xa, index, XA_MARK_0));
	xa_set_mark(xa, index, XA_MARK_0);
	EXPECT_FALSE(!xa_get_mark(xa, index, XA_MARK_0));

	/* Setting one mark will not set another mark */
	EXPECT_FALSE(xa_get_mark(xa, index + 1, XA_MARK_0));
	EXPECT_FALSE(xa_get_mark(xa, index, XA_MARK_1));

	/* Storing NULL clears marks, and they can't be set again */
	xa_erase_index(self, xa, index);
	EXPECT_FALSE(!xa_empty(xa));
	EXPECT_FALSE(xa_get_mark(xa, index, XA_MARK_0));
	xa_set_mark(xa, index, XA_MARK_0);
	EXPECT_FALSE(xa_get_mark(xa, index, XA_MARK_0));

	/*
	 * Storing a multi-index entry over entries with marks gives the
	 * entire entry the union of the marks
	 */
	BUG_ON((index % 4) != 0);
	for (order = 2; order < max_order; order++) {
		unsigned long base = round_down(index, 1UL << order);
		unsigned long next = base + (1UL << order);
		unsigned long i;

		EXPECT_FALSE(xa_store_index(self, xa, index + 1, GFP_KERNEL));
		xa_set_mark(xa, index + 1, XA_MARK_0);
		EXPECT_FALSE(xa_store_index(self, xa, index + 2, GFP_KERNEL));
		xa_set_mark(xa, index + 2, XA_MARK_1);
		EXPECT_FALSE(xa_store_index(self, xa, next, GFP_KERNEL));
		xa_store_order(self, xa, index, order, xa_mk_value(index),
				GFP_KERNEL);
		for (i = base; i < next; i++) {
			XA_STATE(xas, xa, i);
			unsigned int seen = 0;
			void *entry;

			EXPECT_FALSE(!xa_get_mark(xa, i, XA_MARK_0));
			EXPECT_FALSE(!xa_get_mark(xa, i, XA_MARK_1));
			EXPECT_FALSE(xa_get_mark(xa, i, XA_MARK_2));

			/* We should see two elements in the array */
			xas_for_each(&xas, entry, ULONG_MAX)
				seen++;
			EXPECT_FALSE(seen != 2);

			/* One of which is marked */
			xas_set(&xas, 0);
			seen = 0;
			xas_for_each_marked(&xas, entry, ULONG_MAX, XA_MARK_0)
				seen++;
			EXPECT_FALSE(seen != 1);
		}
		EXPECT_FALSE(xa_get_mark(xa, next, XA_MARK_0));
		EXPECT_FALSE(xa_get_mark(xa, next, XA_MARK_1));
		EXPECT_FALSE(xa_get_mark(xa, next, XA_MARK_2));
		xa_erase_index(self, xa, index);
		xa_erase_index(self, xa, next);
		EXPECT_FALSE(!xa_empty(xa));
	}
	EXPECT_FALSE(!xa_empty(xa));
}

static noinline void check_xa_mark_2(struct ktf_test *self, struct xarray *xa)
{
	XA_STATE(xas, xa, 0);
	unsigned long index;
	unsigned int count = 0;
	void *entry;

	xa_store_index(self, xa, 0, GFP_KERNEL);
	xa_set_mark(xa, 0, XA_MARK_0);
	xas_lock(&xas);
	xas_load(&xas);
	xas_init_marks(&xas);
	xas_unlock(&xas);
	EXPECT_FALSE(!xa_get_mark(xa, 0, XA_MARK_0) == 0);

	for (index = 3500; index < 4500; index++) {
		xa_store_index(self, xa, index, GFP_KERNEL);
		xa_set_mark(xa, index, XA_MARK_0);
	}

	xas_reset(&xas);
	rcu_read_lock();
	xas_for_each_marked(&xas, entry, ULONG_MAX, XA_MARK_0)
		count++;
	rcu_read_unlock();
	EXPECT_FALSE(count != 1000);

	xas_lock(&xas);
	xas_for_each(&xas, entry, ULONG_MAX) {
		xas_init_marks(&xas);
		EXPECT_FALSE(!xa_get_mark(xa, xas.xa_index, XA_MARK_0));
		EXPECT_FALSE(!xas_get_mark(&xas, XA_MARK_0));
	}
	xas_unlock(&xas);

	xa_destroy(xa);
}

TEST(test_xarray_rewrite, check_xa_mark) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	unsigned long index;

	for (index = 0; index < 16384; index += 4)
		check_xa_mark_1(self, xa, index);

	check_xa_mark_2(self, xa);
}

TEST(test_xarray_rewrite, check_xa_shrink) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	XA_STATE(xas, xa, 1);
	struct xa_node *node;
	unsigned int order;
	unsigned int max_order = IS_ENABLED(CONFIG_XARRAY_MULTI) ? 15 : 1;

	EXPECT_FALSE(!xa_empty(xa));
	EXPECT_FALSE(xa_store_index(self, xa, 0, GFP_KERNEL) != NULL);
	EXPECT_FALSE(xa_store_index(self, xa, 1, GFP_KERNEL) != NULL);

	/*
	 * Check that erasing the entry at 1 shrinks the tree and properly
	 * marks the node as being deleted.
	 */
	xas_lock(&xas);
	EXPECT_FALSE(xas_load(&xas) != xa_mk_value(1));
	node = xas.xa_node;
	EXPECT_FALSE(xa_entry_locked(xa, node, 0) != xa_mk_value(0));
	EXPECT_FALSE(xas_store(&xas, NULL) != xa_mk_value(1));
	EXPECT_FALSE(xa_load(xa, 1) != NULL);
	EXPECT_FALSE(xas.xa_node != XAS_BOUNDS);
	EXPECT_FALSE(xa_entry_locked(xa, node, 0) != XA_RETRY_ENTRY);
	EXPECT_FALSE(xas_load(&xas) != NULL);
	xas_unlock(&xas);
	EXPECT_FALSE(xa_load(xa, 0) != xa_mk_value(0));
	xa_erase_index(self, xa, 0);
	EXPECT_FALSE(!xa_empty(xa));

	for (order = 0; order < max_order; order++) {
		unsigned long max = (1UL << order) - 1;
		xa_store_order(self, xa, 0, order, xa_mk_value(0), GFP_KERNEL);
		EXPECT_FALSE(xa_load(xa, max) != xa_mk_value(0));
		EXPECT_FALSE(xa_load(xa, max + 1) != NULL);
		rcu_read_lock();
		node = xa_head(xa);
		rcu_read_unlock();
		EXPECT_FALSE(xa_store_index(self, xa, ULONG_MAX, GFP_KERNEL) !=
				NULL);
		rcu_read_lock();
		EXPECT_FALSE(xa_head(xa) == node);
		rcu_read_unlock();
		EXPECT_FALSE(xa_load(xa, max + 1) != NULL);
		xa_erase_index(self, xa, ULONG_MAX);
		EXPECT_FALSE(xa->xa_head != node);
		xa_erase_index(self, xa, 0);
	}
}

TEST(test_xarray_rewrite, check_cmpxchg) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	void *FIVE = xa_mk_value(5);
	void *SIX = xa_mk_value(6);
	void *LOTS = xa_mk_value(12345678);

	EXPECT_FALSE(!xa_empty(xa));
	EXPECT_FALSE(xa_store_index(self, xa, 12345678, GFP_KERNEL) != NULL);
	EXPECT_FALSE(xa_insert(xa, 12345678, xa, GFP_KERNEL) != -EEXIST);
	EXPECT_FALSE(xa_cmpxchg(xa, 12345678, SIX, FIVE, GFP_KERNEL) != LOTS);
	EXPECT_FALSE(xa_cmpxchg(xa, 12345678, LOTS, FIVE, GFP_KERNEL) != LOTS);
	EXPECT_FALSE(xa_cmpxchg(xa, 12345678, FIVE, LOTS, GFP_KERNEL) != FIVE);
	EXPECT_FALSE(xa_cmpxchg(xa, 5, FIVE, NULL, GFP_KERNEL) != NULL);
	EXPECT_FALSE(xa_cmpxchg(xa, 5, NULL, FIVE, GFP_KERNEL) != NULL);
	xa_erase_index(self, xa, 12345678);
	xa_erase_index(self, xa, 5);
	EXPECT_FALSE(!xa_empty(xa));
}

TEST(test_xarray_rewrite, check_reserve) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	void *entry;
	unsigned long index = 0;

	/* An array with a reserved entry is not empty */
	EXPECT_FALSE(!xa_empty(xa));
	xa_reserve(xa, 12345678, GFP_KERNEL);
	EXPECT_FALSE(xa_empty(xa));
	EXPECT_FALSE(xa_load(xa, 12345678));
	xa_release(xa, 12345678);
	EXPECT_FALSE(!xa_empty(xa));

	/* Releasing a used entry does nothing */
	xa_reserve(xa, 12345678, GFP_KERNEL);
	EXPECT_FALSE(xa_store_index(self, xa, 12345678, GFP_NOWAIT) != NULL);
	xa_release(xa, 12345678);
	xa_erase_index(self, xa, 12345678);
	EXPECT_FALSE(!xa_empty(xa));

	/* cmpxchg sees a reserved entry as NULL */
	xa_reserve(xa, 12345678, GFP_KERNEL);
	EXPECT_FALSE(xa_cmpxchg(xa, 12345678, NULL, xa_mk_value(12345678),
				GFP_NOWAIT) != NULL);
	xa_release(xa, 12345678);
	xa_erase_index(self, xa, 12345678);
	EXPECT_FALSE(!xa_empty(xa));

	/* Can iterate through a reserved entry */
	xa_store_index(self, xa, 5, GFP_KERNEL);
	xa_reserve(xa, 6, GFP_KERNEL);
	xa_store_index(self, xa, 7, GFP_KERNEL);

	xa_for_each(xa, entry, index, ULONG_MAX, XA_PRESENT) {
		EXPECT_FALSE(index != 5 && index != 7);
	}
	xa_destroy(xa);
}

TEST(test_xarray_rewrite, check_xas_erase) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	XA_STATE(xas, xa, 0);
	void *entry;
	unsigned long i, j;

	for (i = 0; i < 200; i++) {
		for (j = i; j < 2 * i + 17; j++) {
			xas_set(&xas, j);
			do {
				xas_lock(&xas);
				xas_store(&xas, xa_mk_value(j));
				xas_unlock(&xas);
			} while (xas_nomem(&xas, GFP_KERNEL));
		}

		xas_set(&xas, ULONG_MAX);
		do {
			xas_lock(&xas);
			xas_store(&xas, xa_mk_value(0));
			xas_unlock(&xas);
		} while (xas_nomem(&xas, GFP_KERNEL));

		xas_lock(&xas);
		xas_store(&xas, NULL);

		xas_set(&xas, 0);
		j = i;
		xas_for_each(&xas, entry, ULONG_MAX) {
			EXPECT_FALSE(entry != xa_mk_value(j));
			xas_store(&xas, NULL);
			j++;
		}
		xas_unlock(&xas);
		EXPECT_FALSE(!xa_empty(xa));
	}
}

#ifdef CONFIG_XARRAY_MULTI
static noinline void check_multi_store_1(struct ktf_test *self, struct xarray *xa, unsigned long index,
		unsigned int order)
{
	XA_STATE(xas, xa, index);
	unsigned long min = index & ~((1UL << order) - 1);
	unsigned long max = min + (1UL << order);

	xa_store_order(self, xa, index, order, xa_mk_value(index), GFP_KERNEL);
	EXPECT_FALSE(xa_load(xa, min) != xa_mk_value(index));
	EXPECT_FALSE(xa_load(xa, max - 1) != xa_mk_value(index));
	EXPECT_FALSE(xa_load(xa, max) != NULL);
	EXPECT_FALSE(xa_load(xa, min - 1) != NULL);

	EXPECT_FALSE(xas_store(&xas, xa_mk_value(min)) != xa_mk_value(index));
	EXPECT_FALSE(xa_load(xa, min) != xa_mk_value(min));
	EXPECT_FALSE(xa_load(xa, max - 1) != xa_mk_value(min));
	EXPECT_FALSE(xa_load(xa, max) != NULL);
	EXPECT_FALSE(xa_load(xa, min - 1) != NULL);

	xa_erase_index(self, xa, min);
	EXPECT_FALSE(!xa_empty(xa));
}

static noinline void check_multi_store_2(struct ktf_test *self, struct xarray *xa, unsigned long index,
		unsigned int order)
{
	XA_STATE(xas, xa, index);
	xa_store_order(self, xa, index, order, xa_mk_value(0), GFP_KERNEL);

	EXPECT_FALSE(xas_store(&xas, xa_mk_value(1)) != xa_mk_value(0));
	EXPECT_FALSE(xas.xa_index != index);
	EXPECT_FALSE(xas_store(&xas, NULL) != xa_mk_value(1));
	EXPECT_FALSE(!xa_empty(xa));
}
#endif

TEST(test_xarray_rewrite, check_multi_store) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


#ifdef CONFIG_XARRAY_MULTI
	unsigned long i, j, k;
	unsigned int max_order = (sizeof(long) == 4) ? 30 : 60;

	/* Loading from any position returns the same value */
	xa_store_order(self, xa, 0, 1, xa_mk_value(0), GFP_KERNEL);
	EXPECT_FALSE(xa_load(xa, 0) != xa_mk_value(0));
	EXPECT_FALSE(xa_load(xa, 1) != xa_mk_value(0));
	EXPECT_FALSE(xa_load(xa, 2) != NULL);
	rcu_read_lock();
	EXPECT_FALSE(xa_to_node(xa_head(xa))->count != 2);
	EXPECT_FALSE(xa_to_node(xa_head(xa))->nr_values != 2);
	rcu_read_unlock();

	/* Storing adjacent to the value does not alter the value */
	xa_store(xa, 3, xa, GFP_KERNEL);
	EXPECT_FALSE(xa_load(xa, 0) != xa_mk_value(0));
	EXPECT_FALSE(xa_load(xa, 1) != xa_mk_value(0));
	EXPECT_FALSE(xa_load(xa, 2) != NULL);
	rcu_read_lock();
	EXPECT_FALSE(xa_to_node(xa_head(xa))->count != 3);
	EXPECT_FALSE(xa_to_node(xa_head(xa))->nr_values != 2);
	rcu_read_unlock();

	/* Overwriting multiple indexes works */
	xa_store_order(self, xa, 0, 2, xa_mk_value(1), GFP_KERNEL);
	EXPECT_FALSE(xa_load(xa, 0) != xa_mk_value(1));
	EXPECT_FALSE(xa_load(xa, 1) != xa_mk_value(1));
	EXPECT_FALSE(xa_load(xa, 2) != xa_mk_value(1));
	EXPECT_FALSE(xa_load(xa, 3) != xa_mk_value(1));
	EXPECT_FALSE(xa_load(xa, 4) != NULL);
	rcu_read_lock();
	EXPECT_FALSE(xa_to_node(xa_head(xa))->count != 4);
	EXPECT_FALSE(xa_to_node(xa_head(xa))->nr_values != 4);
	rcu_read_unlock();

	/* We can erase multiple values with a single store */
	xa_store_order(self, xa, 0, 63, NULL, GFP_KERNEL);
	EXPECT_FALSE(!xa_empty(xa));

	/* Even when the first slot is empty but the others aren't */
	xa_store_index(self, xa, 1, GFP_KERNEL);
	xa_store_index(self, xa, 2, GFP_KERNEL);
	xa_store_order(self, xa, 0, 2, NULL, GFP_KERNEL);
	EXPECT_FALSE(!xa_empty(xa));

	for (i = 0; i < max_order; i++) {
		for (j = 0; j < max_order; j++) {
			xa_store_order(self, xa, 0, i, xa_mk_value(i), GFP_KERNEL);
			xa_store_order(self, xa, 0, j, xa_mk_value(j), GFP_KERNEL);

			for (k = 0; k < max_order; k++) {
				void *entry = xa_load(xa, (1UL << k) - 1);
				if ((i < k) && (j < k))
					EXPECT_FALSE(entry != NULL);
				else
					EXPECT_FALSE(entry != xa_mk_value(j));
			}

			xa_erase(xa, 0);
			EXPECT_FALSE(!xa_empty(xa));
		}
	}

	for (i = 0; i < 20; i++) {
		check_multi_store_1(self, xa, 200, i);
		check_multi_store_1(self, xa, 0, i);
		check_multi_store_1(self, xa, (1UL << i) + 1, i);
	}
	check_multi_store_2(self, xa, 4095, 9);
#endif
}

static DEFINE_XARRAY_ALLOC(xa0);

TEST(test_xarray_rewrite, check_xa_alloc) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	int i;
	u32 id;

	/* An empty array should assign 0 to the first alloc */
	xa_alloc_index(self, &xa0, 0, GFP_KERNEL);

	/* Erasing it should make the array empty again */
	xa_erase_index(self, &xa0, 0);
	EXPECT_FALSE(!xa_empty(&xa0));

	/* And it should assign 0 again */
	xa_alloc_index(self, &xa0, 0, GFP_KERNEL);

	/* The next assigned ID should be 1 */
	xa_alloc_index(self, &xa0, 1, GFP_KERNEL);
	xa_erase_index(self, &xa0, 1);

	/* Storing a value should mark it used */
	xa_store_index(self, &xa0, 1, GFP_KERNEL);
	xa_alloc_index(self, &xa0, 2, GFP_KERNEL);

	/* If we then erase 0, it should be free */
	xa_erase_index(self, &xa0, 0);
	xa_alloc_index(self, &xa0, 0, GFP_KERNEL);

	xa_erase_index(self, &xa0, 1);
	xa_erase_index(self, &xa0, 2);

	for (i = 1; i < 5000; i++) {
		xa_alloc_index(self, &xa0, i, GFP_KERNEL);
	}

	xa_destroy(&xa0);

	id = 0xfffffffeU;
	EXPECT_FALSE(xa_alloc(&xa0, &id, UINT_MAX, xa_mk_value(0),
				GFP_KERNEL) != 0);
	EXPECT_FALSE(id != 0xfffffffeU);
	EXPECT_FALSE(xa_alloc(&xa0, &id, UINT_MAX, xa_mk_value(0),
				GFP_KERNEL) != 0);
	EXPECT_FALSE(id != 0xffffffffU);
	EXPECT_FALSE(xa_alloc(&xa0, &id, UINT_MAX, xa_mk_value(0),
				GFP_KERNEL) != -ENOSPC);
	EXPECT_FALSE(id != 0xffffffffU);
	xa_destroy(&xa0);
}

static noinline void __check_store_iter(struct ktf_test *self, struct xarray *xa, unsigned long start,
			unsigned int order, unsigned int present)
{
	XA_STATE_ORDER(xas, xa, start, order);
	void *entry;
	unsigned int count = 0;

retry:
	xas_lock(&xas);
	xas_for_each_conflict(&xas, entry) {
		EXPECT_FALSE(!xa_is_value(entry));
		EXPECT_FALSE(entry < xa_mk_value(start));
		EXPECT_FALSE(entry > xa_mk_value(start + (1UL << order) - 1));
		count++;
	}
	xas_store(&xas, xa_mk_value(start));
	xas_unlock(&xas);
	if (xas_nomem(&xas, GFP_KERNEL)) {
		count = 0;
		goto retry;
	}
	EXPECT_FALSE(xas_error(&xas));
	EXPECT_FALSE(count != present);
	EXPECT_FALSE(xa_load(xa, start) != xa_mk_value(start));
	EXPECT_FALSE(xa_load(xa, start + (1UL << order) - 1) !=
			xa_mk_value(start));
	xa_erase_index(self, xa, start);
}

TEST(test_xarray_rewrite, check_store_iter) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	unsigned int i, j;
	unsigned int max_order = IS_ENABLED(CONFIG_XARRAY_MULTI) ? 20 : 1;

	for (i = 0; i < max_order; i++) {
		unsigned int min = 1 << i;
		unsigned int max = (2 << i) - 1;
		__check_store_iter(self, xa, 0, i, 0);
		EXPECT_FALSE(!xa_empty(xa));
		__check_store_iter(self, xa, min, i, 0);
		EXPECT_FALSE(!xa_empty(xa));

		xa_store_index(self, xa, min, GFP_KERNEL);
		__check_store_iter(self, xa, min, i, 1);
		EXPECT_FALSE(!xa_empty(xa));
		xa_store_index(self, xa, max, GFP_KERNEL);
		__check_store_iter(self, xa, min, i, 1);
		EXPECT_FALSE(!xa_empty(xa));

		for (j = 0; j < min; j++)
			xa_store_index(self, xa, j, GFP_KERNEL);
		__check_store_iter(self, xa, 0, i, min);
		EXPECT_FALSE(!xa_empty(xa));
		for (j = 0; j < min; j++)
			xa_store_index(self, xa, min + j, GFP_KERNEL);
		__check_store_iter(self, xa, min, i, min);
		EXPECT_FALSE(!xa_empty(xa));
	}
#ifdef CONFIG_XARRAY_MULTI
	xa_store_index(self, xa, 63, GFP_KERNEL);
	xa_store_index(self, xa, 65, GFP_KERNEL);
	__check_store_iter(self, xa, 64, 2, 1);
	xa_erase_index(self, xa, 63);
#endif
	EXPECT_FALSE(!xa_empty(xa));
}

static noinline void check_multi_find(struct ktf_test *self, struct xarray *xa)
{
#ifdef CONFIG_XARRAY_MULTI
	unsigned long index;

	xa_store_order(self, xa, 12, 2, xa_mk_value(12), GFP_KERNEL);
	EXPECT_FALSE(xa_store_index(self, xa, 16, GFP_KERNEL) != NULL);

	index = 0;
	EXPECT_FALSE(xa_find(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(12));
	EXPECT_FALSE(index != 12);
	index = 13;
	EXPECT_FALSE(xa_find(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(12));
	EXPECT_FALSE((index < 12) || (index >= 16));
	EXPECT_FALSE(xa_find_after(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(16));
	EXPECT_FALSE(index != 16);

	xa_erase_index(self, xa, 12);
	xa_erase_index(self, xa, 16);
	EXPECT_FALSE(!xa_empty(xa));
#endif
}

static noinline void check_multi_find_2(struct ktf_test *self, struct xarray *xa)
{
	unsigned int max_order = IS_ENABLED(CONFIG_XARRAY_MULTI) ? 10 : 1;
	unsigned int i, j;
	void *entry;

	for (i = 0; i < max_order; i++) {
		unsigned long index = 1UL << i;
		for (j = 0; j < index; j++) {
			XA_STATE(xas, xa, j + index);
			xa_store_index(self, xa, index - 1, GFP_KERNEL);
			xa_store_order(self, xa, index, i, xa_mk_value(index),
					GFP_KERNEL);
			rcu_read_lock();
			xas_for_each(&xas, entry, ULONG_MAX) {
				xa_erase_index(self, xa, index);
			}
			rcu_read_unlock();
			xa_erase_index(self, xa, index - 1);
			EXPECT_FALSE(!xa_empty(xa));
		}
	}
}

TEST(test_xarray_rewrite, check_find) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	unsigned long i, j, k;

	EXPECT_FALSE(!xa_empty(xa));

	/*
	 * Check xa_find with all pairs between 0 and 99 inclusive,
	 * starting at every index between 0 and 99
	 */
	for (i = 0; i < 100; i++) {
		EXPECT_FALSE(xa_store_index(self, xa, i, GFP_KERNEL) != NULL);
		xa_set_mark(xa, i, XA_MARK_0);
		for (j = 0; j < i; j++) {
			EXPECT_FALSE(xa_store_index(self, xa, j, GFP_KERNEL) !=
					NULL);
			xa_set_mark(xa, j, XA_MARK_0);
			for (k = 0; k < 100; k++) {
				unsigned long index = k;
				void *entry = xa_find(xa, &index, ULONG_MAX,
								XA_PRESENT);
				if (k <= j)
					EXPECT_FALSE(index != j);
				else if (k <= i)
					EXPECT_FALSE(index != i);
				else
					EXPECT_FALSE(entry != NULL);

				index = k;
				entry = xa_find(xa, &index, ULONG_MAX,
								XA_MARK_0);
				if (k <= j)
					EXPECT_FALSE(index != j);
				else if (k <= i)
					EXPECT_FALSE(index != i);
				else
					EXPECT_FALSE(entry != NULL);
			}
			xa_erase_index(self, xa, j);
			EXPECT_FALSE(xa_get_mark(xa, j, XA_MARK_0));
			EXPECT_FALSE(!xa_get_mark(xa, i, XA_MARK_0));
		}
		xa_erase_index(self, xa, i);
		EXPECT_FALSE(xa_get_mark(xa, i, XA_MARK_0));
	}
	EXPECT_FALSE(!xa_empty(xa));
	check_multi_find(self, xa);
	check_multi_find_2(self, xa);
}

/* See find_swap_entry() in mm/shmem.c */
static noinline unsigned long xa_find_entry(struct ktf_test *self, struct xarray *xa, void *item)
{
	XA_STATE(xas, xa, 0);
	unsigned int checked = 0;
	void *entry;

	rcu_read_lock();
	xas_for_each(&xas, entry, ULONG_MAX) {
		if (xas_retry(&xas, entry))
			continue;
		if (entry == item)
			break;
		checked++;
		if ((checked % 4) != 0)
			continue;
		xas_pause(&xas);
	}
	rcu_read_unlock();

	return entry ? xas.xa_index : -1;
}

TEST(test_xarray_rewrite, check_find_entry) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


#ifdef CONFIG_XARRAY_MULTI
	unsigned int order;
	unsigned long offset, index;

	for (order = 0; order < 20; order++) {
		for (offset = 0; offset < (1UL << (order + 3));
		     offset += (1UL << order)) {
			for (index = 0; index < (1UL << (order + 5));
			     index += (1UL << order)) {
				xa_store_order(self, xa, index, order,
						xa_mk_value(index), GFP_KERNEL);
				EXPECT_FALSE(xa_load(xa, index) !=
						xa_mk_value(index));
				EXPECT_FALSE(xa_find_entry(self, xa,
						xa_mk_value(index)) != index);
			}
			EXPECT_FALSE(xa_find_entry(self, xa, xa) != -1);
			xa_destroy(xa);
		}
	}
#endif

	EXPECT_FALSE(xa_find_entry(self, xa, xa) != -1);
	xa_store_index(self, xa, ULONG_MAX, GFP_KERNEL);
	EXPECT_FALSE(xa_find_entry(self, xa, xa) != -1);
	EXPECT_FALSE(xa_find_entry(self, xa, xa_mk_value(LONG_MAX)) != -1);
	xa_erase_index(self, xa, ULONG_MAX);
	EXPECT_FALSE(!xa_empty(xa));
}

static noinline void check_move_small(struct ktf_test *self, struct xarray *xa, unsigned long idx)
{
	XA_STATE(xas, xa, 0);
	unsigned long i;

	xa_store_index(self, xa, 0, GFP_KERNEL);
	xa_store_index(self, xa, idx, GFP_KERNEL);

	rcu_read_lock();
	for (i = 0; i < idx * 4; i++) {
		void *entry = xas_next(&xas);
		if (i <= idx)
			EXPECT_FALSE(xas.xa_node == XAS_RESTART);
		EXPECT_FALSE(xas.xa_index != i);
		if (i == 0 || i == idx)
			EXPECT_FALSE(entry != xa_mk_value(i));
		else
			EXPECT_FALSE(entry != NULL);
	}
	xas_next(&xas);
	EXPECT_FALSE(xas.xa_index != i);

	do {
		void *entry = xas_prev(&xas);
		i--;
		if (i <= idx)
			EXPECT_FALSE(xas.xa_node == XAS_RESTART);
		EXPECT_FALSE(xas.xa_index != i);
		if (i == 0 || i == idx)
			EXPECT_FALSE(entry != xa_mk_value(i));
		else
			EXPECT_FALSE(entry != NULL);
	} while (i > 0);

	xas_set(&xas, ULONG_MAX);
	EXPECT_FALSE(xas_next(&xas) != NULL);
	EXPECT_FALSE(xas.xa_index != ULONG_MAX);
	EXPECT_FALSE(xas_next(&xas) != xa_mk_value(0));
	EXPECT_FALSE(xas.xa_index != 0);
	EXPECT_FALSE(xas_prev(&xas) != NULL);
	EXPECT_FALSE(xas.xa_index != ULONG_MAX);
	rcu_read_unlock();

	xa_erase_index(self, xa, 0);
	xa_erase_index(self, xa, idx);
	EXPECT_FALSE(!xa_empty(xa));
}

TEST(test_xarray_rewrite, check_move) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	XA_STATE(xas, xa, (1 << 16) - 1);
	unsigned long i;

	for (i = 0; i < (1 << 16); i++)
		EXPECT_FALSE(xa_store_index(self, xa, i, GFP_KERNEL) != NULL);

	rcu_read_lock();
	do {
		void *entry = xas_prev(&xas);
		i--;
		EXPECT_FALSE(entry != xa_mk_value(i));
		EXPECT_FALSE(i != xas.xa_index);
	} while (i != 0);

	EXPECT_FALSE(xas_prev(&xas) != NULL);
	EXPECT_FALSE(xas.xa_index != ULONG_MAX);

	do {
		void *entry = xas_next(&xas);
		EXPECT_FALSE(entry != xa_mk_value(i));
		EXPECT_FALSE(i != xas.xa_index);
		i++;
	} while (i < (1 << 16));
	rcu_read_unlock();

	for (i = (1 << 8); i < (1 << 15); i++)
		xa_erase_index(self, xa, i);

	i = xas.xa_index;

	rcu_read_lock();
	do {
		void *entry = xas_prev(&xas);
		i--;
		if ((i < (1 << 8)) || (i >= (1 << 15)))
			EXPECT_FALSE(entry != xa_mk_value(i));
		else
			EXPECT_FALSE(entry != NULL);
		EXPECT_FALSE(i != xas.xa_index);
	} while (i != 0);

	EXPECT_FALSE(xas_prev(&xas) != NULL);
	EXPECT_FALSE(xas.xa_index != ULONG_MAX);

	do {
		void *entry = xas_next(&xas);
		if ((i < (1 << 8)) || (i >= (1 << 15)))
			EXPECT_FALSE(entry != xa_mk_value(i));
		else
			EXPECT_FALSE(entry != NULL);
		EXPECT_FALSE(i != xas.xa_index);
		i++;
	} while (i < (1 << 16));
	rcu_read_unlock();

	xa_destroy(xa);

	for (i = 0; i < 16; i++)
		check_move_small(self, xa, 1UL << i);

	for (i = 2; i < 16; i++)
		check_move_small(self, xa, (1UL << i) - 1);
}

static noinline void xa_store_many_order(struct ktf_test *self, struct xarray *xa,
		unsigned long index, unsigned order)
{
	XA_STATE_ORDER(xas, xa, index, order);
	unsigned int i = 0;

	do {
		xas_lock(&xas);
		EXPECT_FALSE(xas_find_conflict(&xas));
		xas_create_range(&xas);
		if (xas_error(&xas))
			goto unlock;
		for (i = 0; i < (1U << order); i++) {
			EXPECT_FALSE(xas_store(&xas, xa_mk_value(index + i)));
			xas_next(&xas);
		}
unlock:
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	EXPECT_FALSE(xas_error(&xas));
}

static noinline void check_create_range_1(struct ktf_test *self, struct xarray *xa,
		unsigned long index, unsigned order)
{
	unsigned long i;

	xa_store_many_order(self, xa, index, order);
	for (i = index; i < index + (1UL << order); i++)
		xa_erase_index(self, xa, i);
	EXPECT_FALSE(!xa_empty(xa));
}

static noinline void check_create_range_2(struct ktf_test *self, struct xarray *xa, unsigned order)
{
	unsigned long i;
	unsigned long nr = 1UL << order;

	for (i = 0; i < nr * nr; i += nr)
		xa_store_many_order(self, xa, i, order);
	for (i = 0; i < nr * nr; i++)
		xa_erase_index(self, xa, i);
	EXPECT_FALSE(!xa_empty(xa));
}

static noinline void check_create_range_3(struct ktf_test *self)
{
	XA_STATE(xas, NULL, 0);
	xas_set_err(&xas, -EEXIST);
	xas_create_range(&xas);
	EXPECT_FALSE(xas_error(&xas) != -EEXIST);
}

static noinline void check_create_range_4(struct ktf_test *self, struct xarray *xa,
		unsigned long index, unsigned order)
{
	XA_STATE_ORDER(xas, xa, index, order);
	unsigned long base = xas.xa_index;
	unsigned long i = 0;

	xa_store_index(self, xa, index, GFP_KERNEL);
	do {
		xas_lock(&xas);
		xas_create_range(&xas);
		if (xas_error(&xas))
			goto unlock;
		for (i = 0; i < (1UL << order); i++) {
			void *old = xas_store(&xas, xa_mk_value(base + i));
			if (xas.xa_index == index)
				EXPECT_FALSE(old != xa_mk_value(base + i));
			else
				EXPECT_FALSE(old != NULL);
			xas_next(&xas);
		}
unlock:
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	EXPECT_FALSE(xas_error(&xas));

	for (i = base; i < base + (1UL << order); i++)
		xa_erase_index(self, xa, i);
	EXPECT_FALSE(!xa_empty(xa));
}

TEST(test_xarray_rewrite, check_create_range) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	unsigned int order;
	unsigned int max_order = IS_ENABLED(CONFIG_XARRAY_MULTI) ? 12 : 1;

	for (order = 0; order < max_order; order++) {
		check_create_range_1(self, xa, 0, order);
		check_create_range_1(self, xa, 1U << order, order);
		check_create_range_1(self, xa, 2U << order, order);
		check_create_range_1(self, xa, 3U << order, order);
		check_create_range_1(self, xa, 1U << 24, order);
		if (order < 10)
			check_create_range_2(self, xa, order);

		check_create_range_4(self, xa, 0, order);
		check_create_range_4(self, xa, 1U << order, order);
		check_create_range_4(self, xa, 2U << order, order);
		check_create_range_4(self, xa, 3U << order, order);
		check_create_range_4(self, xa, 1U << 24, order);

		check_create_range_4(self, xa, 1, order);
		check_create_range_4(self, xa, (1U << order) + 1, order);
		check_create_range_4(self, xa, (2U << order) + 1, order);
		check_create_range_4(self, xa, (2U << order) - 1, order);
		check_create_range_4(self, xa, (3U << order) + 1, order);
		check_create_range_4(self, xa, (3U << order) - 1, order);
		check_create_range_4(self, xa, (1U << 24) + 1, order);
	}

	check_create_range_3(self);
}

static noinline void __check_store_range(struct ktf_test *self, struct xarray *xa, unsigned long first,
		unsigned long last)
{
#ifdef CONFIG_XARRAY_MULTI
	xa_store_range(xa, first, last, xa_mk_value(first), GFP_KERNEL);

	EXPECT_FALSE(xa_load(xa, first) != xa_mk_value(first));
	EXPECT_FALSE(xa_load(xa, last) != xa_mk_value(first));
	EXPECT_FALSE(xa_load(xa, first - 1) != NULL);
	EXPECT_FALSE(xa_load(xa, last + 1) != NULL);

	xa_store_range(xa, first, last, NULL, GFP_KERNEL);
#endif

	EXPECT_FALSE(!xa_empty(xa));
}

TEST(test_xarray_rewrite, check_store_range) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	unsigned long i, j;

	for (i = 0; i < 128; i++) {
		for (j = i; j < 128; j++) {
			__check_store_range(self, xa, i, j);
			__check_store_range(self, xa, 128 + i, 128 + j);
			__check_store_range(self, xa, 4095 + i, 4095 + j);
			__check_store_range(self, xa, 4096 + i, 4096 + j);
			__check_store_range(self, xa, 123456 + i, 123456 + j);
			__check_store_range(self, xa, UINT_MAX + i, UINT_MAX + j);
		}
	}
}

static LIST_HEAD(shadow_nodes);

static void test_update_node(struct xa_node *node)
{
	if (node->count && node->count == node->nr_values) {
		if (list_empty(&node->private_list))
			list_add(&shadow_nodes, &node->private_list);
	} else {
		if (!list_empty(&node->private_list))
			list_del_init(&node->private_list);
	}
}

static noinline void shadow_remove(struct ktf_test *self, struct xarray *xa)
{
	struct xa_node *node;

	xa_lock(xa);
	while ((node = list_first_entry_or_null(&shadow_nodes,
					struct xa_node, private_list))) {
		XA_STATE(xas, node->array, 0);
		EXPECT_FALSE(node->array != xa);
		list_del_init(&node->private_list);
		xas.xa_node = xa_parent_locked(node->array, node);
		xas.xa_offset = node->offset;
		xas.xa_shift = node->shift + XA_CHUNK_SHIFT;
		xas_set_update(&xas, test_update_node);
		xas_store(&xas, NULL);
	}
	xa_unlock(xa);
}

static noinline void check_workingset(struct ktf_test *self, struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	xas_set_update(&xas, test_update_node);

	do {
		xas_lock(&xas);
		xas_store(&xas, xa_mk_value(0));
		xas_next(&xas);
		xas_store(&xas, xa_mk_value(1));
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	EXPECT_FALSE(list_empty(&shadow_nodes));

	xas_lock(&xas);
	xas_next(&xas);
	xas_store(&xas, &xas);
	EXPECT_FALSE(!list_empty(&shadow_nodes));

	xas_store(&xas, xa_mk_value(2));
	xas_unlock(&xas);
	EXPECT_FALSE(list_empty(&shadow_nodes));

	shadow_remove(self, xa);
	EXPECT_FALSE(!list_empty(&shadow_nodes));
	EXPECT_FALSE(!xa_empty(xa));
}

/*
 * Check that the pointer / value / sibling entries are accounted the
 * way we expect them to be.
 */
TEST(test_xarray_rewrite, check_account) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


#ifdef CONFIG_XARRAY_MULTI
	unsigned int order;

	for (order = 1; order < 12; order++) {
		XA_STATE(xas, xa, 1 << order);

		xa_store_order(self, xa, 0, order, xa, GFP_KERNEL);
		xas_load(&xas);
		EXPECT_FALSE(xas.xa_node->count == 0);
		EXPECT_FALSE(xas.xa_node->count > (1 << order));
		EXPECT_FALSE(xas.xa_node->nr_values != 0);

		xa_store_order(self, xa, 1 << order, order, xa_mk_value(1 << order),
				GFP_KERNEL);
		EXPECT_FALSE(xas.xa_node->count != xas.xa_node->nr_values * 2);

		xa_erase(xa, 1 << order);
		EXPECT_FALSE(xas.xa_node->nr_values != 0);

		xa_erase(xa, 0);
		EXPECT_FALSE(!xa_empty(xa));
	}
#endif
}

TEST(test_xarray_rewrite, check_destroy) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;


	unsigned long index;

	EXPECT_FALSE(!xa_empty(xa));

	/* Destroying an empty array is a no-op */
	xa_destroy(xa);
	EXPECT_FALSE(!xa_empty(xa));

	/* Destroying an array with a single entry */
	for (index = 0; index < 1000; index++) {
		xa_store_index(self, xa, index, GFP_KERNEL);
		EXPECT_FALSE(xa_empty(xa));
		xa_destroy(xa);
		EXPECT_FALSE(!xa_empty(xa));
	}

	/* Destroying an array with a single entry at ULONG_MAX */
	xa_store(xa, ULONG_MAX, xa, GFP_KERNEL);
	EXPECT_FALSE(xa_empty(xa));
	xa_destroy(xa);
	EXPECT_FALSE(!xa_empty(xa));

#ifdef CONFIG_XARRAY_MULTI
	/* Destroying an array with a multi-index entry */
	xa_store_order(self, xa, 1 << 11, 11, xa, GFP_KERNEL);
	EXPECT_FALSE(xa_empty(xa));
	xa_destroy(xa);
	EXPECT_FALSE(!xa_empty(xa));
#endif
}

static DEFINE_XARRAY(array);

static struct array_context cxa = { .xa = &array };

KTF_INIT();

TEST(test_xarray_rewrite, check_workingset_1_) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;

	check_workingset(self, xa, 0);
}

TEST(test_xarray_rewrite, check_workingset_2_) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;

	check_workingset(self, xa, 64);
}

TEST(test_xarray_rewrite, check_workingset_3_) {
    struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;

	check_workingset(self, xa, 4096);
}

static int xarray_checks(void)
{
    	KTF_CONTEXT_ADD(&cxa.k, "array");

	ADD_TEST(check_xa_err);
	ADD_TEST(check_xas_retry);
	ADD_TEST(check_xa_load);
	ADD_TEST(check_xa_mark);
	ADD_TEST(check_xa_shrink);
	ADD_TEST(check_xas_erase);
	ADD_TEST(check_cmpxchg);
	ADD_TEST(check_reserve);
	ADD_TEST(check_multi_store);
	ADD_TEST(check_xa_alloc);
	ADD_TEST(check_find);
	ADD_TEST(check_find_entry);
	ADD_TEST(check_account);
	ADD_TEST(check_destroy);
	ADD_TEST(check_move);
	ADD_TEST(check_create_range);
	ADD_TEST(check_store_range);
	ADD_TEST(check_store_iter);

	ADD_TEST(check_workingset_1_);
	ADD_TEST(check_workingset_2_);
	ADD_TEST(check_workingset_3_);

	printk("XArray: %u of %u tests passed\n", tests_passed, tests_run);
	return (tests_run == tests_passed) ? 0 : -EINVAL;
}

static void xarray_exit(void)
{
    struct ktf_context *pctx = KTF_CONTEXT_FIND("array");
    KTF_CONTEXT_REMOVE(pctx);

    KTF_CLEANUP();
}

module_init(xarray_checks);
module_exit(xarray_exit);
MODULE_AUTHOR("Matthew Wilcox <willy@infradead.org>");
MODULE_LICENSE("GPL");