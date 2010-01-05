/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "splat-internal.h"

#define SPLAT_TASKQ_NAME		"taskq"
#define SPLAT_TASKQ_DESC		"Kernel Task Queue Tests"

#define SPLAT_TASKQ_TEST1_ID		0x0201
#define SPLAT_TASKQ_TEST1_NAME		"single"
#define SPLAT_TASKQ_TEST1_DESC		"Single task queue, single task"

#define SPLAT_TASKQ_TEST2_ID		0x0202
#define SPLAT_TASKQ_TEST2_NAME		"multiple"
#define SPLAT_TASKQ_TEST2_DESC		"Multiple task queues, multiple tasks"

#define SPLAT_TASKQ_TEST3_ID		0x0203
#define SPLAT_TASKQ_TEST3_NAME		"system"
#define SPLAT_TASKQ_TEST3_DESC		"System task queue, multiple tasks"

#define SPLAT_TASKQ_TEST4_ID		0x0204
#define SPLAT_TASKQ_TEST4_NAME		"wait"
#define SPLAT_TASKQ_TEST4_DESC		"Multiple task waiting"

#define SPLAT_TASKQ_TEST5_ID		0x0205
#define SPLAT_TASKQ_TEST5_NAME		"order"
#define SPLAT_TASKQ_TEST5_DESC		"Correct task ordering"

#define SPLAT_TASKQ_ORDER_MAX		8

typedef struct splat_taskq_arg {
	int flag;
	int id;
	atomic_t count;
	int order[SPLAT_TASKQ_ORDER_MAX];
	spinlock_t lock;
	struct file *file;
	const char *name;
} splat_taskq_arg_t;

typedef struct splat_taskq_id {
	int id;
	splat_taskq_arg_t *arg;
} splat_taskq_id_t;

/*
 * Create a taskq, queue a task, wait until task completes, ensure
 * task ran properly, cleanup taskq.
 */
static void
splat_taskq_test13_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' setting flag\n",
	           tq_arg->name, sym2str(splat_taskq_test13_func));
	tq_arg->flag = 1;
}

static int
splat_taskq_test1(struct file *file, void *arg)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' creating\n",
	           SPLAT_TASKQ_TEST1_NAME);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST1_NAME, 1, maxclsyspri,
			       50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		           "Taskq '%s' create failed\n",
		           SPLAT_TASKQ_TEST1_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	tq_arg.id   = 0;
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST1_NAME;

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' dispatching\n",
	           tq_arg.name, sym2str(splat_taskq_test13_func));
	if ((id = taskq_dispatch(tq, splat_taskq_test13_func,
				 &tq_arg, TQ_SLEEP)) == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		           "Taskq '%s' function '%s' dispatch failed\n",
		           tq_arg.name, sym2str(splat_taskq_test13_func));
		taskq_destroy(tq);
		return -EINVAL;
	}

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' waiting\n",
	           tq_arg.name);
	taskq_wait(tq);
	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);
	taskq_destroy(tq);

	return (tq_arg.flag) ? 0 : -EINVAL;
}

/*
 * Create multiple taskq's, each with multiple tasks, wait until
 * all tasks complete, ensure all tasks ran properly and in the
 * correct order.  Run order must be the same as the order submitted
 * because we only have 1 thread per taskq.  Finally cleanup the taskq.
 */
static void
splat_taskq_test2_func1(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d * 2\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(splat_taskq_test2_func1),
	           tq_arg->flag * 2, tq_arg->flag);
	tq_arg->flag *= 2;
}

static void
splat_taskq_test2_func2(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d + 1\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(splat_taskq_test2_func2),
	           tq_arg->flag + 1, tq_arg->flag);
	tq_arg->flag += 1;
}

#define TEST2_TASKQS                    8
#define TEST2_THREADS_PER_TASKQ         1

