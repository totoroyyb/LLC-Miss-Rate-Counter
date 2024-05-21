#include <stdio.h>
#include <stdint.h>

#include <base/stddef.h>
// #include <base/log.h>


void poll_loop(void) {
	for (;;) {
		sched_poll();
	}
}

void main(int argc, char *argv[]) {
	int i, ret;

	if (getuid() != 0) {
		fprintf(stderr, "Error: please run as root\n");
		return -EPERM;
	}

	base_init();
	ksched_init();
	sched_init();
	ias_bw_init();

	poll_loop();
}