/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#include <sys/zfs_context.h>
#include <sys/txg.h>
#include <sys/zil.h>
#include <sys/uzfs_zvol.h>
#include <uzfs_mgmt.h>
#include <uzfs_mtree.h>
#include <uzfs_io.h>
#include <uzfs_test.h>

extern int total_time_in_sec;
extern void populate_data(char *buf, uint64_t offset, int idx,
    uint64_t block_size);
extern uint64_t block_size;

static int del_from_mblktree(avl_tree_t *tree, uint64_t b_offset,
    uint64_t b_len);
static int uzfs_search_mblktree(avl_tree_t *tree, uint64_t offset,
    uint64_t *len);

typedef struct wblkinfo {
	uint64_t offset;
	uint64_t len;
	list_node_t link;
} wblkinfo_t;

int
del_from_mblktree(avl_tree_t *tree, uint64_t offset, uint64_t len)
{
	uint64_t new_offset, new_len, b_end, b_offset, b_len;
	uint64_t entry_len, entry_offset;
	uzfs_zvol_blk_phy_t *entry, *new_entry, *b_entry;
	uzfs_zvol_blk_phy_t f_entry;
	avl_index_t where;
	int err = 0;

	new_offset = offset;
	new_len = len;

	f_entry.offset = new_offset;
	f_entry.len = new_len;
	entry = avl_find(tree, &f_entry, &where);

	if (entry != NULL) {
		if (entry->len < new_len) {
			err = -1;
			goto done;
		}

		entry_offset = entry->offset;
		entry_len = entry->len;
		avl_remove(tree, entry);
		umem_free(entry, sizeof (*entry));

		if (entry_len > new_len) {
			new_entry = umem_alloc(sizeof (uzfs_zvol_blk_phy_t),
			    UMEM_NOFAIL);
			new_entry->offset = entry_offset + new_len;
			new_entry->len = (entry_len - new_len);
			avl_add(tree, new_entry);
		}
		goto done;
	}

	b_entry = avl_nearest(tree, where, AVL_BEFORE);
	if (b_entry) {
		b_end = (b_entry->offset + b_entry->len);
		if (b_end < (new_offset + new_len)) {
			err = -1;
			goto done;
		}

		b_offset = b_entry->offset;
		b_len = b_entry->len;
		avl_remove(tree, b_entry);

		umem_free(b_entry, sizeof (*b_entry));

		new_entry = umem_alloc(sizeof (uzfs_zvol_blk_phy_t),
		    UMEM_NOFAIL);
		new_entry->offset = b_offset;
		new_entry->len = (new_offset - b_offset);
		avl_add(tree, new_entry);

		if (b_end > (new_offset + new_len)) {
			new_entry = umem_alloc(sizeof (uzfs_zvol_blk_phy_t),
			    UMEM_NOFAIL);
			new_entry->offset = new_offset + new_len;
			new_entry->len = (b_end - new_entry->offset);
			avl_add(tree, new_entry);
		}
		goto done;
	} else {
		err = -1;
		goto done;
	}

done:
	return (err);
}

int
uzfs_search_mblktree(avl_tree_t *tree, uint64_t offset, uint64_t *len)
{
	uzfs_zvol_blk_phy_t tofind;
	avl_index_t where;
	uzfs_zvol_blk_phy_t *entry;

	tofind.offset = offset;
	tofind.len = 0;

	entry = avl_find(tree, &tofind, &where);
	if (entry == NULL)
		return (0);

	*len = entry->len;
	return (1);
}

