/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2014-2015 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**************************************************************************
 * Self Test
 **************************************************************************/

#include <linux/init.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/rhashtable.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/vmalloc.h>

/* KTF init kode */
#include "ktf.h"

KTF_INIT();

struct thread_self_pointer_ctx {
	struct ktf_context k;
	struct ktf_test *self;
};

static struct thread_self_pointer_ctx tsp;
/* ----- */

#define MAX_ENTRIES	1000000
#define TEST_INSERT_FAIL INT_MAX

static int parm_entries = 2500;
module_param(parm_entries, int, 0);
MODULE_PARM_DESC(parm_entries, "Number of entries to add (default: 50000)");

static int runs = 4;
module_param(runs, int, 0);
MODULE_PARM_DESC(runs, "Number of test runs per variant (default: 4)");

static int max_size = 0;
module_param(max_size, int, 0);
MODULE_PARM_DESC(max_size, "Maximum table size (default: calculated)");

static bool shrinking = false;
module_param(shrinking, bool, 0);
MODULE_PARM_DESC(shrinking, "Enable automatic shrinking (default: off)");

static int size = 8;
module_param(size, int, 0);
MODULE_PARM_DESC(size, "Initial size hint of table (default: 8)");

static int tcount = 10;
module_param(tcount, int, 0);
MODULE_PARM_DESC(tcount, "Number of threads to spawn (default: 10)");

static bool enomem_retry = false;
module_param(enomem_retry, bool, 0);
MODULE_PARM_DESC(enomem_retry, "Retry insert even if -ENOMEM was returned (default: off)");

struct test_obj_val {
	int	id;
	int	tid;
};

struct test_obj {
	struct test_obj_val	value;
	struct rhash_head	node;
};

struct test_obj_rhl {
	struct test_obj_val	value;
	struct rhlist_head	list_node;
};

struct thread_data {
	unsigned int entries;
	int id;
	struct task_struct *task;
	struct test_obj *objs;
};

static u32 my_hashfn(const void *data, u32 len, u32 seed)
{
	const struct test_obj_rhl *obj = data;

	return (obj->value.id % 10);
}

static int my_cmpfn(struct rhashtable_compare_arg *arg, const void *obj)
{
	const struct test_obj_rhl *test_obj = obj;
	const struct test_obj_val *val = arg->key;

	return test_obj->value.id - val->id;
}

static struct rhashtable_params test_rht_params = {
	.head_offset = offsetof(struct test_obj, node),
	.key_offset = offsetof(struct test_obj, value),
	.key_len = sizeof(struct test_obj_val),
	.hashfn = jhash,
};

static struct rhashtable_params test_rht_params_dup = {
	.head_offset = offsetof(struct test_obj_rhl, list_node),
	.key_offset = offsetof(struct test_obj_rhl, value),
	.key_len = sizeof(struct test_obj_val),
	.hashfn = jhash,
	.obj_hashfn = my_hashfn,
	.obj_cmpfn = my_cmpfn,
	.nelem_hint = 128,
	.automatic_shrinking = false,
};

static struct semaphore prestart_sem;
static struct semaphore startup_sem = __SEMAPHORE_INITIALIZER(startup_sem, 0);

static int insert_retry(struct rhashtable *ht, struct test_obj *obj,
                        const struct rhashtable_params params, struct ktf_test *self)
{
	int err, retries = -1;

	do {
		retries++;
		cond_resched();
        err = rhashtable_insert_fast(ht, &obj->node, params);
        ASSERT_FALSE_GOTO(err == -ENOMEM && enomem_retry, insert_retry_end);
        break;
    insert_retry_end:
        err = -EBUSY;

        /* Old code:
        
		err = rhashtable_insert_fast(ht, &obj->node, params);
		if (err == -ENOMEM && enomem_retry) {
			enomem_retries++;
			err = -EBUSY;
		}*/
	} while (err == -EBUSY);

	return err ? : retries;
}

static int test_rht_lookup(struct rhashtable *ht, struct test_obj *array,
				  unsigned int entries, struct ktf_test *self)
{
	unsigned int i;

	for (i = 0; i < entries; i++) {
		struct test_obj *obj;
		bool expected = !(i % 2);
		struct test_obj_val key = {
			.id = i,
		};

		if (array[i / 2].value.id == TEST_INSERT_FAIL)
			expected = false;

		obj = rhashtable_lookup_fast(ht, &key, test_rht_params);

		ASSERT_FALSE_RETVAL(expected && !obj, -ENOENT);
		ASSERT_FALSE_RETVAL(!expected && obj, -EEXIST);
		ASSERT_FALSE_RETVAL((expected && obj) && (obj->value.id != i), -EINVAL);

		cond_resched_rcu();
	}

	return 0;
}

