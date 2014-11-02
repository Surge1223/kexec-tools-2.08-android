#ifndef FS2DT_H
#define FS2DT_H

#if (BOOT_BLOCK_VERSION != 2 && BOOT_BLOCK_VERSION != 17)
//#error Please add or correct definition of BOOT_BLOCK_VERSION
#define BOOT_BLOCK_VERSION 11
#endif





#define COMMAND_LINE_SIZE	1024
 //extern struct memory_ranges usablemem_rgns;
 /* boot block version 17 as defined by the linux kernel */
typedef struct mem_rgns {
	unsigned int size;
	struct memory_range *ranges;
	} mem_rgns_t;
static  mem_rgns_t usablemem_rgns;

off_t initrd_base, initrd_size;
#define BOOT_BLOCK_LAST_COMP_VERSION 10


/* boot block as defined by the linux kernel */
struct bootblock {
	unsigned magic;
	unsigned totalsize;
	unsigned off_dt_struct;
	unsigned off_dt_strings;
	unsigned off_mem_rsvmap;
	unsigned version;
	unsigned last_comp_version;
#if (BOOT_BLOCK_VERSION >= 2)
	/* version 2 fields below */
	unsigned boot_physid;
	/* version 3 fields below */
	unsigned dt_strings_size;
#if (BOOT_BLOCK_VERSION >= 17)
	/* version 17 fields below */
	unsigned dt_struct_size;
#endif
#endif
};

extern struct bootblock bb[1];

/* Used for enabling printing message from purgatory code
 * Only has implemented for PPC64 */
int my_debug;

void reserve(unsigned long long where, unsigned long long length);
void create_flatten_tree(char **, off_t *, const char *);

#endif /* KEXEC_H */