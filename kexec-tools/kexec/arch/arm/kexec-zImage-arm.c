/*
 * - 08/21/2007 ATAG support added by Uli Luckas <u.luckas@road.de>
 *
 */
#define _GNU_SOURCE
#define _XOPEN_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <libfdt.h>
#include <getopt.h>
#include <unistd.h>
#include <arch/options.h>
#include "../../kexec.h"
#include "../../kexec-syscall.h"
#include "kexec-arm.h"
#include "../../fs2dt.h"
#include "crashdump-arm.h"

#define BOOT_PARAMS_SIZE 1536


char *scanmem;long unsigned int sizescan;long unsigned int ISC;
#define MEMSCAN fprintf(stderr,"\n***Start scan\n");for (ISC=0;ISC<sizescan;ISC++){fprintf(stderr,"%.1s",scanmem++);}fprintf(stderr,"\n***End scan\n\n");

off_t initrd_base = 0, initrd_size = 0;

struct tag_header {
	uint32_t size;
	uint32_t tag;
};

/* The list must start with an ATAG_CORE node */
#define ATAG_CORE       0x54410001

struct tag_core {
	uint32_t flags;	    /* bit 0 = read-only */
	uint32_t pagesize;
	uint32_t rootdev;
};

/* it is allowed to have multiple ATAG_MEM nodes */
#define ATAG_MEM	0x54410002

struct tag_mem32 {
	uint32_t   size;
	uint32_t   start;  /* physical start address */
};

/* describes where the compressed ramdisk image lives (virtual address) */
/*
 * this one accidentally used virtual addresses - as such,
 * it's deprecated.
 */
#define ATAG_INITRD     0x54410005

/* describes where the compressed ramdisk image lives (physical address) */
#define ATAG_INITRD2    0x54420005

struct tag_initrd {
        uint32_t start;    /* physical start address */
        uint32_t size;     /* size of compressed ramdisk image in bytes */
};

/* command line: \0 terminated string */
#define ATAG_CMDLINE    0x54410009

struct tag_cmdline {
	char    cmdline[1];     /* this is the minimum size */
};

/* The list ends with an ATAG_NONE node. */
#define ATAG_NONE       0x00000000

struct tag {
	struct tag_header hdr;
	union {
		struct tag_core	 core;
		struct tag_mem32	mem;
		struct tag_initrd       initrd;
		struct tag_cmdline      cmdline;
	} u;
};

#define tag_next(t)     ((struct tag *)((uint32_t *)(t) + (t)->hdr.size))
#define byte_size(t)    ((t)->hdr.size << 2)
#define tag_size(type)  ((sizeof(struct tag_header) + sizeof(struct type) + 3) >> 2)

int zImage_arm_probe(const char *UNUSED(buf), off_t UNUSED(len))
{
	/* 
	 * Only zImage loading is supported. Do not check if
	 * the buffer is valid kernel image
	 */	
	return 0;
}

void zImage_arm_usage(void)
{
	printf(	"     --command-line=STRING Set the kernel command line to STRING.\n"
		"     --append=STRING       Set the kernel command line to STRING.\n"
		"     --initrd=FILE         Use FILE as the kernel's initial ramdisk.\n"
		"     --ramdisk=FILE        Use FILE as the kernel's initial ramdisk.\n"
		"     --dtb=FILE            Use FILE as the fdt blob.\n"
		"     --atags               Use ATAGs instead of device-tree.\n"
		);
}


// Delewer - Decembre 2013
static
struct tag * atag_read_tags(void)
{
	static unsigned long buf[BOOT_PARAMS_SIZE];
	const char fn[]= "/proc/atags";
	FILE *fp;
	fp = fopen(fn, "r");
	if (!fp) { const char fn2[]= "/data/atags"; FILE *fp2; fp2 = fopen(fn2, "r");
		if (!fp2) { /*fprintf(stderr, "Cannot open /proc/atags : %s\n", strerror(errno));*/ return NULL; }
		if (!fread(buf, sizeof(buf[1]), BOOT_PARAMS_SIZE, fp2)) { fclose(fp2); return NULL; }
		if (ferror(fp2)) { fprintf(stderr, "Cannot read %s: %s\n", fn2, strerror(errno)); fclose(fp); return NULL; }
		fclose(fp2); return (struct tag *) buf;
	}
	if (!fread(buf, sizeof(buf[1]), BOOT_PARAMS_SIZE, fp)) { fclose(fp); return NULL; }
	if (ferror(fp)) { fprintf(stderr, "Cannot read %s: %s\n", fn, strerror(errno));
		fclose(fp);
		return NULL; }
	fclose(fp);
	return (struct tag *) buf;
}