static void test_bucket_stats(struct rhashtable *ht, unsigned int entries, struct ktf_test *self)
{
	unsigned int err, total = 0; //, chain_len = 0;
	struct rhashtable_iter hti;
	struct rhash_head *pos;

	err = rhashtable_walk_init(ht, &hti, GFP_KERNEL);
	ASSERT_FALSE(err);

	rhashtable_walk_start(&hti);

	while ((pos = rhashtable_walk_next(&hti))) {
		ASSERT_FALSE_CONT(PTR_ERR(pos) == -EAGAIN);
		ASSERT_FALSE_BREAK(IS_ERR(pos));

		total++;
	}

	rhashtable_walk_stop(&hti);
	rhashtable_walk_exit(&hti);

	EXPECT_TRUE(total == atomic_read(&ht->nelems));
	EXPECT_TRUE(total == entries);
}

static s64 test_rhashtable(struct rhashtable *ht, struct test_obj *array,
				  unsigned int entries, struct ktf_test *self)
{
	struct test_obj *obj;
	int err;
	unsigned int i, insert_retries = 0;
	s64 start, end;

	/*
	 * Insertion Test:
	 * Insert entries into table with all keys even numbers
	 */
	start = ktime_get_ns();
	for (i = 0; i < entries; i++) {
		struct test_obj *obj = &array[i];

		obj->value.id = i * 2;
		err = insert_retry(ht, obj, test_rht_params, self);
		if (err > 0)
			insert_retries += err;
		else
			ASSERT_FALSE_RETVAL(err, err);
	}

	test_bucket_stats(ht, entries, self);
	rcu_read_lock();
	test_rht_lookup(ht, array, entries, self);
	rcu_read_unlock();

	test_bucket_stats(ht, entries, self);

	for (i = 0; i < entries; i++) {
		struct test_obj_val key = {
			.id = i * 2,
		};

		if (array[i].value.id != TEST_INSERT_FAIL) {
			obj = rhashtable_lookup_fast(ht, &key, test_rht_params);
			ASSERT_TRUE(obj != NULL);

			rhashtable_remove_fast(ht, &obj->node, test_rht_params);
		}

		cond_resched();
	}

	end = ktime_get_ns();

	return end - start;
}

static struct rhashtable ht;
static struct rhltable rhlt;