void
uzfs_zvol_txg_diff_blk_test(void *arg)
{
	uzfs_test_info_t *test_info = (uzfs_test_info_t *)arg;
	avl_tree_t *tree;
	uint64_t first_txg, last_txg;
	hrtime_t end, now;
	uint64_t blk_offset, offset, vol_blocks;
	uint64_t blksz = io_block_size, io_num = 0;
	void *spa, *zvol;
	char *buf;
	int diff_txg = 5, count, i = 0;
	list_t wlist;
	wblkinfo_t *blk;

	setup_unit_test();
	unit_test_create_pool_ds();
	open_pool_ds(&spa, &zvol);

	vol_blocks = active_size / blksz;
	buf = umem_alloc(block_size, UMEM_NOFAIL);

	list_create(&wlist, sizeof (wblkinfo_t), offsetof(wblkinfo_t, link));

	now = gethrtime();
	end = now + (hrtime_t)(total_time_in_sec * (hrtime_t)(NANOSEC));

	while (i++ < test_iterations) {
		count = 0;

		txg_wait_synced(spa_get_dsl(spa), 0);
		first_txg  = spa_last_synced_txg(spa);

		while (count++ < diff_txg) {
			io_num++;
			blk_offset = uzfs_random(vol_blocks - 16);
			offset = ((blk_offset * blksz + block_size) /
			    block_size) * block_size;

			populate_data(buf, offset, 0, block_size);

			if (uzfs_write_data(zvol, buf, offset, block_size,
			    &io_num, B_FALSE))
				printf("IO error at offset: %lu len: %lu\n",
				    offset, block_size);

			blk = umem_alloc(sizeof (wblkinfo_t), UMEM_NOFAIL);
			blk->offset = offset;
			blk->len = block_size;
			list_insert_tail(&wlist, blk);
			blk = NULL;
		}

		txg_wait_synced(spa_get_dsl(spa), 0);
		last_txg = spa_last_synced_txg(spa);

		uzfs_txg_block_diff(zvol, first_txg, last_txg, (void **)&tree);

		while ((blk = list_remove_head(&wlist))) {
			VERIFY0(del_from_mblktree(tree, blk->offset, blk->len));
			umem_free(blk, sizeof (*blk));
			blk = NULL;
		}
		VERIFY0(avl_numnodes(tree));
		printf("%s pass:%d\n", test_info->name, i);
		umem_free(tree, sizeof (*tree));
		tree = NULL;
	}

	uzfs_close_dataset(zvol);
	uzfs_close_pool(spa);
	list_destroy(&wlist);
	umem_free(buf, block_size);
}

static void
check_tree(avl_tree_t *tree, uint64_t offset, uint64_t len, uint64_t exp_off,
    uint64_t exp_len, int exp_ret)
{
	int ret;
	uint64_t len1 = 0;

	ret = uzfs_search_mblktree(tree, exp_off, &len1);

	VERIFY(ret == exp_ret);

	if (ret)
		VERIFY(exp_len == len1);
}

static void
add_and_check_tree(avl_tree_t *tree, uint64_t offset, uint64_t len,
    uint64_t exp_off, uint64_t exp_len, int exp_ret)
{
	add_to_mblktree(tree, offset, len);
	check_tree(tree, offset, len, exp_off, exp_len, exp_ret);
}

static void
delete_and_check_tree(avl_tree_t *tree, uint64_t offset, uint64_t len,
    uint64_t exp_off, uint64_t exp_len, int exp_ret)
{
	del_from_mblktree(tree, offset, len);
	check_tree(tree, offset, len, exp_off, exp_len, exp_ret);
}

