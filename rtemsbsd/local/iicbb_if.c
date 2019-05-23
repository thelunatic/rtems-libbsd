#include <machine/rtems-bsd-kernel-space.h>

/*
 * This file is produced automatically.
 * Do not modify anything in here by hand.
 *
 * Created from source file
 *   freebsd-org/sys/dev/iicbus/iicbb_if.m
 * with
 *   makeobjops.awk
 *
 * See the source file for legal information
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/bus.h>
#include <rtems/bsd/local/iicbb_if.h>


static int
null_pre_xfer(device_t dev)
{
	return 0;
}

static void
null_post_xfer(device_t dev)
{
}

static int
null_callback(device_t dev, int index, caddr_t data)
{
	return 0;
}

struct kobjop_desc iicbb_callback_desc = {
	0, { &iicbb_callback_desc, (kobjop_t)null_callback }
};

struct kobjop_desc iicbb_pre_xfer_desc = {
	0, { &iicbb_pre_xfer_desc, (kobjop_t)null_pre_xfer }
};

struct kobjop_desc iicbb_post_xfer_desc = {
	0, { &iicbb_post_xfer_desc, (kobjop_t)null_post_xfer }
};

struct kobjop_desc iicbb_setsda_desc = {
	0, { &iicbb_setsda_desc, (kobjop_t)kobj_error_method }
};

struct kobjop_desc iicbb_setscl_desc = {
	0, { &iicbb_setscl_desc, (kobjop_t)kobj_error_method }
};

struct kobjop_desc iicbb_getsda_desc = {
	0, { &iicbb_getsda_desc, (kobjop_t)kobj_error_method }
};

struct kobjop_desc iicbb_getscl_desc = {
	0, { &iicbb_getscl_desc, (kobjop_t)kobj_error_method }
};

struct kobjop_desc iicbb_reset_desc = {
	0, { &iicbb_reset_desc, (kobjop_t)kobj_error_method }
};