static int test_rhltable(unsigned int entries, struct ktf_test *self)
{
	struct test_obj_rhl *rhl_test_objects;
	unsigned long *obj_in_table;
	unsigned int i, j, k;
	int ret, err;

	if (entries == 0)
		entries = 1;

	rhl_test_objects = vzalloc(array_size(entries,
					      sizeof(*rhl_test_objects)));
	ASSERT_TRUE_RETVAL((rhl_test_objects != NULL), -ENOMEM);

	ret = -ENOMEM;
	obj_in_table = vzalloc(array_size(sizeof(unsigned long),
					  BITS_TO_LONGS(entries)));
	ASSERT_TRUE_GOTO((obj_in_table != NULL), out_free);

	err = rhltable_init(&rhlt, &test_rht_params);
	ASSERT_FALSE_GOTO(err, out_free);

	k = prandom_u32();
	ret = 0;
	for (i = 0; i < entries; i++) {
		rhl_test_objects[i].value.id = k;
		err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node,
				      test_rht_params);
		ASSERT_FALSE_BREAK(err);
		if (err == 0)
			set_bit(i, obj_in_table);
	}

	if (err)
		ret = err;

	for (i = 0; i < entries; i++) {
		struct rhlist_head *h, *pos;
		struct test_obj_rhl *obj;
		struct test_obj_val key = {
			.id = k,
		};
		bool found;

		rcu_read_lock();
		h = rhltable_lookup(&rhlt, &key, test_rht_params);
		if (!h) {
			rcu_read_unlock();
			ASSERT_TRUE_BREAK((h != NULL));
		}

		if (i) {
			j = i - 1;
			rhl_for_each_entry_rcu(obj, pos, h, list_node) {
				ASSERT_FALSE_BREAK(pos == &rhl_test_objects[j].list_node);
			}
		}

		cond_resched_rcu();

		found = false;

		rhl_for_each_entry_rcu(obj, pos, h, list_node) {
			if (pos == &rhl_test_objects[i].list_node) {
				found = true;
				break;
			}
		}

		rcu_read_unlock();

		ASSERT_TRUE_BREAK(found);

		err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		EXPECT_FALSE(err);
		if (err == 0)
			clear_bit(i, obj_in_table);
	}

	if (ret == 0 && err)
		ret = err;

	for (i = 0; i < entries; i++) {
		EXPECT_FALSE(test_bit(i, obj_in_table));

		err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node,
				      test_rht_params);
		ASSERT_FALSE_BREAK(err);
		if (err == 0)
			set_bit(i, obj_in_table);
	}

	for (j = 0; j < entries; j++) {
		u32 i = prandom_u32_max(entries);
		u32 prand = prandom_u32();

		cond_resched();

		if (prand == 0)
			prand = prandom_u32();

		if (prand & 1) {
			prand >>= 1;
			continue;
		}

		err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		if (test_bit(i, obj_in_table)) {
			clear_bit(i, obj_in_table);
			ASSERT_FALSE_CONT(err);
		} else {
			ASSERT_TRUE_CONT(err == -ENOENT);
		}

		if (prand & 1) {
			prand >>= 1;
			continue;
		}

		err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		if (err == 0) {
			ASSERT_FALSE_CONT(test_and_set_bit(i, obj_in_table));
		} else {
			ASSERT_TRUE_CONT(test_bit(i, obj_in_table));
		}

		if (prand & 1) {
			prand >>= 1;
			continue;
		}

		i = prandom_u32_max(entries);
		if (test_bit(i, obj_in_table)) {
			err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
			EXPECT_FALSE(err);
			if (err == 0)
				clear_bit(i, obj_in_table);
		} else {
			err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
			EXPECT_FALSE(err);
			if (err == 0)
				set_bit(i, obj_in_table);
		}
	}

	for (i = 0; i < entries; i++) {
		cond_resched();
		err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		if (test_bit(i, obj_in_table)) {
			ASSERT_FALSE_CONT(err);
		} else {
			ASSERT_TRUE_CONT(err == -ENOENT);
		}
	}

	rhltable_destroy(&rhlt);
out_free:
	vfree(rhl_test_objects);
	vfree(obj_in_table);
	return ret;
}

static int test_rhashtable_max(struct test_obj *array,
				      unsigned int entries, struct ktf_test *self)
{
	unsigned int i, insert_retries = 0;
	int err;

	test_rht_params.max_size = roundup_pow_of_two(entries / 8);
	err = rhashtable_init(&ht, &test_rht_params);
	ASSERT_FALSE_RETVAL(err, err);

	for (i = 0; i < ht.max_elems; i++) {
		struct test_obj *obj = &array[i];

		obj->value.id = i * 2;
		err = insert_retry(&ht, obj, test_rht_params, self);
		if (err > 0)
			insert_retries += err;
		else
			ASSERT_FALSE_RETVAL(err, err);
	}

	err = insert_retry(&ht, &array[ht.max_elems], test_rht_params, self);
	if (err == -E2BIG) {
		err = 0;
	} else {
		if (err == 0)
			err = -1;
	}

	rhashtable_destroy(&ht);

	return err;
}

static unsigned int print_ht(struct rhltable *rhlt, struct ktf_test *self)
{
	struct rhashtable *ht;
	const struct bucket_table *tbl;
	char buff[512] = "";
	unsigned int i, cnt = 0;

	ht = &rhlt->ht;
	/* Take the mutex to avoid RCU warning */
	mutex_lock(&ht->mutex);
	tbl = rht_dereference(ht->tbl, ht);
	for (i = 0; i < tbl->size; i++) {
		struct rhash_head *pos, *next;
		struct test_obj_rhl *p;

		pos = rht_dereference(tbl->buckets[i], ht);
		next = !rht_is_a_nulls(pos) ? rht_dereference(pos->next, ht) : NULL;

		if (!rht_is_a_nulls(pos)) {
			sprintf(buff, "%s\nbucket[%d] -> ", buff, i);
		}

		while (!rht_is_a_nulls(pos)) {
			struct rhlist_head *list = container_of(pos, struct rhlist_head, rhead);
			sprintf(buff, "%s[[", buff);
			do {
				pos = &list->rhead;
				list = rht_dereference(list->next, ht);
				p = rht_obj(ht, pos);

				sprintf(buff, "%s val %d (tid=%d)%s", buff, p->value.id, p->value.tid,
					list? ", " : " ");
				cnt++;
			} while (list);

			pos = next,
			next = !rht_is_a_nulls(pos) ?
				rht_dereference(pos->next, ht) : NULL;

			sprintf(buff, "%s]]%s", buff, !rht_is_a_nulls(pos) ? " -> " : "");
		}
	}
	mutex_unlock(&ht->mutex);

	return cnt;
}