static
void atag_write_tags(char *buf, off_t len)
{
	const char fn[]= "/data/atags";
	FILE *fp;
	fp = fopen(fn, "w");
	if (!fp) { fprintf(stderr, "Cannot open for write %s: %s\n", fn, strerror(errno)); return; }
	fwrite(buf, sizeof(off_t), len, fp);
	fclose(fp);
	return;
}


static
int atag_arm_load(struct kexec_info *info, unsigned long base,
	const char *command_line, off_t command_line_len,
	const char *initrd, off_t initrd_len, off_t initrd_off)
{
	struct tag *saved_tags = atag_read_tags();
	char *buf;
	off_t len;
	struct tag *params;
	uint32_t *initrd_start = NULL;
	
	buf = xmalloc(getpagesize());
	if (!buf) {
		fprintf(stderr, "Compiling ATAGs: out of memory\n");
		return -1;
	}

// Delewer - Décembre 2013
	memset(buf, 0xff, getpagesize());
//	memset(buf, 0x00, getpagesize());
	params = (struct tag *)buf;

	if (saved_tags) {
		// Copy tags
		saved_tags = (struct tag *) saved_tags;
		while(byte_size(saved_tags)) {
			switch (saved_tags->hdr.tag) {
			case ATAG_INITRD:
			case ATAG_INITRD2:
			case ATAG_CMDLINE:
			case ATAG_NONE:
				// skip these tags
				break;
			default:
				// copy all other tags
				memcpy(params, saved_tags, byte_size(saved_tags));
				params = tag_next(params);
			}
			saved_tags = tag_next(saved_tags);
		}
	} else {
		params->hdr.size = 2;
		params->hdr.tag = ATAG_CORE;
		params = tag_next(params);
	}

	if (initrd) {
		params->hdr.size = tag_size(tag_initrd);
		params->hdr.tag = ATAG_INITRD2;
		initrd_start = &params->u.initrd.start;
		params->u.initrd.size = initrd_len;
		params = tag_next(params);
	}

	if (command_line) {
		params->hdr.size = (sizeof(struct tag_header) + command_line_len + 3) >> 2;
		params->hdr.tag = ATAG_CMDLINE;
		memcpy(params->u.cmdline.cmdline, command_line,
			command_line_len);
		params->u.cmdline.cmdline[command_line_len - 1] = '\0';
		params = tag_next(params);
	}

	params->hdr.size = 0;
	params->hdr.tag = ATAG_NONE;

	len = ((char *)params - buf) + sizeof(struct tag_header);
//scanmem=info;sizescan=sizeof(info);MEMSCAN
//scanmem=buf;sizescan=len;MEMSCAN
//    base=xrealloc(base,len); 
	add_segment(info, buf, len, base, len);
//fprintf(stderr, "XREALLOC : base : %p  - length : %p \n",base, len);
//scanmem=base;sizescan=len;MEMSCAN;

	
	//atag_write_tags(buf, len);

	if (initrd) {
		*initrd_start = locate_hole(info, initrd_len, getpagesize(),
				initrd_off, ULONG_MAX, INT_MAX);
		if (*initrd_start == ULONG_MAX) 
			return -1;
		*initrd_start &= ~(getpagesize() - 1);
		add_segment(info, initrd, initrd_len, *initrd_start, initrd_len);
	}

	return 0;
}


int zImage_arm_load(int argc, char **argv, const char *buf, off_t len,
	struct kexec_info *info)
{
	unsigned long base;
	unsigned int atag_offset = 0x1000; /* 4k offset from memory start */
//	unsigned int offset = 0x8000;      /* 32k offset from memory start */
//	unsigned int offset = 0x4000;
//	unsigned int offset = 0x1000;
	unsigned int offset = 0x0000;	
	const char *command_line;
	char *modified_cmdline = NULL;
	off_t command_line_len;
	const char *ramdisk;
	char *ramdisk_buf;
	//off_t ramdisk_length;
	//off_t ramdisk_offset;
	int opt;
	int use_atags;
	char *dtb_buf;
	off_t dtb_length;
	char *dtb_file;
	off_t dtb_offset;

