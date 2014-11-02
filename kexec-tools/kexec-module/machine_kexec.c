/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 */

 
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/system_misc.h>
#include <linux/memblock.h>

#include <linux/module.h>
#include <linux/kallsyms.h>
#include <asm/mmu_writeable.h>

#include <linux/of_fdt.h>

#include <linux/platform_device.h>
#include <asm/memblock.h>

#ifdef CONFIG_MSM_WATCHDOG
#include <mach/msm_iomap.h>
#define WDT0_EN (MSM_TMR_BASE + 0x40)
#endif

extern const unsigned char relocate_new_kernel[];
extern const unsigned int relocate_new_kernel_size;


extern unsigned long kexec_start_address;
extern unsigned long kexec_indirection_page;
extern unsigned long kexec_mach_type;
extern unsigned long kexec_boot_atags;
#ifdef CONFIG_KEXEC_HARDBOOT
extern unsigned long kexec_hardboot;
extern unsigned long kexec_boot_atags_len;
extern unsigned long kexec_kernel_len;
void (*kexec_hardboot_hook)(void);
#endif


// Delewer - 02/2014 and 06/2014
//  echo 1 > /proc/sys/kernel/kptr_restrict
//  grep xxxxxxxxx /proc/kallsyms
void (*my_setup_mm_for_reboot)(void); 
void (*my_cpu_v7_proc_fin)(void); 
void (*my_cpu_v7_reset)(unsigned long); 
void (*my_smp_send_stop)(void);
void (*my_soft_restart)(unsigned long); 
void (*my_flush_tlb_kernel_page)(unsigned long); 
int (*my_memblock_reserve2)(phys_addr_t, phys_addr_t);
int (*my_memblock_is_region_memory)(phys_addr_t, phys_addr_t);

void (*my_reboot_phys)(void); 

//typedef void (*phys_reset_t)(unsigned long);	
//phys_reset_t phys_reset;


// Userspace RW memory for varaiables modifications
long unsigned int my_reloc_kernel[0x200];
int my_reloc_kernel_start_address;
int my_reloc_kernel_indirection_page;
int my_reloc_kernel_mach_type;
int my_reloc_kernel_boot_atags;



static atomic_t waiting_for_crash_ipi;


void machine_kexec_cleanup(struct kimage *image)
{
}

void machine_crash_nonpanic_core(void *unused)
{
	struct pt_regs regs;

	crash_setup_regs(&regs, NULL);
	printk(KERN_DEBUG "CPU %u will stop doing anything useful since another CPU has crashed\n",
	       smp_processor_id());
	crash_save_cpu(&regs, smp_processor_id());
	flush_cache_all();

	atomic_dec(&waiting_for_crash_ipi);
	while (1)
		cpu_relax();
}

static void machine_kexec_mask_interrupts(void)
{
	unsigned int i;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		struct irq_chip *chip;

		chip = irq_desc_get_chip(desc);
		if (!chip)
			continue;

		if (chip->irq_eoi && irqd_irq_inprogress(&desc->irq_data))
			chip->irq_eoi(&desc->irq_data);

		if (chip->irq_mask)
			chip->irq_mask(&desc->irq_data);

		if (chip->irq_disable && !irqd_irq_disabled(&desc->irq_data))
			chip->irq_disable(&desc->irq_data);
	}
}


void machine_crash_shutdown(struct pt_regs *regs)
{
	unsigned long msecs;

	local_irq_disable();

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);
	smp_call_function(machine_crash_nonpanic_core, NULL, false);
	msecs = 1000; // Wait at most a second for the other cpus to stop 
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && msecs) {
		mdelay(1);
		msecs--;
	}
	if (atomic_read(&waiting_for_crash_ipi) > 0)
		printk(KERN_WARNING "Non-crashing CPUs did not react to IPI\n");

	crash_save_cpu(regs, smp_processor_id());
	machine_kexec_mask_interrupts();

	printk(KERN_INFO "Loading crashdump kernel...\n");
}


void machine_shutdown(void)
{
	preempt_disable();
#ifdef CONFIG_SMP
	//smp_send_stop();
	my_smp_send_stop = (void *)kallsyms_lookup_name("smp_send_stop");
	my_smp_send_stop();
#endif
}


void arch_kexec(void)
{
#ifdef CONFIG_MSM_WATCHDOG
	// Prevent watchdog from resetting SoC
	writel(0, WDT0_EN);
	printk("Kexec: MSM Watchdog Exit - Deactivated\n");
#endif
	return;
}

