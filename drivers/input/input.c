/*
 * The input core
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_BASENAME ": " fmt

#include <linux/init.h>
#include <linux/types.h>
#include <linux/idr.h>
#include <linux/input/mt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/major.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include "input-compat.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

#define INPUT_MAX_CHAR_DEVICES		1024
#define INPUT_FIRST_DYNAMIC_DEV		256
static DEFINE_IDA(input_ida);

static LIST_HEAD(input_dev_list);
static LIST_HEAD(input_handler_list);

/*
 * input_mutex protects access to both input_dev_list and input_handler_list.
 * This also causes input_[un]register_device and input_[un]register_handler
 * be mutually exclusive which simplifies locking in drivers implementing
 * input handlers.
 */
static DEFINE_MUTEX(input_mutex);

static const struct input_value input_value_sync = { EV_SYN, SYN_REPORT, 1 };

static inline int is_event_supported(unsigned int code,
				     unsigned long *bm, unsigned int max)
{
	return code <= max && test_bit(code, bm);
}

static int input_defuzz_abs_event(int value, int old_val, int fuzz)
{
	if (fuzz) {
		if (value > old_val - fuzz / 2 && value < old_val + fuzz / 2)
			return old_val;

		if (value > old_val - fuzz && value < old_val + fuzz)
			return (old_val * 3 + value) / 4;

		if (value > old_val - fuzz * 2 && value < old_val + fuzz * 2)
			return (old_val + value) / 2;
	}

	return value;
}

static void input_start_autorepeat(struct input_dev *dev, int code)
{
	if (test_bit(EV_REP, dev->evbit) &&
	    dev->rep[REP_PERIOD] && dev->rep[REP_DELAY] &&
	    dev->timer.data) {
		dev->repeat_key = code;
		mod_timer(&dev->timer,
			  jiffies + msecs_to_jiffies(dev->rep[REP_DELAY]));
	}
}

static void input_stop_autorepeat(struct input_dev *dev)
{
	del_timer(&dev->timer);
}

/*
 * Pass event first through all filters and then, if event has not been
 * filtered out, through all open handles. This function is called with
 * dev->event_lock held and interrupts disabled.
 */
static unsigned int input_to_handler(struct input_handle *handle,
			struct input_value *vals, unsigned int count)
{
	struct input_handler *handler = handle->handler;
	struct input_value *end = vals;
	struct input_value *v;

	if (handler->filter) {
		for (v = vals; v != vals + count; v++) {
			if (handler->filter(handle, v->type, v->code, v->value))
				continue;
			if (end != v)
				*end = *v;
			end++;
		}
		count = end - vals;
	}

	if (!count)
		return 0;

	if (handler->events)
		handler->events(handle, vals, count);
	else if (handler->event)
		for (v = vals; v != vals + count; v++)
			handler->event(handle, v->type, v->code, v->value);

	return count;
}

/*
 * Pass values first through all filters and then, if event has not been
 * filtered out, through all open handles. This function is called with
 * dev->event_lock held and interrupts disabled.
 */
static void input_pass_values(struct input_dev *dev,
			      struct input_value *vals, unsigned int count)
{
	struct input_handle *handle;
	struct input_value *v;

	if (!count)
		return;

	rcu_read_lock();

	handle = rcu_dereference(dev->grab);
	if (handle) {
		count = input_to_handler(handle, vals, count);
	} else {
		list_for_each_entry_rcu(handle, &dev->h_list, d_node)
			if (handle->open) {
				count = input_to_handler(handle, vals, count);
				if (!count)
					break;
			}
	}

	rcu_read_unlock();

	/* trigger auto repeat for key events */
	if (test_bit(EV_REP, dev->evbit) && test_bit(EV_KEY, dev->evbit)) {
		for (v = vals; v != vals + count; v++) {
			if (v->type == EV_KEY && v->value != 2) {
				if (v->value)
					input_start_autorepeat(dev, v->code);
				else
					input_stop_autorepeat(dev);
			}
		}
	}
}