	/* See options.h -- add any more there, too. */
	static const struct option options[] = {
		KEXEC_ARCH_OPTIONS
		{ "command-line",	1, 0, OPT_APPEND },
		{ "append",		1, 0, OPT_APPEND },
		{ "initrd",		1, 0, OPT_RAMDISK },
		{ "ramdisk",		1, 0, OPT_RAMDISK },
		{ "dtb",		1, 0, OPT_DTB },
		{ "atags",		0, 0, OPT_ATAGS },
		{ 0, 			0, 0, 0 },
	};
	static const char short_options[] = KEXEC_ARCH_OPT_STR "a:r:";


	/*
	 * Parse the command line arguments
	 */
	command_line = 0;
	command_line_len = 0;
	ramdisk = 0;
	ramdisk_buf = 0;
	//ramdisk_length = 0;
	initrd_size = 0;
	use_atags = 0;
	dtb_file = NULL;
	while((opt = getopt_long(argc, argv, short_options, options, 0)) != -1) {
		switch(opt) {
		default:
			/* Ignore core options */
			if (opt < OPT_ARCH_MAX) {
				break;
			}
		case '?':
			usage();
			return -1;
		case OPT_APPEND:
			command_line = optarg;
			break;
		case OPT_RAMDISK:
			ramdisk = optarg;
			break;
		case OPT_DTB:
			dtb_file = optarg;
			break;
		case OPT_ATAGS:
			use_atags = 1;
			break;
		}
	}

	if (use_atags && dtb_file) {
		fprintf(stderr, "You can only use ATAGs if you don't specify a "
		        "dtb file.\n");
		return -1;
	}

	if (command_line) {
		command_line_len = strlen(command_line) + 1;
		if (command_line_len > COMMAND_LINE_SIZE)
			command_line_len = COMMAND_LINE_SIZE;
	}
	if (ramdisk) {
		//ramdisk_buf = slurp_file(ramdisk, &ramdisk_length);
		ramdisk_buf = slurp_file(ramdisk, &initrd_size);
	}

	/*
	 * If we are loading a dump capture kernel, we need to update kernel
	 * command line and also add some additional segments.
	 */
	if (info->kexec_flags & KEXEC_ON_CRASH) {
		uint64_t start, end;

		modified_cmdline = xmalloc(COMMAND_LINE_SIZE);
		if (!modified_cmdline)
			return -1;

		if (command_line) {
			(void) strncpy(modified_cmdline, command_line,
				       COMMAND_LINE_SIZE);
			modified_cmdline[COMMAND_LINE_SIZE - 1] = '\0';
		}

		if (load_crashdump_segments(info, modified_cmdline) < 0) {
			free(modified_cmdline);
			return -1;
		}

		command_line = modified_cmdline;
		command_line_len = strlen(command_line) + 1;

		/*
		 * We put the dump capture kernel at the start of crashkernel
		 * reserved memory.
		 */
		if (parse_iomem_single("Crash kernel\n", &start, &end)) {
			/*
			 * No crash kernel memory reserved. We cannot do more
			 * but just bail out.
			 */
			return -1;
		}
		base = start;
	} else {
		base = locate_hole(info,len+offset,0,0,ULONG_MAX,INT_MAX);
	}

	if (base == ULONG_MAX)
		return -1;

	/* assume the maximum kernel compression ratio is 4,
	 * and just to be safe, place ramdisk after that
	 */
	//ramdisk_offset = base + len * 4;
	initrd_base = base + len * 4;

