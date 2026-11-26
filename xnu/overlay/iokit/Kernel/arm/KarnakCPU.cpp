/*
 * SMP bring-up for karnak (MediaTek MT8163, four Cortex-A53) using XNU's
 * legacy IOCPU interface and PSCI.
 *
 * The DTB describes PSCI with SMC32 function IDs (cpu_on = 0x84000003).
 * The architected virtual timer (PPI 27) and SGI 1 are delivered as Group 1
 * IRQs by the GIC-400 and handled in sleh_irq(); PE_cpu_signal() is routed
 * through signalCPU() to GICD_SGIR.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pexpert/arm64/board_config.h>

#if defined(KARNAK)
#include <IOKit/IOCPU.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <arm64/proc_reg.h>
#include <machine/atomic.h>
#include <kern/clock.h>
#include <kern/processor.h>
#include <kern/thread.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/protos.h>

extern "C" void karnak_secondary_entry(void);
extern "C" boolean_t karnak_cpu_signal_is_enabled(unsigned int cpu);

class KarnakCPU;
static KarnakCPU *karnak_cpus[MAX_CPUS];
static uint32_t karnak_cpus_registered;

/*
 * PSCI CPU_ON.  The DTB names 0x84000003 (SMC32), not the SMC64 variant.
 * x0 is the signed result; SMCCC does not preserve x1-x17 across the call.
 */
static int32_t
karnak_psci_cpu_on(uint32_t target, uint32_t entry, uint32_t context)
{
	register uint32_t x0 __asm__("x0") = 0x84000003U;
	register uint32_t x1 __asm__("x1") = target;
	register uint32_t x2 __asm__("x2") = entry;
	register uint32_t x3 __asm__("x3") = context;
	__asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :
	    "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13",
	    "x14", "x15", "x16", "x17", "cc", "memory");
	return (int32_t)x0;
}

