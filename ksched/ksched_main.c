/*
 * ksched.c - an accelerated scheduler interface for the IOKernel
 */

#include <asm/io.h>
#include <asm/local.h>
#include <asm/msr-index.h>
#include <asm/msr.h>
#include <asm/mwait.h>
#include <asm/tlbflush.h>
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/cpuidle.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/smp.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <linux/version.h>
// #include <linux/types.h>

// #include "ksched.h"
// #include "uintr.h"
#include "defs.h"
// #include "../iokernel/pmc.h"

#define CORE_PERF_GLOBAL_CTRL_ENABLE_PMC_0 (0x1)
#define CORE_PERF_GLOBAL_CTRL_ENABLE_PMC_1 (0x2)

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,1,0)
#define PF__HOLE__40000000 0x40000000
#endif

#define PF_KSCHED_PARKED PF__HOLE__40000000

/* the character device that provides the ksched IOCTL interface */
static struct cdev ksched_cdev;

/* shared memory between the IOKernel and the Linux Kernel */
__read_mostly struct ksched_shm_cpu *shm;
#define SHM_SIZE (NR_CPUS * sizeof(struct ksched_shm_cpu))

/* per-cpu data to coordinate context switching and signal delivery */
DEFINE_PER_CPU(struct ksched_percpu, kp);

/**
 * ksched_measure_pmc - read a performance counter
 * @sel: selects an x86 performance counter
 */
static u64 ksched_measure_pmc(u64 sel)
{
	struct ksched_percpu *p = this_cpu_ptr(&kp);
	u64 val;

	if (p->last_sel != sel) {
		wrmsrl(MSR_P6_EVNTSEL0, sel);
		p->last_sel = sel;
	}
	rdmsrl(MSR_P6_PERFCTR0, val);

	return val;
}

static void ksched_ipi(void *unused)
{
	// struct ksched_percpu *p;
	struct ksched_shm_cpu *s;
	int cpu, tmp;

	cpu = get_cpu();
	// p = this_cpu_ptr(&kp);
	s = &shm[cpu];

	/* check if a signal has been requested */
	// tmp = smp_load_acquire(&s->sig);
	// if (tmp == p->last_gen) {
	// 	ksched_deliver_signal(p, READ_ONCE(s->signum));
	// 	smp_store_release(&s->sig, 0);
	// }

	/* check if a performance counter has been requested */
	tmp = smp_load_acquire(&s->pmc);
	if (tmp != 0) {
		s->pmcval = ksched_measure_pmc(READ_ONCE(s->pmcsel));
		s->pmctsc = rdtsc();
		smp_store_release(&s->pmc, 0);
	}

	put_cpu();
}

static int get_user_cpu_mask(const unsigned long __user *user_mask_ptr,
			     unsigned len, struct cpumask *new_mask)
{
	if (len < cpumask_size())
		cpumask_clear(new_mask);
	else if (len > cpumask_size())
		len = cpumask_size();

	return copy_from_user(new_mask, user_mask_ptr, len) ? -EFAULT : 0;
}

static long ksched_intr(struct ksched_intr_req __user *ureq)
{
	cpumask_var_t mask;
	struct ksched_intr_req req;

	/* only the IOKernel can send interrupts (privileged) */
	if (unlikely(!capable(CAP_SYS_ADMIN)))
		return -EACCES;

	/* validate inputs */
	if (unlikely(copy_from_user(&req, ureq, sizeof(req))))
		return -EFAULT;
	if (unlikely(!alloc_cpumask_var(&mask, GFP_KERNEL)))
		return -ENOMEM;
	if (unlikely(get_user_cpu_mask((const unsigned long __user *)req.mask,
				       req.len, mask))) {
		free_cpumask_var(mask);
		return -EFAULT;
	}

	smp_call_function_many(mask, ksched_ipi, NULL, false);
	free_cpumask_var(mask);
	return 0;
}