	//if (atag_arm_load(info, base + atag_offset,
	//		 command_line, command_line_len,
	//		 ramdisk_buf, ramdisk_length, ramdisk_offset) == -1)
	//	return -1;
	if (use_atags) {
		/*
		 * use ATAGs from /proc/atags
		 */
		if (atag_arm_load(info, base + atag_offset,
		                  command_line, command_line_len,
		                  ramdisk_buf, initrd_size, initrd_base) == -1)
			return -1;
	} else {
		/*
		 * Read a user-specified DTB file.
		 */
		if (dtb_file) {
			dtb_buf = slurp_file(dtb_file, &dtb_length);
			long int dtb_buf_found;
			long int scan_loop_dtb;
			long int scan_dtb_length;
fprintf(stderr, "kexec-zImage-arm : dtb.img BEFORE CUT : Start : '0x%08lx'  - Length : '0x%06lx'  - End : '0x%08lx'\n",(long unsigned int)dtb_buf,(long unsigned int)dtb_length,(long unsigned int)(dtb_buf+dtb_length));
			/* 
			 * Recalcul de la position de départ du device_tree
			 * et réattribution de la longueur totale
			 */
			
			// On cherche la première occurence
			/*for (scan_loop_dtb=0;scan_loop_dtb<dtb_length;scan_loop_dtb++)
				{
				if (fdt_magic(dtb_buf+scan_loop_dtb) == FDT_MAGIC){dtb_buf=dtb_buf+scan_loop_dtb;scan_loop_dtb=dtb_length;}
				}  */
			// On cherche la dernière occurence
			// Search for last DTB
			for (scan_loop_dtb=0;scan_loop_dtb<dtb_length;scan_loop_dtb++)
				{
				if (fdt_magic(dtb_buf+scan_loop_dtb) == FDT_MAGIC){dtb_buf_found = (long int)dtb_buf + scan_loop_dtb ; scan_dtb_length = (long int)dtb_length - scan_loop_dtb ;}
				}
			// Fill with 0 unused DTB
			for (scan_loop_dtb=0;scan_loop_dtb<(dtb_buf_found - (long int)dtb_buf - 1);scan_loop_dtb++)
				{
				dtb_buf[scan_loop_dtb]=0 ;
				}
			//Last DTB OK
			dtb_buf=(char *)dtb_buf_found;
			dtb_length=scan_dtb_length;
			// On cherche la taille du dtb
			/*for (scan_loop_dtb=2;scan_loop_dtb<dtb_length;scan_loop_dtb++)
				{
				if (fdt_magic(dtb_buf+scan_loop_dtb) == FDT_MAGIC){dtb_length=scan_loop_dtb;}
				} */
		} else {
			/*
			 * Extract the DTB from /proc/device-tree.
			 */
			create_flatten_tree(&dtb_buf, &dtb_length, command_line);
		}

		if (fdt_check_header(dtb_buf) != 0) {
			fprintf(stderr, "Invalid FDT buffer.\n");
			return -1;
		}

/*		if (base + dtb_offset + dtb_length > base + offset) {
			fprintf(stderr, "kexec-zImage-arm : base = '0x%lx'\n",base);
			fprintf(stderr, "kexec-zImage-arm : dtb_offset = '0x%lx'\n",dtb_offset);
			fprintf(stderr, "kexec-zImage-arm : dtb_length = '0x%lx'\n",dtb_length);
			fprintf(stderr, "kexec-zImage-arm : offset = '0x%lx'\n",offset);
			fprintf(stderr, "(base + dtb_offset + dtb_length) > (base + offset)  :::: '0x%lx' > '0x%lx'\n",base + atag_offset + dtb_length , base + offset);
			fprintf(stderr, "DTB too large!\n");
			return -1;
		}
*/
		if (ramdisk) {
		initrd_base &= ~(getpagesize() - 1);
		add_segment(info, ramdisk_buf, initrd_size,
			            initrd_base, initrd_size);
		}


		/* Stick the dtb at the end of the initrd and page
		 * align it.
		 */
//		dtb_offset = initrd_base + initrd_size + getpagesize();    
		//dtb_offset = ((char *)info->segment[0].mem) + info->segment[0].memsz + getpagesize();
		dtb_offset = ((long unsigned int)info->segment[0].mem) + info->segment[0].memsz + getpagesize();
		dtb_offset &= ~(getpagesize() - 1);

		add_segment(info, dtb_buf, dtb_length,
		            dtb_offset, dtb_length);
	}

	//add_segment(info, buf, len, base, len);
	add_segment(info, buf, len, base + offset, len);
	
