/*
    This file is part of Ciso.

    Ciso is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Ciso is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


    Copyright 2005 BOOSTER
*/


#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>               /* /usr(/local)/include/zlib.h */
#include <zconf.h>

#include "ciso.h"

const char *fname_in,*fname_out;
FILE *fin,*fout;
z_stream z;

unsigned int *index_buf = NULL;
unsigned int *crc_buf = NULL;
unsigned char *block_buf1 = NULL;
unsigned char *block_buf2 = NULL;

/****************************************************************************
	compress ISO to CSO
****************************************************************************/

CISO_H ciso;
int ciso_total_block;

unsigned long long check_file_size(FILE *fp)
{
	unsigned long long pos;

	if( fseek(fp,0,SEEK_END) < 0)
		return -1;
	pos = ftell(fp);
	if(pos==-1) return pos;

	/* init ciso header */
	memset(&ciso,0,sizeof(ciso));

	ciso.magic[0] = 'C';
	ciso.magic[1] = 'I';
	ciso.magic[2] = 'S';
	ciso.magic[3] = 'O';
	ciso.ver      = 0x01;

	ciso.block_size  = 0x800; /* ISO9660 one of sector */
	ciso.total_bytes = pos;
#if 0
	/* align >0 has bug */
	for(ciso.align = 0 ; (ciso.total_bytes >> ciso.align) >0x80000000LL ; ciso.align++);
#endif

	ciso_total_block = pos / ciso.block_size ;

	fseek(fp,0,SEEK_SET);

	return pos;
}

/****************************************************************************
	decompress CSO to ISO
****************************************************************************/
int decomp_ciso(void)
{
	unsigned long long file_size;
	unsigned int index , index2;
	unsigned long long read_pos , read_size;
	int total_sectors;
	int index_size;
	int block;
	unsigned char buf4[4];
	int cmp_size;
	int status;
	int percent_period;
	int percent_cnt;
	int plain;

	/* read header */
	if( fread(&ciso, 1, sizeof(ciso), fin) != sizeof(ciso) )
	{
		printf("file read error\n");
		return 1;
	}

	/* check header */
	if(
		ciso.magic[0] != 'C' ||
		ciso.magic[1] != 'I' ||
		ciso.magic[2] != 'S' ||
		ciso.magic[3] != 'O' ||
		ciso.block_size ==0  ||
		ciso.total_bytes == 0
	)
	{
		printf("ciso file format error\n");
		return 1;
	}
	 
	ciso_total_block = ciso.total_bytes / ciso.block_size;

	/* allocate index block */
	index_size = (ciso_total_block + 1 ) * sizeof(unsigned long);
	index_buf  = malloc(index_size);
	block_buf1 = malloc(ciso.block_size);
	block_buf2 = malloc(ciso.block_size*2);

	if( !index_buf || !block_buf1 || !block_buf2 )
	{
		printf("Can't allocate memory\n");
		return 1;
	}
	memset(index_buf,0,index_size);

	/* read index block */
	if( fread(index_buf, 1, index_size, fin) != index_size )
	{
		printf("file read error\n");
		return 1;
	}

	/* show info */
	printf("Decompress '%s' to '%s'\n",fname_in,fname_out);
	printf("Total File Size %ld bytes\n",ciso.total_bytes);
	printf("block size      %d  bytes\n",ciso.block_size);
	printf("total blocks    %d  blocks\n",ciso_total_block);
	printf("index align     %d\n",1<<ciso.align);

	/* init zlib */
	z.zalloc = Z_NULL;
	z.zfree  = Z_NULL;
	z.opaque = Z_NULL;

	/* decompress data */
	percent_period = ciso_total_block/100;
	percent_cnt = 0;

	for(block = 0;block < ciso_total_block ; block++)
	{
		if(--percent_cnt<=0)
		{
			percent_cnt = percent_period;
			printf("decompress %d%%\r",block / percent_period);
		}

		if (inflateInit2(&z,-15) != Z_OK)
		{
			printf("deflateInit : %s\n", (z.msg) ? z.msg : "???");
			return 1;
		}

		/* check index */
		index  = index_buf[block];
		plain  = index & 0x80000000;
		index  &= 0x7fffffff;
		read_pos = index << (ciso.align);
		if(plain)
		{
			read_size = ciso.block_size;
		}
		else
		{
			index2 = index_buf[block+1] & 0x7fffffff;
			read_size = (index2-index) << (ciso.align);
		}
		fseek(fin,read_pos,SEEK_SET);

		z.avail_in  = fread(block_buf2, 1, read_size , fin);
		if(z.avail_in != read_size)
		{
			printf("block=%d : read error\n",block);
			return 1;
		}

		if(plain)
		{
			memcpy(block_buf1,block_buf2,read_size);
			cmp_size = read_size;
		}
		else
		{
			z.next_out  = block_buf1;
			z.avail_out = ciso.block_size;
			z.next_in   = block_buf2;
			status = inflate(&z, Z_FULL_FLUSH);
			if (status != Z_STREAM_END)
			/*if (status != Z_OK)*/
			{
				printf("block %d:inflate : %s[%d]\n", block,(z.msg) ? z.msg : "error",status);
				return 1;
			}
			cmp_size = ciso.block_size - z.avail_out;
			if(cmp_size != ciso.block_size)
			{
				printf("block %d : block size error %d != %d\n",block,cmp_size , ciso.block_size);
				return 1;
			}
		}
		/* write decompressed block */
		if(fwrite(block_buf1, 1,cmp_size , fout) != cmp_size)
		{
			printf("block %d : Write error\n",block);
			return 1;
		}

		/* term zlib */
		if (inflateEnd(&z) != Z_OK)
		{
			printf("inflateEnd : %s\n", (z.msg) ? z.msg : "error");
			return 1;
		}
	}

	printf("ciso decompress completed\n");
	return 0;
}