static int
splat_taskq_test2(struct file *file, void *arg) {
	taskq_t *tq[TEST2_TASKQS] = { NULL };
	taskqid_t id;
	splat_taskq_arg_t tq_args[TEST2_TASKQS];
	int i, rc = 0;

	for (i = 0; i < TEST2_TASKQS; i++) {

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME, "Taskq '%s/%d' "
		           "creating\n", SPLAT_TASKQ_TEST2_NAME, i);
		if ((tq[i] = taskq_create(SPLAT_TASKQ_TEST2_NAME,
			                  TEST2_THREADS_PER_TASKQ,
					  maxclsyspri, 50, INT_MAX,
					  TASKQ_PREPOPULATE)) == NULL) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' create failed\n",
				   SPLAT_TASKQ_TEST2_NAME, i);
			rc = -EINVAL;
			break;
		}

		tq_args[i].flag = i;
		tq_args[i].id   = i;
		tq_args[i].file = file;
		tq_args[i].name = SPLAT_TASKQ_TEST2_NAME;

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
			   tq_args[i].name, tq_args[i].id,
		           sym2str(splat_taskq_test2_func1));
		if ((id = taskq_dispatch(
		     tq[i], splat_taskq_test2_func1,
		     &tq_args[i], TQ_SLEEP)) == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' function '%s' dispatch "
			           "failed\n", tq_args[i].name, tq_args[i].id,
			           sym2str(splat_taskq_test2_func1));
			rc = -EINVAL;
			break;
		}

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
			   tq_args[i].name, tq_args[i].id,
		           sym2str(splat_taskq_test2_func2));
		if ((id = taskq_dispatch(
		     tq[i], splat_taskq_test2_func2,
		     &tq_args[i], TQ_SLEEP)) == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' function '%s' dispatch failed\n",
			           tq_args[i].name, tq_args[i].id,
			           sym2str(splat_taskq_test2_func2));
			rc = -EINVAL;
			break;
		}
	}

	/* When rc is set we're effectively just doing cleanup here, so
	 * ignore new errors in that case.  They just cause noise. */
	for (i = 0; i < TEST2_TASKQS; i++) {
		if (tq[i] != NULL) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' waiting\n",
			           tq_args[i].name, tq_args[i].id);
			taskq_wait(tq[i]);
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d; destroying\n",
			          tq_args[i].name, tq_args[i].id);
			taskq_destroy(tq[i]);

			if (!rc && tq_args[i].flag != ((i * 2) + 1)) {
				splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
				           "out of order; %d != %d\n",
				           tq_args[i].name, tq_args[i].id,
				           tq_args[i].flag, i * 2 + 1);
				rc = -EINVAL;
			} else {
				splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
					   "in the correct order; %d == %d\n",
				           tq_args[i].name, tq_args[i].id,
				           tq_args[i].flag, i * 2 + 1);
			}
		}
	}

	return rc;
}

/*
 * Use the global system task queue with a single task, wait until task
 * completes, ensure task ran properly.
 */
static int
splat_taskq_test3(struct file *file, void *arg)
{
	taskqid_t id;
	splat_taskq_arg_t tq_arg;

	tq_arg.flag = 0;
	tq_arg.id   = 0;
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST3_NAME;

	splat_vprint(file, SPLAT_TASKQ_TEST3_NAME,
	           "Taskq '%s' function '%s' dispatching\n",
	           tq_arg.name, sym2str(splat_taskq_test13_func));
	if ((id = taskq_dispatch(system_taskq, splat_taskq_test13_func,
				 &tq_arg, TQ_SLEEP)) == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST3_NAME,
		           "Taskq '%s' function '%s' dispatch failed\n",
		           tq_arg.name, sym2str(splat_taskq_test13_func));
		return -EINVAL;
	}

	splat_vprint(file, SPLAT_TASKQ_TEST3_NAME, "Taskq '%s' waiting\n",
	           tq_arg.name);
	taskq_wait(system_taskq);

	return (tq_arg.flag) ? 0 : -EINVAL;
}

/*
 * Create a taskq and dispatch a large number of tasks to the queue.
 * Then use taskq_wait() to block until all the tasks complete, then
 * cross check that all the tasks ran by checking tg_arg->count which
 * is incremented in the task function.  Finally cleanup the taskq.
 */
static void
splat_taskq_test4_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	ASSERT(tq_arg);

	atomic_inc(&tq_arg->count);
}