/*
static struct resource kexec_RelocateK_resources[] = 
	{
	[0] = 
		{
		.start  = 0x20000000,
		.end    = 0x40000000,
		.flags  = IORESOURCE_MEM,
		},
	};
static struct platform_device kexec_RelocateK_device = 
	{
	.name	= "kexec_relocate_kernel",
	.id	= -1,
	};
static struct platform_device *kexec_RKdevs[] = 
	{
	&kexec_RelocateK_device,
	};
*/
static void kexec_info(struct kimage *image)
{
	int i;
	printk("Kexec: kexec_info - Information\n");
	for (i = 0; i < image->nr_segments; i++)
		{
		char *whoit;
		if (i==0) whoit="zImage";
		if (i==1) whoit="initrd";
		if (i==2) whoit="dt.img";
		if (i==3) whoit="???";
		if (i==4) whoit="???";
		printk("Kexec: kexec_info - BUF - segment[%d]: 0x%08x - 0x%08x (0x%08x)  => %s\n",i, (unsigned int)image->segment[i].buf, (unsigned int)image->segment[i].buf + image->segment[i].bufsz, (unsigned int)image->segment[i].bufsz, whoit);
		printk("Kexec: kexec_info - MEM - segment[%d]: 0x%08x - 0x%08x (0x%08x)  => %s\n",i, (unsigned int)image->segment[i].mem, (unsigned int)image->segment[i].mem + image->segment[i].memsz, (unsigned int)image->segment[i].memsz, whoit);
		}
	printk("Kexec: kexec_info - start     : 0x%08x\n\n", (unsigned int)image->start);

/*
	///////////
	printk("Kexec: kexec_info - 'relocate_new_kernel' memory reservation in '/proc/iomap'\n");
	//memblock_init();
	////////arm_memblock_init();
	//memblock_free(image->segment[0].mem, (image->segment[2].mem + image->segment[2].memsz - image->segment[0].mem));
	//memblock_remove(image->segment[0].mem, (image->segment[2].mem + image->segment[2].memsz - image->segment[0].mem));
	my_memblock_reserve2 = (long int *)kallsyms_lookup_name("memblock_reserve");
	if (my_memblock_reserve2(image->segment[0].mem, (image->segment[2].mem + image->segment[2].memsz - image->segment[0].mem)))
		{
		printk(KERN_ERR "Kexec : Failed to reserve memory for KEXEC_RELOCATE_KERNEL with size 0x%X at 0x%.8X\n", (image->segment[2].mem + image->segment[2].memsz - image->segment[0].mem), image->segment[0].mem);
		return;
		}
	printk("Kexec : relocate_kernel mem space with size 0x%X at 0x%.8X allocated successful.\n", (image->segment[2].mem + image->segment[2].memsz - image->segment[0].mem), image->segment[0].mem);
	kexec_RelocateK_device.num_resources  = ARRAY_SIZE(kexec_RelocateK_resources);
	kexec_RelocateK_device.resource       = kexec_RelocateK_resources;
	platform_add_devices(kexec_RKdevs, ARRAY_SIZE(kexec_RKdevs));
*/
}


	
int machine_kexec_prepare(struct kimage *image)
{
	unsigned long page_list;
	unsigned long reboot_code_buffer_phys;
	void *reboot_code_buffer;
	
	struct kexec_segment *current_segment;
	__be32 header;
	int i, err;	
	
	printk(KERN_INFO "Kexec: machine_kexec_prepare\n\n");
	
	
	
	
	// Take real relocate routine to userspace
	memcpy(my_reloc_kernel, relocate_new_kernel, relocate_new_kernel_size);

	// Calculate variables locations
	my_reloc_kernel_start_address=((long unsigned int)&kexec_start_address - (long unsigned int)relocate_new_kernel) / 4;
	my_reloc_kernel_indirection_page=((long unsigned int)&kexec_indirection_page - (long unsigned int)relocate_new_kernel) / 4;
	my_reloc_kernel_mach_type=((long unsigned int)&kexec_mach_type - (long unsigned int)relocate_new_kernel) / 4;
	my_reloc_kernel_boot_atags=((long unsigned int)&kexec_boot_atags - (long unsigned int)relocate_new_kernel) / 4;

	// information
	kexec_info(image);
	
	// Extract virt and phys addresses for final destination...
	page_list=image->head & PAGE_MASK;
	reboot_code_buffer_phys = page_to_pfn(image->control_code_page) << PAGE_SHIFT;
	reboot_code_buffer = page_address(image->control_code_page);
	
	// No segment at default ATAGs address ; try to locate a dtb using magic.
	my_memblock_is_region_memory = (void *)kallsyms_lookup_name("memblock_is_region_memory");
	for (i = 0; i < image->nr_segments; i++)
		{
		current_segment = &image->segment[i];
		if (!my_memblock_is_region_memory(current_segment->mem, current_segment->memsz))  
			return -EINVAL;
#ifdef CONFIG_KEXEC_HARDBOOT
		if(current_segment->mem == image->start) mem_text_write_kernel_word(&kexec_kernel_len, current_segment->memsz);
#endif
		err = get_user(header, (__be32*)current_segment->buf);
		if (err) return err;
		if (be32_to_cpu(header) == OF_DT_HEADER) 
			{
			my_reloc_kernel[my_reloc_kernel_boot_atags] = current_segment->mem;
#ifdef CONFIG_KEXEC_HARDBOOT
			mem_text_write_kernel_word(&kexec_boot_atags_len, current_segment->memsz);
#endif
            }
		}
	
	// Address allocation
	my_reloc_kernel[my_reloc_kernel_start_address]=image->start;
	my_reloc_kernel[my_reloc_kernel_indirection_page]=page_list;
	my_reloc_kernel[my_reloc_kernel_mach_type]=machine_arch_type;
	if (!my_reloc_kernel[my_reloc_kernel_boot_atags]) my_reloc_kernel[my_reloc_kernel_boot_atags]=image->start - KEXEC_ARM_ZIMAGE_OFFSET + KEXEC_ARM_ATAGS_OFFSET;
#ifdef CONFIG_KEXEC_HARDBOOT
	mem_text_write_kernel_word(&kexec_hardboot, image->hardboot);
#endif

	// Display informations
	printk(KERN_INFO "Kexec: machine_kexec_prepare - kexec_boot_atags  : '0x%lx'\n",(long unsigned int)kexec_boot_atags);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - image->start      : '0x%lx'\n",(long unsigned int)image->start);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - page_list         : '0x%lx'\n",(long unsigned int)page_list);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - machine_arch_type : '0x%lx'\n",(long unsigned int)machine_arch_type);
	//
	printk(KERN_INFO "Kexec: machine_kexec_prepare - kexec_start_address      ==> '0x%lx'\n",(long unsigned int)my_reloc_kernel[my_reloc_kernel_start_address]);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - kexec_indirection_page   ==> '0x%lx'\n",(long unsigned int)my_reloc_kernel[my_reloc_kernel_indirection_page]);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - kexec_mach_type          ==> '0x%lx'\n",(long unsigned int)my_reloc_kernel[my_reloc_kernel_mach_type]);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - kexec_boot_atags         ==> '0x%lx'\n",(long unsigned int)my_reloc_kernel[my_reloc_kernel_boot_atags]);
	//
	printk(KERN_INFO "Kexec: machine_kexec_prepare - reboot_code_buffer       ==> '0x%lx'\n",(long unsigned int)reboot_code_buffer);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - reboot_code_buffer_phys  ==> '0x%lx'\n",(long unsigned int)reboot_code_buffer_phys);
	printk(KERN_INFO "Kexec: machine_kexec_prepare - my_reloc_kernel          ==> '0x%lx'\n",(long unsigned int)my_reloc_kernel);
	//
	printk(KERN_INFO "Kexec: machine_kexec_prepare - relocate_new_kernel_size ==> '0x%lx'\n",(long unsigned int)relocate_new_kernel_size);
	printk(KERN_INFO "Kexec:\n");
	printk(KERN_INFO "Kexec:   (\\_/)\n");
	printk(KERN_INFO "Kexec:   (ಠ_ಠ)\n");
	printk(KERN_INFO "Kexec:   C(\")(\")     - kexec by delewer...\n");
	printk(KERN_INFO "Kexec: machine_kexec_prepare - End\n");

	return 0;
}



