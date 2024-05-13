/*
 * init.c - support for initialization
 */

#include <stdlib.h>

#include <base/init.h>
#include <base/log.h>
#include <base/thread.h>

#include "init_internal.h"

bool base_init_done __aligned(CACHE_LINE_SIZE);

void __weak init_shutdown(int status)
{
	log_info("init: shutting down -> %s",
		 status == EXIT_SUCCESS ? "SUCCESS" : "FAILURE");
	exit(status);
}

/* we initialize these early subsystems by hand */
static int init_internal(void)
{
	int ret;

	ret = cpu_init();
	if (ret)
		return ret;

	ret = time_init();
	if (ret)
		return ret;

	return 0;
}

/**
 * base_init - initializes the base library
 *
 * Call this function before using the library.
 * Returns 0 if successful, otherwise fail.
 */
int base_init(void)
{
	int ret;

	ret = init_internal();
	if (ret)
		return ret;

	base_init_done = true;
	return 0;
}
