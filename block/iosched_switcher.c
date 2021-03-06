/*
 * Copyright (C) 2017, Sultanxda <sultanxda@gmail.com>
 *           (C) 2017, Pranav Vashi <neobuddy89@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "iosched-swch: " fmt

#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/elevator.h>
#include <linux/state_notifier.h>

#define NOOP_IOSCHED "noop"
#define RESTORE_DELAY_MS (10000)

struct req_queue_data {
	struct list_head list;
	struct request_queue *queue;
	char prev_e[ELV_NAME_MAX];
	bool using_noop;
};

static struct delayed_work restore_prev;
static DEFINE_SPINLOCK(init_lock);
static struct req_queue_data req_queues = {
	.list = LIST_HEAD_INIT(req_queues.list),
};

static struct notifier_block notif;

static void change_elevator(struct req_queue_data *r, bool use_noop)
{
	struct request_queue *q = r->queue;

	if (r->using_noop == use_noop)
		return;

	r->using_noop = use_noop;

	if (use_noop) {
		strcpy(r->prev_e, q->elevator->type->elevator_name);
		elevator_change(q, NOOP_IOSCHED);
	} else {
		elevator_change(q, r->prev_e);
	}
}

static void change_all_elevators(struct list_head *head, bool use_noop)
{
	struct req_queue_data *r;

	list_for_each_entry(r, head, list)
		change_elevator(r, use_noop);
}


static int state_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
		case STATE_NOTIFIER_ACTIVE:
			/*
			 * Switch back from noop to the original iosched after a delay
			 * when the screen is turned on.
			 */
			schedule_delayed_work(&restore_prev,
				msecs_to_jiffies(RESTORE_DELAY_MS));
			break;
		case STATE_NOTIFIER_SUSPEND:
			/*
			 * Switch to noop when the screen turns off. Purposely block
			 * the state notifier chain call in case weird things can happen
			 * when switching elevators while the screen is off.
			 */
			cancel_delayed_work_sync(&restore_prev);
			change_all_elevators(&req_queues.list, true);
			break;
		default:
			break;
	}

	return NOTIFY_OK;
}

static void restore_prev_fn(struct work_struct *work)
{
	change_all_elevators(&req_queues.list, false);
}

int init_iosched_switcher(struct request_queue *q)
{
	struct req_queue_data *r;

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	r->queue = q;

	spin_lock(&init_lock);
	list_add(&r->list, &req_queues.list);
	spin_unlock(&init_lock);

	return 0;
}

static int iosched_switcher_core_init(void)
{
	int ret = 0;

	INIT_DELAYED_WORK(&restore_prev, restore_prev_fn);
	notif.notifier_call = state_notifier_callback;
	ret = state_register_client(&notif);

	return ret;
}
late_initcall(iosched_switcher_core_init);