/****************************************************************************
	compress ISO
****************************************************************************/
int comp_ciso(int level)
{
	unsigned long long file_size;
	unsigned long long write_pos;
	int total_sectors;
	int index_size;
	int block;
	unsigned char buf4[64];
	int cmp_size;
	int status;
	int percent_period;
	int percent_cnt;
	int align,align_b,align_m;

	file_size = check_file_size(fin);
	if(file_size<0)
	{
		printf("Can't get file size\n");
		return 1;
	}

	/* allocate index block */
	index_size = (ciso_total_block + 1 ) * sizeof(unsigned long);
	index_buf  = malloc(index_size);
	crc_buf    = malloc(index_size);
	block_buf1 = malloc(ciso.block_size);
	block_buf2 = malloc(ciso.block_size*2);

	if( !index_buf || !crc_buf || !block_buf1 || !block_buf2 )
	{
		printf("Can't allocate memory\n");
		return 1;
	}
	memset(index_buf,0,index_size);
	memset(crc_buf,0,index_size);
	memset(buf4,0,sizeof(buf4));

	/* init zlib */
	z.zalloc = Z_NULL;
	z.zfree  = Z_NULL;
	z.opaque = Z_NULL;

	/* show info */
	printf("Compress '%s' to '%s'\n",fname_in,fname_out);
	printf("Total File Size %ld bytes\n",ciso.total_bytes);
	printf("block size      %d  bytes\n",ciso.block_size);
	printf("index align     %d\n",1<<ciso.align);
	printf("compress level  %d\n",level);

	/* write header block */
	fwrite(&ciso,1,sizeof(ciso),fout);

	/* dummy write index block */
	fwrite(index_buf,1,index_size,fout);

	write_pos = sizeof(ciso) + index_size;

	/* compress data */
	percent_period = ciso_total_block/100;
	percent_cnt    = ciso_total_block/100;

	align_b = 1<<(ciso.align);
	align_m = align_b -1;

	for(block = 0;block < ciso_total_block ; block++)
	{
		if(--percent_cnt<=0)
		{
			percent_cnt = percent_period;
			printf("compress %3d%% avarage rate %3d%%\r"
				,block / percent_period
				,block==0 ? 0 : 100*write_pos/(block*0x800));
		}

		if (deflateInit2(&z, level , Z_DEFLATED, -15,8,Z_DEFAULT_STRATEGY) != Z_OK)
		{
			printf("deflateInit : %s\n", (z.msg) ? z.msg : "???");
			return 1;
		}

		/* write align */
		align = (int)write_pos & align_m;
		if(align)
		{
			align = align_b - align;
			if(fwrite(buf4,1,align, fout) != align)
			{
				printf("block %d : Write error\n",block);
				return 1;
			}
			write_pos += align;
		}

		/* mark offset index */
		index_buf[block] = write_pos>>(ciso.align);

		/* read buffer */
		z.next_out  = block_buf2;
		z.avail_out = ciso.block_size*2;
		z.next_in   = block_buf1;
		z.avail_in  = fread(block_buf1, 1, ciso.block_size , fin);
		if(z.avail_in != ciso.block_size)
		{
			printf("block=%d : read error\n",block);
			return 1;
		}

		/* compress block
		status = deflate(&z, Z_FULL_FLUSH);*/
		status = deflate(&z, Z_FINISH);
		if (status != Z_STREAM_END)
	/*	if (status != Z_OK) */
		{
			printf("block %d:deflate : %s[%d]\n", block,(z.msg) ? z.msg : "error",status);
			return 1;
		}

		cmp_size = ciso.block_size*2 - z.avail_out;

		/* choise plain / compress */
		if(cmp_size >= ciso.block_size)
		{
			cmp_size = ciso.block_size;
			memcpy(block_buf2,block_buf1,cmp_size);
			/* plain block mark */
			index_buf[block] |= 0x80000000;
		}

		/* write compressed block */
		if(fwrite(block_buf2, 1,cmp_size , fout) != cmp_size)
		{
			printf("block %d : Write error\n",block);
			return 1;
		}

		/* mark next index */
		write_pos += cmp_size;

		/* term zlib */
		if (deflateEnd(&z) != Z_OK)
		{
			printf("deflateEnd : %s\n", (z.msg) ? z.msg : "error");
			return 1;
		}
	}

	/* last position (total size)*/
	index_buf[block] = write_pos>>(ciso.align);

	/* write header & index block */
	fseek(fout,sizeof(ciso),SEEK_SET);
	fwrite(index_buf,1,index_size,fout);

	printf("ciso compress completed , total size = %8d bytes , rate %d%%\n"
		,(int)write_pos,(int)(write_pos*100/ciso.total_bytes));
	return 0;
}