/*
 * Function pointer to optional machine-specific reinitialization
 */
void (*kexec_reinit)(void);

void machine_kexec(struct kimage *image)
{
//	unsigned long page_list;
	unsigned long reboot_code_buffer_phys;
	void *reboot_code_buffer;
	printk(KERN_INFO "Kexec: machine_kexec - Start - OK\n");
	arch_kexec();

//	page_list=image->head & PAGE_MASK;


	/* we need both effective and real address here */
	reboot_code_buffer_phys = page_to_pfn(image->control_code_page) << PAGE_SHIFT;
	reboot_code_buffer = page_address(image->control_code_page);

	/* Prepare parameters for reboot_code_buffer*/
	// Delewer - Juin 2014
	// Not need anymore - replaced by "my_reloc_kernel"
	//mem_text_write_kernel_word((long unsigned int *)&kexec_start_address, (long unsigned int)image->start);
	//mem_text_write_kernel_word((long unsigned int *)&kexec_indirection_page, (long unsigned int)page_list);
	//mem_text_write_kernel_word((long unsigned int *)&kexec_mach_type, (long unsigned int)machine_arch_type);
	//mem_text_write_kernel_word((long unsigned int *)&kexec_boot_atags, (long unsigned int)image->start - KEXEC_ARM_ZIMAGE_OFFSET + KEXEC_ARM_ATAGS_OFFSET);

	// copy our kernel relocation code to the control code page
	//memcpy(reboot_code_buffer, relocate_new_kernel, relocate_new_kernel_size);
	memcpy(reboot_code_buffer, my_reloc_kernel, relocate_new_kernel_size);

	
my_setup_mm_for_reboot = (void *)kallsyms_lookup_name("setup_mm_for_reboot");
my_soft_restart = (void *)kallsyms_lookup_name("soft_restart");
my_cpu_v7_reset = (void *)kallsyms_lookup_name("cpu_v7_reset");	
my_cpu_v7_proc_fin = (void *)kallsyms_lookup_name("cpu_v7_proc_fin");

	flush_icache_range((unsigned long) reboot_code_buffer, (unsigned long) reboot_code_buffer + KEXEC_CONTROL_PAGE_SIZE);


	printk(KERN_INFO "Bye!\n");

	//
	// From Z1 stock kexec
	//
	if (kexec_reinit)
		kexec_reinit();

#ifdef CONFIG_KEXEC_HARDBOOT
	/* Run any final machine-specific shutdown code. */
	if (image->hardboot && kexec_hardboot_hook)
		kexec_hardboot_hook();
#endif

//void (*my_relocate)(void) = (void *)reboot_code_buffer_phys;
//void (*my_relocate)(void) = (void *)relocate_new_kernel;
//my_relocate();

	//soft_restart(reboot_code_buffer_phys);

	//
	// From original kexec
	//
	////cpu_proc_fin();

// --- Reboot with proc_fin ?!?! - DO NOT USE -
//	my_cpu_v7_proc_fin = (void *)kallsyms_lookup_name("cpu_v7_proc_fin");
//	my_cpu_v7_proc_fin();
// --------------------------------------------

	////setup_mm_for_reboot();
	//my_setup_mm_for_reboot = (void *)kallsyms_lookup_name("setup_mm_for_reboot");
	//my_setup_mm_for_reboot();
	/////cpu_reset(reboot_code_buffer_phys);
	//my_cpu_v7_reset = (void *)kallsyms_lookup_name("cpu_v7_reset");
	//my_cpu_v7_reset(reboot_code_buffer_phys);
//	my_cpu_v7_reset(my_reloc_kernel);
//	my_cpu_v7_reset(reboot_code_buffer);
	
    //((typeof(cpu_reset) *)virt_to_phys(cpu_reset))(reboot_code_buffer_phys);

	///////////////
	// => Cause reboot ... May be cpu_proc_fin...
	my_soft_restart(reboot_code_buffer_phys);
	//my_soft_restart(reboot_code_buffer);
	//my_soft_restart(my_reloc_kernel);

return;

/* Take out a flat memory mapping. */
//setup_mm_for_reboot();
//printk(KERN_INFO "KEXEC : my_setup_mm_for_reboot\n");
my_setup_mm_for_reboot();
/* Clean and invalidate caches */
//printk(KERN_INFO "KEXEC : flush_cache_all\n");
flush_cache_all();
/* Turn off caching */ // ===>>> CAUSE REBOOT
//printk(KERN_INFO "KEXEC : cpu_proc_fin\n");
//cpu_proc_fin();
//my_cpu_v7_proc_fin();
/* Push out any further dirty data, and ensure cache is empty */
//printk(KERN_INFO "KEXEC : flush_cache_all\n");
//flush_cache_all();
/* Push out the dirty data from external caches */
//printk(KERN_INFO "KEXEC : outer_disable\n");
outer_disable();
/* Switch to the identity mapping. */
//phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
//phys_reset((unsigned long)reboot_code_buffer_phys);
//printk(KERN_INFO "KEXEC : END !!!\n");

my_cpu_v7_reset(reboot_code_buffer_phys);
//((void (*)())reboot_code_buffer_phys)();
//my_reboot_phys = (void *)reboot_code_buffer_phys;
//my_reboot_phys = (void *)reboot_code_buffer;
//my_reboot_phys = (void *)my_reloc_kernel;
//my_reboot_phys();


}