/* La til 'struct ktf_test *self' argument */
static int test_insert_dup(struct test_obj_rhl *rhl_test_objects,
				  int cnt, bool slow, struct ktf_test *self)
{
	struct rhltable rhlt;
	unsigned int i, ret;
	const char *key;
	int err = 0;

	err = rhltable_init(&rhlt, &test_rht_params_dup);
	ASSERT_FALSE_RETVAL(err, err);

	for (i = 0; i < cnt; i++) {
		rhl_test_objects[i].value.tid = i;
		key = rht_obj(&rhlt.ht, &rhl_test_objects[i].list_node.rhead);
		key += test_rht_params_dup.key_offset;

		if (slow) {
			err = PTR_ERR(rhashtable_insert_slow(&rhlt.ht, key,
							     &rhl_test_objects[i].list_node.rhead));
			if (err == -EAGAIN)
				err = 0;
		} else
			err = rhltable_insert(&rhlt,
					      &rhl_test_objects[i].list_node,
					      test_rht_params_dup);
		ASSERT_INT_EQ_GOTO(err, 0, skip_print);
	}

	ret = print_ht(&rhlt, self);
	EXPECT_TRUE(ret == cnt);

skip_print:
	rhltable_destroy(&rhlt);

	return 0;
}

static int test_insert_duplicates_run(struct ktf_test *self)
{
	struct test_obj_rhl rhl_test_objects[3] = {};

	/* two different values that map to same bucket */
	rhl_test_objects[0].value.id = 1;
	rhl_test_objects[1].value.id = 21;

	/* and another duplicate with same as [0] value
	 * which will be second on the bucket list */
	rhl_test_objects[2].value.id = rhl_test_objects[0].value.id;

	test_insert_dup(rhl_test_objects, 2, false, self);
	test_insert_dup(rhl_test_objects, 3, false, self);
	test_insert_dup(rhl_test_objects, 2, true, self);
	test_insert_dup(rhl_test_objects, 3, true, self);

	return 0;
}

static int thread_lookup_test(struct thread_data *tdata, struct ktf_test *self)
{
	unsigned int entries = tdata->entries;
	int i, err = 0;

	for (i = 0; i < entries; i++) {
		struct test_obj *obj;
		struct test_obj_val key = {
			.id = i,
			.tid = tdata->id,
		};

		obj = rhashtable_lookup_fast(&ht, &key, test_rht_params);
		ASSERT_FALSE_GOTO(obj && (tdata->objs[i].value.id == TEST_INSERT_FAIL), inc_err);
		ASSERT_FALSE_GOTO(!obj && (tdata->objs[i].value.id != TEST_INSERT_FAIL), inc_err);
		ASSERT_FALSE_GOTO(obj && memcmp(&obj->value, &key, sizeof(key)), inc_err);

		cond_resched();

		continue;
	inc_err:
		err++;
	}
	return err;
}

static int threadfunc(void *data)
{
	int i, step, err = 0; //, insert_retries = 0;
	struct thread_data *tdata = data;

	int max_iter, count;

	struct thread_self_pointer_ctx* tsp = KTF_CONTEXT_GET("thread_self", struct thread_self_pointer_ctx);
	struct ktf_test *self = tsp->self;

	up(&prestart_sem);
	EXPECT_INT_EQ(down_interruptible(&startup_sem), 0);

	for (i = 0; i < tdata->entries; i++) {
		tdata->objs[i].value.id = i;
		tdata->objs[i].value.tid = tdata->id;
		err = insert_retry(&ht, &tdata->objs[i], test_rht_params, self);
		EXPECT_INT_EQ(err, 0);
		ASSERT_INT_GE_GOTO(err, 0, out);
	}
	err = thread_lookup_test(tdata, self);
	ASSERT_INT_EQ_GOTO(err, 0, out);
	/*
	 * This counter is added, as the kernel crashes if the loop below is
	 * allowed to run for as long as it wishes!
	 */
	max_iter = 501;
	count = 0;

	for (step = 10; step > 0; step--) {
		for (i = 0; i < tdata->entries; i += step) {
			/* Check added for reasons explained above. */
			count++;
			if (count >= max_iter) {
				break;
			}

			ASSERT_FALSE_CONT(tdata->objs[i].value.id == TEST_INSERT_FAIL);
			ASSERT_INT_EQ_GOTO((err = rhashtable_remove_fast(&ht, &tdata->objs[i].node,
			                             test_rht_params)), 0, out);
			tdata->objs[i].value.id = TEST_INSERT_FAIL;

			cond_resched();
		}
		ASSERT_INT_EQ_GOTO(thread_lookup_test(tdata, self), 0, out);
	}
out:
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return err;
}