/****************************************************************************
	main
****************************************************************************/
int main(int argc, char *argv[])
{
	int level;
	int result;

	fprintf(stderr, "Compressed ISO9660 converter Ver.1.01 by BOOSTER\n");

	if (argc != 4)
	{
		printf("Usage: ciso level infile outfile\n");
		printf("  level: 1-9 compress ISO to CSO (1=fast/large - 9=small/slow\n");
		printf("         0   decompress CSO to ISO\n");
		return 0;
	}
	level = argv[1][0] - '0';
	if(level < 0 || level > 9)
	{
        printf("Unknown mode: %c\n", argv[1][0]);
		return 1;
	}

	fname_in = argv[2];
	fname_out = argv[3];

	if ((fin = fopen(fname_in, "rb")) == NULL)
	{
		printf("Can't open %s\n", fname_in);
		return 1;
	}
	if ((fout = fopen(fname_out, "wb")) == NULL)
	{
		printf("Can't create %s\n", fname_out);
		return 1;
	}

	if(level==0)
		result = decomp_ciso();
	else
		result = comp_ciso(level);

	/* free memory */
	if(index_buf) free(index_buf);
	if(crc_buf) free(crc_buf);
	if(block_buf1) free(block_buf1);
	if(block_buf2) free(block_buf2);

	/* close files */
	fclose(fin);
	fclose(fout);
	return result;
}