static int
splat_taskq_test4(struct file *file, void *arg)
{
	taskq_t *tq;
	splat_taskq_arg_t tq_arg;
	int i, j, rc = 0;

	splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' creating\n",
	             SPLAT_TASKQ_TEST4_NAME);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST4_NAME, 1, maxclsyspri,
		               50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST4_NAME);
		return -EINVAL;
	}

	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST4_NAME;

	for (i = 1; i <= 1024; i *= 2) {
		atomic_set(&tq_arg.count, 0);
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		             "Taskq '%s' function '%s' dispatched %d times\n",
		             tq_arg.name, sym2str(splat_taskq_test4_func), i);

		for (j = 0; j < i; j++) {
			if ((taskq_dispatch(tq, splat_taskq_test4_func,
			                    &tq_arg, TQ_SLEEP)) == 0) {
				splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
				        "Taskq '%s' function '%s' dispatch "
					"%d failed\n", tq_arg.name,
					sym2str(splat_taskq_test13_func), j);
					rc = -EINVAL;
					goto out;
			}
		}

		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' "
			     "waiting for %d dispatches\n", tq_arg.name, i);
		taskq_wait(tq);
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' "
			     "%d/%d dispatches finished\n", tq_arg.name,
			     atomic_read(&tq_arg.count), i);
		if (atomic_read(&tq_arg.count) != i) {
			rc = -ERANGE;
			goto out;

		}
	}
out:
	splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);
	taskq_destroy(tq);

	return rc;
}

/*
 * Create a taskq and dispatch a specific sequence of tasks carefully
 * crafted to validate the order in which tasks are processed.  When
 * there are multiple worker threads each thread will process the
 * next pending task as soon as it completes its current task.  This
 * means that tasks do not strictly complete in order in which they
 * were dispatched (increasing task id).  This is fine but we need to
 * verify that taskq_wait_id() blocks until the passed task id and all
 * lower task ids complete.  We do this by dispatching the following
 * specific sequence of tasks each of which block for N time units.
 * We then use taskq_wait_id() to unblock at specific task id and
 * verify the only the expected task ids have completed and in the
 * correct order.  The two cases of interest are:
 *
 * 1) Task ids larger than the waited for task id can run and
 *    complete as long as there is an available worker thread.
 * 2) All task ids lower than the waited one must complete before
 *    unblocking even if the waited task id itself has completed.
 *
 * The following table shows each task id and how they will be
 * scheduled.  Each rows represent one time unit and each column
 * one of the three worker threads.  The places taskq_wait_id()
 * must unblock for a specific id are identified as well as the
 * task ids which must have completed and their order.
 *
 *       +-----+       <--- taskq_wait_id(tq, 8) unblocks
 *       |     |            Required Completion Order: 1,2,4,5,3
 * +-----+     |
 * |     |     |
 * |     |     +-----+
 * |     |     |  8  |
 * |     |     +-----+ <--- taskq_wait_id(tq, 3) unblocks
 * |     |  7  |     |      Required Completion Order: 1,2,4,5,3,8,6,7
 * |     +-----+     |
 * |  6  |     |     |
 * +-----+     |     |
 * |     |  5  |     |
 * |     +-----+     |
 * |  4  |     |     |
 * +-----+     |     |
 * |  1  |  2  |  3  |
 * +-----+-----+-----+
 *
 */
static void
splat_taskq_test5_func(void *arg)
{
	splat_taskq_id_t *tq_id = (splat_taskq_id_t *)arg;
	splat_taskq_arg_t *tq_arg = tq_id->arg;
	int factor;

	/* Delays determined by above table */
	switch (tq_id->id) {
		default:		factor = 0;	break;
		case 1: case 8:		factor = 1;	break;
		case 2: case 4: case 5:	factor = 2;	break;
		case 6: case 7:		factor = 4;	break;
		case 3:			factor = 5;	break;
	}

	msleep(factor * 100);
	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskqid %d complete for taskq '%s'\n",
		     tq_id->id, tq_arg->name);

	spin_lock(&tq_arg->lock);
	tq_arg->order[tq_arg->flag] = tq_id->id;
	tq_arg->flag++;
	spin_unlock(&tq_arg->lock);
}