class KarnakCPU : public IOCPU {
	OSDeclareDefaultStructors(KarnakCPU);
	const OSSymbol *cpuName = NULL;
	uint32_t initialized = 0; /* release/acquire publication after initCPU sends SGI */

public:
	static void startAll(void *arg, wait_result_t result);
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	void initCPU(bool boot) APPLE_KEXT_OVERRIDE;
	void quiesceCPU(void) APPLE_KEXT_OVERRIDE;
	kern_return_t startCPU(vm_offset_t start_paddr, vm_offset_t arg_paddr) APPLE_KEXT_OVERRIDE;
	void haltCPU(void) APPLE_KEXT_OVERRIDE;
	void signalCPU(IOCPU *target) APPLE_KEXT_OVERRIDE;
	const OSSymbol *getCPUName(void) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(KarnakCPU, IOCPU);

bool
KarnakCPU::start(IOService *provider)
{
	const ml_topology_info_t *topology = ml_get_topology_info();
	OSNumber *logical = OSDynamicCast(OSNumber, provider->getProperty("logical-cpu-id"));
	OSData *reg = OSDynamicCast(OSData, provider->getProperty("reg"));

	if (logical == NULL || reg == NULL || reg->getLength() != sizeof(uint32_t) ||
	    logical->unsigned32BitValue() >= topology->num_cpus) {
		return false;
	}
	const ml_topology_cpu_t *cpu = &topology->cpus[logical->unsigned32BitValue()];
	if (*(const uint32_t *)reg->getBytesNoCopy() != cpu->phys_id) {
		return false;
	}
	if (cpu->phys_id >= KARNAK_SMP_MAX_CPUS ||
	    topology->num_cpus > KARNAK_SMP_MAX_CPUS) {
		panic("KARNAK CPU: invalid Cortex-A53 topology cpu=%u mpidr=0x%x count=%u",
		    cpu->cpu_id, cpu->phys_id, topology->num_cpus);
	}
	if (!IOCPU::start(provider)) {
		return false;
	}
	cpuName = OSSymbol::withCStringNoCopy("Cortex-A53");
	if (cpuName == NULL) {
		return false;
	}
	setCPUNumber(cpu->cpu_id);

	ml_processor_info_t info = {};
	info.cpu_id = reinterpret_cast<cpu_id_t>(this);
	info.phys_id = cpu->phys_id;
	info.log_id = cpu->cpu_id;
	info.cluster_id = cpu->cluster_id;
	info.cluster_type = cpu->cluster_type;
	info.l2_cache_id = cpu->l2_cache_id;
	info.l2_cache_size = cpu->l2_cache_size;
	info.l3_cache_id = cpu->l3_cache_id;
	info.l3_cache_size = cpu->l3_cache_size;
	perfmon_interrupt_handler_func pmi_handler = NULL;
	kern_return_t result = ml_processor_register(&info, &machProcessor, &ipi_handler, &pmi_handler);
	if (result != KERN_SUCCESS) {
		panic("KARNAK CPU: ml_processor_register failed: %d", result);
	}
	if (pmi_handler != NULL) {
		panic("KARNAK CPU: PMI routing is not implemented");
	}

	registerService();
	__atomic_store_n(&karnak_cpus[cpu->cpu_id], this, __ATOMIC_RELEASE);
	__atomic_add_fetch(&karnak_cpus_registered, 1, __ATOMIC_ACQ_REL);
	if (cpu == topology->boot_cpu) {
		thread_t thread;
		result = kernel_thread_start(&KarnakCPU::startAll, NULL, &thread);
		if (result != KERN_SUCCESS) {
			panic("KARNAK CPU: cannot create startup thread: %d", result);
		}
		thread_deallocate(thread);
	}
	return true;
}

/* Do not wait in a nub's start(): matching can be asynchronous or serialized. */
void
KarnakCPU::startAll(void *arg __unused, wait_result_t result __unused)
{
	const ml_topology_info_t *topology = ml_get_topology_info();
	uint32_t requested = topology->num_cpus;
	PE_parse_boot_argn("cpus", &requested, sizeof(requested));
	IOLog("KARNAK CPU: requested=%u selected=%u (Cortex-A53 limit=%u)\n",
	    requested, topology->num_cpus, KARNAK_SMP_MAX_CPUS);

	uint64_t deadline;
	nanoseconds_to_deadline(10 * NSEC_PER_SEC, &deadline);
	while (__atomic_load_n(&karnak_cpus_registered, __ATOMIC_ACQUIRE) != topology->num_cpus) {
		if (mach_absolute_time() > deadline) {
			for (unsigned int i = 0; i < topology->num_cpus; i++) {
				if (__atomic_load_n(&karnak_cpus[i], __ATOMIC_ACQUIRE) == NULL) {
					IOLog("KARNAK CPU: cpu=%u NOT registered (IOKit matching timeout)\n", i);
				}
			}
			panic("KARNAK CPU: CPU registration timeout");
		}
		IOSleep(1);
	}

	/* The stock DTB numbers cpu@0..cpu@3 and the boot CPU is Aff0 0. */
	if (topology->boot_cpu->cpu_id != 0 || topology->boot_cpu->phys_id != 0) {
		panic("KARNAK CPU: expected boot MPIDR 0 and logical CPU 0");
	}
	for (unsigned int i = 0; i < topology->num_cpus; i++) {
		KarnakCPU *cpu = __atomic_load_n(&karnak_cpus[i], __ATOMIC_ACQUIRE);
		kern_return_t kr = processor_start(cpu->machProcessor);
		if (kr != KERN_SUCCESS) {
			panic("KARNAK CPU: cpu=%u processor_start failed: %d", i, kr);
		}
		/* processor_start() is asynchronous for secondaries in this XNU. */
		nanoseconds_to_deadline(5 * NSEC_PER_SEC, &deadline);
		while (__atomic_load_n(&cpu->initialized, __ATOMIC_ACQUIRE) == 0 ||
		    !karnak_cpu_signal_is_enabled(i)) {
			if (mach_absolute_time() > deadline) {
				IOLog("KARNAK CPU: cpu=%u mpidr=0x%x NOT alive after 5s\n",
				    i, topology->cpus[i].phys_id);
				panic("KARNAK CPU: cpu=%u startup timeout (reset/MMU/GIC/SGI)", i);
			}
			IOSleep(1);
		}
		IOLog("KARNAK CPU: cpu=%u mpidr=0x%x came alive; SGI1 OK\n",
		    i, topology->cpus[i].phys_id);
	}
	/* Publish exactly once, after every selected CPU has completed initCPU(). */
	ml_cpu_init_completed();
	__atomic_load_n(&karnak_cpus[topology->boot_cpu->cpu_id], __ATOMIC_ACQUIRE)->cpusRunning();
	IOService::publishResource(gIOAllCPUInitializedKey, kOSBooleanTrue);
	ml_set_max_cpus(topology->max_cpu_id + 1);
	IOLog("KARNAK CPU: %u/%u CPUs initialized, SGI1 verified\n",
	    topology->num_cpus, topology->num_cpus);
	thread_terminate(current_thread());
}

void
KarnakCPU::free(void)
{
	OSSafeReleaseNULL(cpuName);
	IOCPU::free();
}

void
KarnakCPU::initCPU(bool boot)
{
	if (!boot) {
		panic("KARNAK CPU: cpu=%u resume is not implemented", getCPUNumber());
	}
	assert(cpu_number() == (int)getCPUNumber());
	ml_broadcast_cpu_event(CPU_BOOTED, getCPUNumber());

	/* Secondary startup runs at splsched: queue the self-IPI and return.
	 * startAll waits for its delivery after the startup thread unmasks IRQs.
	 * Waiting here with DAIF.I set would deadlock every secondary. */
	signalCPU(this);
	setCPUState(kIOCPUStateRunning);
	kprintf("KARNAK CPU: cpu=%u entered initCPU mpidr=0x%llx EL%llu IRQs=%u\n",
	    getCPUNumber(), __builtin_arm_rsr64("MPIDR_EL1") & 0xffffULL,
	    __builtin_arm_rsr64("CurrentEL") >> 2, ml_get_interrupts_enabled());
	__atomic_store_n(&initialized, 1, __ATOMIC_RELEASE);
}

void
KarnakCPU::signalCPU(IOCPU *target)
{
	const ml_topology_info_t *topology = ml_get_topology_info();
	unsigned int logical = target->getCPUNumber();
	if (logical >= topology->num_cpus) {
		panic("KARNAK CPU: invalid SGI target %u", logical);
	}
	/* GICv2 targets Aff0; the boot DTB has a single cluster (Aff1 = 0). */
	uint32_t phys = topology->cpus[logical].phys_id;
	if ((phys & ~0xffU) != 0) {
		panic("KARNAK CPU: SGI target 0x%x has a non-zero affinity level", phys);
	}
	karnak_gicv2_send_sgi(phys & 0xffU);
}

kern_return_t
KarnakCPU::startCPU(vm_offset_t start_paddr, vm_offset_t arg_paddr)
{
	const ml_topology_cpu_t *cpu = &ml_get_topology_info()->cpus[getCPUNumber()];
	/* cpu_start() passes NULL,NULL: x20/x21 are set by LowResetVectorBase. */
	if (start_paddr != 0 || arg_paddr != 0 || cpu->phys_id == 0 ||
	    __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) != 0) {
		panic("KARNAK CPU: cpu=%u unsupported restart", getCPUNumber());
	}
	vm_offset_t entry = ml_static_vtop((vm_offset_t)&karnak_secondary_entry);
	IOLog("KARNAK CPU: cpu=%u CPU_ON target=0x%x entry_pa=0x%llx context=%u\n",
	    getCPUNumber(), cpu->phys_id, (uint64_t)entry, getCPUNumber());
	__builtin_arm_dsb(DSB_SY);
	int32_t rc = karnak_psci_cpu_on(cpu->phys_id, (uint32_t)entry, getCPUNumber());
	IOLog("KARNAK CPU: cpu=%u PSCI CPU_ON rc=%d\n", getCPUNumber(), rc);
	/* cpu_start() discards PE_cpu_start's return; do not silently carry on. */
	if (rc != 0) {
		panic("KARNAK CPU: cpu=%u NOT alive: PSCI CPU_ON rc=%d", getCPUNumber(), rc);
	}
	return KERN_SUCCESS;
}

void
KarnakCPU::quiesceCPU(void)
{
	panic("KARNAK CPU: CPU quiesce is not implemented");
}

void
KarnakCPU::haltCPU(void)
{
	panic("KARNAK CPU: CPU halt/suspend is not implemented");
}

const OSSymbol *
KarnakCPU::getCPUName(void)
{
	return cpuName;
}
#endif /* KARNAK */