static long
ksched_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* validate input */
	if (unlikely(_IOC_TYPE(cmd) != KSCHED_MAGIC))
		return -ENOTTY;
	if (unlikely(_IOC_NR(cmd) > KSCHED_IOC_MAXNR))
		return -ENOTTY;

	switch (cmd) {
	// case KSCHED_IOC_START:
	// 	return ksched_start(to_uintr_ctx(filp), arg);
	// case KSCHED_IOC_PARK:
	// 	return ksched_park(to_uintr_ctx(filp), arg);
	case KSCHED_IOC_INTR:
		return ksched_intr((void __user *)arg);
	// case KSCHED_IOC_UINTR_MULTICAST:
	// 	return uintr_multicast((void __user *)arg);
	// case KSCHED_IOC_UINTR_SETUP_USER:
	// 	return uintr_setup_user(filp, arg);
	// case KSCHED_IOC_UINTR_SETUP_ADMIN:
	// 	return uintr_setup_admin(filp);
	default:
		break;
	}

	return -ENOTTY;
}

static int ksched_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* only the IOKernel can access the shared region (privileged) */
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	return remap_vmalloc_range(vma, (void *)shm, vma->vm_pgoff);
}

static int ksched_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ksched_release(struct inode *inode, struct file *filp)
{
	// uintr_file_release(filp);
	return 0;
}

static struct file_operations ksched_ops = {
	.owner		= THIS_MODULE,
	.mmap		= ksched_mmap,
	.unlocked_ioctl	= ksched_ioctl,
	.open		= ksched_open,
	.release	= ksched_release,
};

static void __init ksched_init_pmc(void *arg)
{
	wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, 0x333);
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL,
	       CORE_PERF_GLOBAL_CTRL_ENABLE_PMC_0 |
	       CORE_PERF_GLOBAL_CTRL_ENABLE_PMC_1 |
	       (1UL << 32) | (1UL << 33) | (1UL << 34));
}

static int __init ksched_init(void)
{
	dev_t devno_ksched = MKDEV(KSCHED_MAJOR, KSCHED_MINOR);
	int ret;

	// has_mwait = cpu_has(&boot_cpu_data, X86_FEATURE_MWAIT);
	// if (!has_mwait)
	// 	printk(KERN_ERR "ksched: mwait support is missing");

	ret = register_chrdev_region(devno_ksched, 1, "ksched");
	if (ret)
		return ret;

	cdev_init(&ksched_cdev, &ksched_ops);
	ret = cdev_add(&ksched_cdev, devno_ksched, 1);
	if (ret)
		goto fail_ksched_cdev_add;

	shm = vmalloc_user(SHM_SIZE);
	if (!shm) {
		ret = -ENOMEM;
		goto fail_shm;
	}
	memset(shm, 0, SHM_SIZE);

	// ret = uintr_init();
	// if (ret)
	// 	goto fail_uintr;

	// ret = ksched_cpuidle_hijack();
	// if (ret)
	// 	goto fail_hijack;

	smp_call_function(ksched_init_pmc, NULL, 1);
	ksched_init_pmc(NULL);
	printk(KERN_INFO "ksched: API V2 enabled");
	return 0;

// fail_uintr:
// 	vfree(shm);
// fail_hijack:
// 	uintr_exit();
fail_shm:
	cdev_del(&ksched_cdev);
fail_ksched_cdev_add:
	unregister_chrdev_region(devno_ksched, 1);
	return ret;
}

static void __exit ksched_exit(void)
{
	dev_t devno_ksched = MKDEV(KSCHED_MAJOR, KSCHED_MINOR);

	vfree(shm);
	cdev_del(&ksched_cdev);
	unregister_chrdev_region(devno_ksched, 1);
	printk(KERN_INFO "ksched: exited.");
}

module_init(ksched_init);
module_exit(ksched_exit);

MODULE_LICENSE("GPL");
