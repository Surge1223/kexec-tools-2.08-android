#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

int main(int argc, char **argv) {

	printf("zImage-dtb generator by Munjeni @ XDA 2014\n\n");

	if (argc != 4) {
		printf("Syntax: kernel_dt ZIMAGE DT_IMAGE ZIMAGE_DTB_OUT\n\n");
		return 1;
	}

	FILE *dt, *dtout, *zimage, *zimage_dtb;
	int find_result = 0;
	char temp[11];
	char *fname_zimage = argv[1];
	char *fname_zimage_dtb = argv[3];
	char *fname_dt = argv[2];
	char *fname_dt_out = "/tmp/dtb_segment";
	unsigned long int last_address = 0;
	char dt_tmp[3];
	unsigned long int dt_address = 0;

	char buff[1];
	
	if((dt = fopen(fname_dt, "r")) == NULL) {
		printf("unable to open %s for reading!\n", fname_dt);
		return 1;
	}

	while(fread(temp, 1, 11, dt) != 0) {
		if(temp[0] == 0x8 && temp[1] == 0 && temp[2] == 0 && temp[3] == 0 &&
		   temp[4] == 0x2 && temp[5] == 0 && temp[6] == 0x2 && temp[7] == 0) {
			find_result++;
			dt_tmp[0] = temp[10];
			dt_tmp[1] = temp[9];
			dt_tmp[2] = temp[8];
		}
		if (find_result)
			goto found;
		last_address += 1;
		fseek(dt, last_address, SEEK_SET);
	}

	if(dt)
		fclose(dt);

	if(!find_result) {
		printf("\nError, couldn't find a dt segment in %s!\n", fname_dt);
		return 1;
	}

found:
	last_address += 8;
	dt_address = dt_tmp[0] * 0x10000;
	dt_address += dt_tmp[1] * 0x100;
	dt_address += dt_tmp[2];

	printf("found tag at: 0x%08lx\n", last_address);
	printf("dt address: 0x%08lx\n", dt_address);
	printf("dumping dt segment from %s ...\n", argv[2]);

	if((dtout = fopen(fname_dt_out, "w")) == NULL) {
		printf("unable to open %s for writing!\n", fname_dt_out);
		return 1;
	}

	fseek(dt, dt_address, SEEK_SET);

	while (fread(buff, 1, 1, dt) != 0) {
		fwrite(buff, 1, 1, dtout);
		dt_address += 1;
		fseek(dt, dt_address, SEEK_SET);
	}


	if(dt)
		fclose(dt);

	if(dtout)
		fclose(dtout);

	printf("%s created tempoorary\n", fname_dt_out);

	printf("making %s ...\n", argv[3]);

	if((zimage = fopen(fname_zimage, "r")) == NULL) {
		printf("unable to open %s for reading!\n", fname_zimage);
		unlink(fname_dt_out);
		return 1;
	}

	if((zimage_dtb = fopen(fname_zimage_dtb, "w")) == NULL) {
		printf("unable to open %s for writing!\n", fname_zimage_dtb);
		if(zimage)
			fclose(zimage);
		unlink(fname_dt_out);
		return 1;
	}

	if((dtout = fopen(fname_dt_out, "r")) == NULL) {
		printf("unable to open %s for reading!\n", fname_dt_out);
		if(zimage)
			fclose(zimage);
		if(zimage_dtb)
			fclose(zimage_dtb);
		unlink(fname_dt_out);
		unlink(fname_zimage_dtb);
		return 1;
	}

	while (fread(buff, 1, 1, zimage) != 0) {
		fwrite(buff, 1, 1, zimage_dtb);
	}

	while (fread(buff, 1, 1, dtout) != 0) {
		fwrite(buff, 1, 1, zimage_dtb);
	}

	if(zimage)
		fclose(zimage);

	if(zimage_dtb)
		fclose(zimage_dtb);

	if(dtout)
		fclose(dtout);

	unlink(fname_dt_out);
	printf("%s removed\nFile %s created sucesfully!", fname_dt_out, argv[3]);

   	return 0;
}