	info->entry = (void*)base + offset;

//////////////////////////////////////////////
// Extraction de la totalité pour expertise
//
//   | /////////////////////// | ///////////////////////////// | ////////////////////// | /////////////////////////////
//   |   --- len ---           |     --- initrd_size ---       |    --- dtb_length ---  |     --- command_line_len ---
//   | /////////////////////// | ///////////////////////////// | ////////////////////// | /////////////////////////////
//   ^-base (zImage)           ^-initrd_base (initrd)          ^-dtb_offset (dt.img)    ^-command_line
//

fprintf(stderr, "\n");
fprintf(stderr, "   | /////////////////////// | ///////////////////////////// | ////////////////////// | /////////////////////////////  \n");
fprintf(stderr, "   |   --- len ---           |     --- initrd_size ---       |    --- dtb_length ---  |     --- command_line_len ---   \n");
fprintf(stderr, "   | /////////////////////// | ///////////////////////////// | ////////////////////// | /////////////////////////////  \n");
fprintf(stderr, "   ^-base (zImage)           ^-initrd_base (initrd)          ^-dtb_offset (dt.img)    ^-command_line                   \n");
fprintf(stderr, "\n");
fprintf(stderr, "kexec-zImage-arm : ---LOAD IN MEMORY-----------------------------------------\n");
fprintf(stderr, "kexec-zImage-arm : zImage         : '0x%08lx'    - '0x%06lx' length\n",(long unsigned int)buf,(long unsigned int)len);
fprintf(stderr, "kexec-zImage-arm : initrd         : '0x%08lx'    - '0x%06lx' length\n",(long unsigned int)ramdisk_buf,(long unsigned int)initrd_size);
fprintf(stderr, "kexec-zImage-arm : dtb.img        : '0x%08lx'    - '0x%06lx' length\n",(long unsigned int)dtb_buf,(long unsigned int)dtb_length);
fprintf(stderr, "kexec-zImage-arm : command_line   : '0x%08lx'    - '0x%06lx' length\n",(long unsigned int)command_line,(long unsigned int)command_line_len);
fprintf(stderr, "kexec-zImage-arm : \n");
fprintf(stderr, "kexec-zImage-arm : ---RELOCATE ADDRESSES-------------------------------------\n");
fprintf(stderr, "kexec-zImage-arm : zImage         : Start : '0x%08lx'  - Length : '0x%06lx'  - End : '0x%08lx'\n",(long unsigned int)base,(long unsigned int)len,(long unsigned int)(base+len));
fprintf(stderr, "kexec-zImage-arm : initrd         : Start : '0x%08lx'  - Length : '0x%06lx'  - End : '0x%08lx'\n",(long unsigned int)initrd_base,(long unsigned int)initrd_size,(long unsigned int)(initrd_base+initrd_size));
fprintf(stderr, "kexec-zImage-arm : dtb.img        : Start : '0x%08lx'  - Length : '0x%06lx'  - End : '0x%08lx'\n",(long unsigned int)dtb_offset,(long unsigned int)dtb_length,(long unsigned int)(dtb_offset+dtb_length));
fprintf(stderr, "kexec-zImage-arm : ---ENTRY POINT--------------------------------------------\n");
fprintf(stderr, "kexec-zImage-arm : zImage         : '0x%08lx'\n",(long unsigned int)info->entry);
fprintf(stderr, "kexec-zImage-arm : ----------------------------\n\n");

//
//
/*
fprintf(stderr, "kexec-zImage-arm : Write /data/kexec_extr_stock_kernel...\n");
const char *fn= "/data/kexec_extr_stock_kernel";
FILE *fp;
fp = fopen(fn, "w");
if (!fp) { fprintf(stderr, "Cannot open for write %s: %s\n", fn, strerror(errno)); return; }
//fwrite(0x0, 1, 0x7afffff, fp);
//fwrite(base, 1, len, fp);
fwrite(base, 1, 0x7ad98b, fp);
fclose(fp);
//
fprintf(stderr, "kexec-zImage-arm : Write /data/kexec_extr_cmdline...\n");
const char *fn2= "/data/kexec_extr_cmdline";
FILE *fp2;
fp2 = fopen(fn2, "w");
if (!fp2) { fprintf(stderr, "Cannot open for write %s: %s\n", fn2, strerror(errno)); return; }
//fwrite(info->entry, sizeof(void*), len, fp2);
//fwrite(buf, sizeof(void*), len, fp2);
fwrite(command_line, 1, command_line_len, fp2);
fclose(fp2);
//
fprintf(stderr, "kexec-zImage-arm : Write /data/kexec_extr_dt...\n");
const char *fn3= "/data/kexec_extr_dt";
FILE *fp3 = fopen(fn3, "w");
if (!fp3) { fprintf(stderr, "Cannot open for write %s: %s\n", fn3, strerror(errno)); return; }
//fwrite(dtb_buf, 1, dtb_length, fp3);
fwrite(dtb_offset, 1, dtb_length, fp3);
fclose(fp3);
//
fprintf(stderr, "kexec-zImage-arm : Write /data/kexec_extr_initrd...\n");
const char *fn4= "/data/kexec_extr_initrd";
FILE *fp4 = fopen(fn4, "w");
if (!fp4) { fprintf(stderr, "Cannot open for write %s: %s\n", fn4, strerror(errno)); return; }
//fwrite(ramdisk_buf, 1, initrd_size, fp4);
fwrite(initrd_base, 1, initrd_size, fp4);
fclose(fp4);
//////////////////////////////////////////////
*/
	return 0;
}