void
uzfs_zvol_txg_mtree_test(void *arg)
{
	uzfs_test_info_t *test_info = (uzfs_test_info_t *)arg;
	avl_tree_t *tree;
	uint64_t blksz = io_block_size;

	uzfs_create_mblktree((void **)&tree);

	add_and_check_tree(tree, 100, 50, 100, 50, 1);
	add_and_check_tree(tree, 150, 50, 100, 100, 1);
	add_and_check_tree(tree, 5 * blksz, blksz, 5 * blksz, blksz, 1);
	add_and_check_tree(tree, 4 * blksz, blksz, 4 * blksz,
	    2 * blksz, 1);
	check_tree(tree, 4 * blksz, blksz, 5 * blksz, blksz, 0);
	add_and_check_tree(tree, 2 * blksz, blksz, 2 * blksz, blksz, 1);
	check_tree(tree, 2 * blksz, blksz, 4 * blksz, 2 * blksz, 1);
	check_tree(tree, 2 * blksz, blksz, 5 * blksz, blksz, 0);
	add_and_check_tree(tree, 2 * blksz, blksz, 2 * blksz, blksz, 1);
	check_tree(tree, 2 * blksz, blksz, 4 * blksz, 2 * blksz, 1);
	check_tree(tree, 2 * blksz, blksz, 5 * blksz, blksz, 0);
	add_and_check_tree(tree, blksz, blksz, blksz, 2 * blksz, 1);
	check_tree(tree, blksz, blksz, 2 * blksz, blksz, 0);
	check_tree(tree, blksz, blksz, 4 * blksz, 2 * blksz, 1);
	check_tree(tree, blksz, blksz, 5 * blksz, blksz, 0);
	add_and_check_tree(tree, 3 * blksz, blksz, blksz, 5 * blksz, 1);
	check_tree(tree, 3 * blksz, blksz, 2 * blksz, blksz, 0);
	check_tree(tree, 3 * blksz, blksz, 3 * blksz, blksz, 0);
	check_tree(tree, 3 * blksz, blksz, 4 * blksz, 2 * blksz, 0);
	check_tree(tree, 3 * blksz, blksz, 5 * blksz, blksz, 0);
	add_and_check_tree(tree, blksz, blksz, blksz, 5 * blksz, 1);
	check_tree(tree, blksz, blksz, 2 * blksz, blksz, 0);
	check_tree(tree, blksz, blksz, 3 * blksz, blksz, 0);
	check_tree(tree, blksz, blksz, 4 * blksz, 2 * blksz, 0);
	check_tree(tree, blksz, blksz, 5 * blksz, blksz, 0);
	add_and_check_tree(tree, 3 * blksz, blksz, blksz, 5 * blksz, 1);
	check_tree(tree, 3 * blksz, blksz, 2 * blksz, blksz, 0);
	check_tree(tree, 3 * blksz, blksz, 3 * blksz, blksz, 0);
	check_tree(tree, 3 * blksz, blksz, 4 * blksz, 2 * blksz, 0);
	check_tree(tree, 3 * blksz, blksz, 5 * blksz, blksz, 0);
	delete_and_check_tree(tree, blksz, blksz, 2 * blksz,
	    4 * blksz, 1);
	check_tree(tree, blksz, blksz, blksz, blksz, 0);
	check_tree(tree, blksz, blksz, 3 * blksz, blksz, 0);
	check_tree(tree, blksz, blksz, 4 * blksz, 2 * blksz, 0);
	check_tree(tree, blksz, blksz, 5 * blksz, blksz, 0);
	delete_and_check_tree(tree, 2 * blksz, blksz, 3 * blksz,
	    3 * blksz, 1);
	check_tree(tree, 2 * blksz, blksz, blksz, blksz, 0);
	check_tree(tree, 2 * blksz, blksz, 2 * blksz, blksz, 0);
	check_tree(tree, 2 * blksz, blksz, 4 * blksz, 2 * blksz, 0);
	check_tree(tree, 2 * blksz, blksz, 5 * blksz, blksz, 0);
	delete_and_check_tree(tree, 110, 10, 100, 10, 1);
	check_tree(tree, 120, 30, 120, 80, 1);
	add_and_check_tree(tree, 60, 20, 50, 0, 0);
	add_and_check_tree(tree, 60, 20, 60, 20, 1);
	delete_and_check_tree(tree, 60, 10, 70, 10, 1);
	add_and_check_tree(tree, 80, 20, 70, 40, 1);
	add_and_check_tree(tree, 80, 20, 90, 0, 0);
	add_and_check_tree(tree, 80, 20, 100, 0, 0);
	add_and_check_tree(tree, 200, 50, 200, 0, 0);
	add_and_check_tree(tree, 200, 50, 70, 40, 1);
	delete_and_check_tree(tree, 230, 20, 200, 0, 0);
	add_and_check_tree(tree, 40, 45, 40, 70, 1);
	add_and_check_tree(tree, 40, 45, 70, 40, 0);
	add_and_check_tree(tree, 150, 50, 120, 110, 1);
	add_and_check_tree(tree, 130, 140, 120, 150, 1);
	add_and_check_tree(tree, 30, 270, 30, 270, 1);
	add_and_check_tree(tree, 30, 270, 40, 0, 0);
	add_and_check_tree(tree, 130, 140, 130, 140, 0);
	add_and_check_tree(tree, 130, 140, 120, 150, 0);

	uzfs_destroy_mblktree((void *)tree);
	printf("%s pass\n", test_info->name);
}