static int
splat_taskq_test5_order(splat_taskq_arg_t *tq_arg, int *order)
{
	int i, j;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		if (tq_arg->order[i] != order[i]) {
			splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST5_NAME,
				     "Taskq '%s' incorrect completion "
				     "order\n", tq_arg->name);
			splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST5_NAME,
				     "%s", "Expected { ");

			for (j = 0; j < SPLAT_TASKQ_ORDER_MAX; j++)
				splat_print(tq_arg->file, "%d ", order[j]);

			splat_print(tq_arg->file, "%s", "}\n");
			splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST5_NAME,
				     "%s", "Got      { ");

			for (j = 0; j < SPLAT_TASKQ_ORDER_MAX; j++)
				splat_print(tq_arg->file, "%d ",
					    tq_arg->order[j]);

			splat_print(tq_arg->file, "%s", "}\n");
			return -EILSEQ;
		}
	}

	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST5_NAME,
		     "Taskq '%s' validated correct completion order\n",
		     tq_arg->name);

	return 0;
}

static int
splat_taskq_test5(struct file *file, void *arg)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_id_t tq_id[SPLAT_TASKQ_ORDER_MAX];
	splat_taskq_arg_t tq_arg;
	int order1[SPLAT_TASKQ_ORDER_MAX] = { 1,2,4,5,3,0,0,0 };
	int order2[SPLAT_TASKQ_ORDER_MAX] = { 1,2,4,5,3,8,6,7 };
	int i, rc = 0;

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME, "Taskq '%s' creating\n",
		     SPLAT_TASKQ_TEST5_NAME);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST5_NAME, 3, maxclsyspri,
		               50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST5_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	memset(&tq_arg.order, 0, sizeof(int) * SPLAT_TASKQ_ORDER_MAX);
	spin_lock_init(&tq_arg.lock);
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST5_NAME;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		tq_id[i].id = i + 1;
		tq_id[i].arg = &tq_arg;

		if ((id = taskq_dispatch(tq, splat_taskq_test5_func,
		                         &tq_id[i], TQ_SLEEP)) == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
			        "Taskq '%s' function '%s' dispatch failed\n",
				tq_arg.name, sym2str(splat_taskq_test5_func));
				rc = -EINVAL;
				goto out;
		}

		if (tq_id[i].id != id) {
			splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
			        "Taskq '%s' expected taskqid %d got %d\n",
				tq_arg.name, (int)tq_id[i].id, (int)id);
				rc = -EINVAL;
				goto out;
		}
	}

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name, 3);
	taskq_wait_id(tq, 3);
	if ((rc = splat_taskq_test5_order(&tq_arg, order1)))
		goto out;

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name, 8);
	taskq_wait_id(tq, 8);
	rc = splat_taskq_test5_order(&tq_arg, order2);

out:
	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		     "Taskq '%s' destroying\n", tq_arg.name);
	taskq_destroy(tq);

	return rc;
}

splat_subsystem_t *
splat_taskq_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_TASKQ_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_TASKQ_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
	spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_TASKQ;

	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST1_NAME, SPLAT_TASKQ_TEST1_DESC,
	              SPLAT_TASKQ_TEST1_ID, splat_taskq_test1);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST2_NAME, SPLAT_TASKQ_TEST2_DESC,
	              SPLAT_TASKQ_TEST2_ID, splat_taskq_test2);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST3_NAME, SPLAT_TASKQ_TEST3_DESC,
	              SPLAT_TASKQ_TEST3_ID, splat_taskq_test3);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST4_NAME, SPLAT_TASKQ_TEST4_DESC,
	              SPLAT_TASKQ_TEST4_ID, splat_taskq_test4);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST5_NAME, SPLAT_TASKQ_TEST5_DESC,
	              SPLAT_TASKQ_TEST5_ID, splat_taskq_test5);

        return sub;
}

void
splat_taskq_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST5_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST4_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST3_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST2_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST1_ID);

        kfree(sub);
}

int
splat_taskq_id(void) {
        return SPLAT_SUBSYSTEM_TASKQ;
}
