#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ktf.h"

KTF_INIT();

TEST(test_string_rewrite, memset16_selftest)
{
	unsigned i, j, k;
	u16 v, *p;

	p = kmalloc(256 * 2 * 2, GFP_KERNEL);
	ASSERT_OK_ADDR(p);

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset16(p + i, 0xb1b2, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					ASSERT_FALSE_GOTO(v != 0xa1a1, fail);
				} else if (k < i + j) {
					ASSERT_FALSE_GOTO(v != 0xb1b2, fail);
				} else {
					ASSERT_FALSE_GOTO(v != 0xa1a1, fail);
				}
			}
		}
	}

fail:
	kfree(p);
}

TEST(test_string_rewrite, memset32_selftest)
{
	unsigned i, j, k;
	u32 v, *p;

	p = kmalloc(256 * 2 * 4, GFP_KERNEL);
	ASSERT_OK_ADDR(p);

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset32(p + i, 0xb1b2b3b4, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					ASSERT_FALSE_GOTO(v != 0xa1a1a1a1, fail);
				} else if (k < i + j) {
					ASSERT_FALSE_GOTO(v != 0xb1b2b3b4, fail);
				} else {
					ASSERT_FALSE_GOTO(v != 0xa1a1a1a1, fail);
				}
			}
		}
	}

fail:
	kfree(p);
}

TEST(test_string_rewrite, memset64_selftest)
{
	unsigned i, j, k;
	u64 v, *p;

	p = kmalloc(256 * 2 * 8, GFP_KERNEL);
	ASSERT_OK_ADDR(p);

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 256; j++) {
			memset(p, 0xa1, 256 * 2 * sizeof(v));
			memset64(p + i, 0xb1b2b3b4b5b6b7b8ULL, j);
			for (k = 0; k < 512; k++) {
				v = p[k];
				if (k < i) {
					ASSERT_FALSE_GOTO(v != 0xa1a1a1a1a1a1a1a1ULL, fail);
				} else if (k < i + j) {
					ASSERT_FALSE_GOTO(v != 0xb1b2b3b4b5b6b7b8ULL, fail);
				} else {
					ASSERT_FALSE_GOTO(v != 0xa1a1a1a1a1a1a1a1ULL, fail);
				}
			}
		}
	}

fail:
	kfree(p);
}

static __init int string_selftest_init(void)
{
	ADD_TEST(memset16_selftest);
	ADD_TEST(memset32_selftest);
	ADD_TEST(memset64_selftest);

	return 0;
}

static void __exit string_selftest_exit(void)
{
	KTF_CLEANUP();
}

module_init(string_selftest_init);
module_exit(string_selftest_exit);
MODULE_LICENSE("GPL v2");