TEST(test_rht, test_rht_init2)
{
	unsigned int entries;
	int i, err, started_threads = 0; //, failed_threads = 0;
	u64 total_time = 0;
	struct thread_data *tdata;
	struct test_obj *objs;

	if (parm_entries < 0)
		parm_entries = 1;

	entries = min(parm_entries, MAX_ENTRIES);

	test_rht_params.automatic_shrinking = shrinking;
	test_rht_params.max_size = max_size ? : roundup_pow_of_two(entries);
	test_rht_params.nelem_hint = size;

	objs = vzalloc(array_size(sizeof(struct test_obj),
				  test_rht_params.max_size + 1));
    ASSERT_TRUE(objs != NULL);

	for (i = 0; i < runs; i++) {
		s64 time;

		memset(objs, 0, test_rht_params.max_size * sizeof(struct test_obj));

		err = rhashtable_init(&ht, &test_rht_params);
        ASSERT_FALSE_CONT(err < 0);

		time = test_rhashtable(&ht, objs, entries, self);
		rhashtable_destroy(&ht);

		if (time < 0) {
			vfree(objs);
			ASSERT_FALSE(time < 0);
		}

		total_time += time;
	}

	EXPECT_INT_EQ(test_rhashtable_max(objs, entries, self), 0);
	vfree(objs);

	do_div(total_time, runs);

	test_insert_duplicates_run(self);

    ASSERT_TRUE(tcount);

	sema_init(&prestart_sem, 1 - tcount);
	tdata = vzalloc(array_size(tcount, sizeof(struct thread_data)));
	ASSERT_TRUE(tdata != NULL);

	objs  = vzalloc(array3_size(sizeof(struct test_obj), tcount, entries));
	
	/* Not an ideal solution, but it works... */
    ASSERT_TRUE_GOTO((objs != NULL), _objs_null);
    /*
    if (!objs) {
		vfree(tdata);
		return -ENOMEM;
	}*/


	test_rht_params.max_size = max_size ? :
	                           roundup_pow_of_two(tcount * entries);
	err = rhashtable_init(&ht, &test_rht_params);
	if (err < 0) {
		EXPECT_INT_GE(err, 0);
		vfree(tdata);
		vfree(objs);
		return;
	}
	for (i = 0; i < tcount; i++) {
		tdata[i].id = i;
		tdata[i].entries = entries;
		tdata[i].objs = objs + i * entries;
		tdata[i].task = kthread_run(threadfunc, &tdata[i],
		                            "rhashtable_thrad[%d]", i);

		ASSERT_FALSE_CONT(IS_ERR(tdata[i].task));
		started_threads++;
	}
	if (down_interruptible(&prestart_sem))
		EXPECT_TRUE(0);
	for (i = 0; i < tcount; i++)
		up(&startup_sem);
	for (i = 0; i < tcount; i++) {
		ASSERT_FALSE_CONT(IS_ERR(tdata[i].task));
		err = kthread_stop(tdata[i].task);
		EXPECT_INT_EQ(err, 0);
	}
	rhashtable_destroy(&ht);
	vfree(tdata);
	vfree(objs);

	/*
	 * rhltable_remove is very expensive, default values can cause test
	 * to run for 2 minutes or more,  use a smaller number instead.
	 */
	err = test_rhltable(entries / 16, self);
	return;
_objs_null:
    vfree(tdata);
}

static int test_rht_init(void) {
	KTF_CONTEXT_ADD(&tsp.k, "thread_self");

	ADD_TEST(test_rht_init2);

	return 0;
}

static void __exit test_rht_exit(void)
{
	struct ktf_context *tctx = KTF_CONTEXT_FIND("thread_self");
	if (tctx) {
    	KTF_CONTEXT_REMOVE(tctx);
	}

	KTF_CLEANUP();
}

module_init(test_rht_init);
module_exit(test_rht_exit);

MODULE_LICENSE("GPL v2");