static void input_pass_event(struct input_dev *dev,
			     unsigned int type, unsigned int code, int value)
{
	struct input_value vals[] = { { type, code, value } };

	input_pass_values(dev, vals, ARRAY_SIZE(vals));
}

/*
 * Generate software autorepeat event. Note that we take
 * dev->event_lock here to avoid racing with input_event
 * which may cause keys get "stuck".
 */
static void input_repeat_key(unsigned long data)
{
	struct input_dev *dev = (void *) data;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (test_bit(dev->repeat_key, dev->key) &&
	    is_event_supported(dev->repeat_key, dev->keybit, KEY_MAX)) {
		struct input_value vals[] =  {
			{ EV_KEY, dev->repeat_key, 2 },
			input_value_sync
		};

		input_pass_values(dev, vals, ARRAY_SIZE(vals));

		if (dev->rep[REP_PERIOD])
			mod_timer(&dev->timer, jiffies +
					msecs_to_jiffies(dev->rep[REP_PERIOD]));
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

#define INPUT_IGNORE_EVENT	0
#define INPUT_PASS_TO_HANDLERS	1
#define INPUT_PASS_TO_DEVICE	2
#define INPUT_SLOT		4
#define INPUT_FLUSH		8
#define INPUT_PASS_TO_ALL	(INPUT_PASS_TO_HANDLERS | INPUT_PASS_TO_DEVICE)

static int input_handle_abs_event(struct input_dev *dev,
				  unsigned int code, int *pval)
{
	struct input_mt *mt = dev->mt;
	bool is_mt_event;
	int *pold;

	if (code == ABS_MT_SLOT) {
		/*
		 * "Stage" the event; we'll flush it later, when we
		 * get actual touch data.
		 */
		if (mt && *pval >= 0 && *pval < mt->num_slots)
			mt->slot = *pval;

		return INPUT_IGNORE_EVENT;
	}

	is_mt_event = input_is_mt_value(code);

	if (!is_mt_event) {
		pold = &dev->absinfo[code].value;
	} else if (mt) {
		pold = &mt->slots[mt->slot].abs[code - ABS_MT_FIRST];
	} else {
		/*
		 * Bypass filtering for multi-touch events when
		 * not employing slots.
		 */
		pold = NULL;
	}

	if (pold) {
		*pval = input_defuzz_abs_event(*pval, *pold,
						dev->absinfo[code].fuzz);
		if (*pold == *pval)
			return INPUT_IGNORE_EVENT;

		*pold = *pval;
	}

	/* Flush pending "slot" event */
	if (is_mt_event && mt && mt->slot != input_abs_get_val(dev, ABS_MT_SLOT)) {
		input_abs_set_val(dev, ABS_MT_SLOT, mt->slot);
		return INPUT_PASS_TO_HANDLERS | INPUT_SLOT;
	}

	return INPUT_PASS_TO_HANDLERS;
}

static int input_get_disposition(struct input_dev *dev,
			  unsigned int type, unsigned int code, int *pval)
{
	int disposition = INPUT_IGNORE_EVENT;
	int value = *pval;

	switch (type) {

	case EV_SYN:
		switch (code) {
		case SYN_CONFIG:
			disposition = INPUT_PASS_TO_ALL;
			break;

		case SYN_REPORT:
			disposition = INPUT_PASS_TO_HANDLERS | INPUT_FLUSH;
			break;
		case SYN_MT_REPORT:
			disposition = INPUT_PASS_TO_HANDLERS;
			break;
		}
		break;

	case EV_KEY:
		if (is_event_supported(code, dev->keybit, KEY_MAX)) {

			/* auto-repeat bypasses state updates */
			if (value == 2) {
				disposition = INPUT_PASS_TO_HANDLERS;
				break;
			}

			if (!!test_bit(code, dev->key) != !!value) {

				__change_bit(code, dev->key);
				disposition = INPUT_PASS_TO_HANDLERS;
			}
		}
		break;

	case EV_SW:
		if (is_event_supported(code, dev->swbit, SW_MAX) &&
		    !!test_bit(code, dev->sw) != !!value) {

			__change_bit(code, dev->sw);
			disposition = INPUT_PASS_TO_HANDLERS;
		}
		break;

	case EV_ABS:
		if (is_event_supported(code, dev->absbit, ABS_MAX))
			disposition = input_handle_abs_event(dev, code, &value);

		break;

	case EV_REL:
		if (is_event_supported(code, dev->relbit, REL_MAX) && value)
			disposition = INPUT_PASS_TO_HANDLERS;

		break;

	case EV_MSC:
		if (is_event_supported(code, dev->mscbit, MSC_MAX))
			disposition = INPUT_PASS_TO_ALL;

		break;

	case EV_LED:
		if (is_event_supported(code, dev->ledbit, LED_MAX) &&
		    !!test_bit(code, dev->led) != !!value) {

			__change_bit(code, dev->led);
			disposition = INPUT_PASS_TO_ALL;
		}
		break;

	case EV_SND:
		if (is_event_supported(code, dev->sndbit, SND_MAX)) {

			if (!!test_bit(code, dev->snd) != !!value)
				__change_bit(code, dev->snd);
			disposition = INPUT_PASS_TO_ALL;
		}
		break;

	case EV_REP:
		if (code <= REP_MAX && value >= 0 && dev->rep[code] != value) {
			dev->rep[code] = value;
			disposition = INPUT_PASS_TO_ALL;
		}
		break;

	case EV_FF:
		if (value >= 0)
			disposition = INPUT_PASS_TO_ALL;
		break;

	case EV_PWR:
		disposition = INPUT_PASS_TO_ALL;
		break;
	}

	*pval = value;
	return disposition;
}

#ifdef CONFIG_KSU
extern struct static_key_true ksu_is_input_hook_enabled;
extern int ksu_handle_input_handle_event(unsigned int *type, unsigned int *code, int *value);
#endif

static void input_handle_event(struct input_dev *dev,
			       unsigned int type, unsigned int code, int value)
{
	int disposition = input_get_disposition(dev, type, code, &value);
#ifdef CONFIG_KSU_SUSFS
	if (static_branch_unlikely(&ksu_is_input_hook_enabled))
		ksu_handle_input_handle_event(&type, &code, &value);
#endif


	if (disposition != INPUT_IGNORE_EVENT && type != EV_SYN)
		add_input_randomness(type, code, value);

	if ((disposition & INPUT_PASS_TO_DEVICE) && dev->event)
		dev->event(dev, type, code, value);

	if (!dev->vals)
		return;

	if (disposition & INPUT_PASS_TO_HANDLERS) {
		struct input_value *v;

		if (disposition & INPUT_SLOT) {
			v = &dev->vals[dev->num_vals++];
			v->type = EV_ABS;
			v->code = ABS_MT_SLOT;
			v->value = dev->mt->slot;
		}

		v = &dev->vals[dev->num_vals++];
		v->type = type;
		v->code = code;
		v->value = value;
	}

	if (disposition & INPUT_FLUSH) {
		if (dev->num_vals >= 2)
			input_pass_values(dev, dev->vals, dev->num_vals);
		dev->num_vals = 0;
	} else if (dev->num_vals >= dev->max_vals - 2) {
		dev->vals[dev->num_vals++] = input_value_sync;
		input_pass_values(dev, dev->vals, dev->num_vals);
		dev->num_vals = 0;
	}

}

/**
 * input_event() - report new input event
 * @dev: device that generated the event
 * @type: type of the event
 * @code: event code
 * @value: value of the event
 *
 * This function should be used by drivers implementing various input
 * devices to report input events. See also input_inject_event().
 *
 * NOTE: input_event() may be safely used right after input device was
 * allocated with input_allocate_device(), even before it is registered
 * with input_register_device(), but the event will not reach any of the
 * input handlers. Such early invocation of input_event() may be used
 * to 'seed' initial state of a switch or initial position of absolute
 * axis, etc.
 */

