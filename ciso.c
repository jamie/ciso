//Compressed ISO9660 converter Ver.1.01
//Copyright (C) 2005 BOOSTER
//
//Cisco is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either version 2
//of the License, or (at your option) any later version.
//
//Cisco is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with Cisco; if not, write to the Free Software
//Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h> /* /usr(/local)/include/zlib.h */

#include "ciso.h"

const char *fname_in, *fname_out;
FILE *fin, *fout;
z_stream z;

uint32_t *index_buf = NULL;
uint32_t *crc_buf = NULL;
uint8_t *block_buf1 = NULL;
uint8_t *block_buf2 = NULL;

CISO_H ciso;

int decomp_ciso(void)
{
	uint32_t index1, index2;
	int32_t read_pos;
	uint32_t read_size;
	uint32_t block;
	uint32_t cmp_size;
	uint32_t plain;

	/* read header */
	if (fread(&ciso, 1, sizeof(ciso), fin) != sizeof(ciso))
	{
		printf("file read error\n");
		return 1;
	}

	/* check header */
	if (
		ciso.magic[0] != 'C' ||
		ciso.magic[1] != 'I' ||
		ciso.magic[2] != 'S' ||
		ciso.magic[3] != 'O' ||
		ciso.block_size == 0 ||
		ciso.total_bytes == 0
	)
	{
		printf("ciso file format error\n");
		return 1;
	}
	 
	uint32_t ciso_total_block = ciso.total_bytes / ciso.block_size;
	uint32_t index_size = (ciso_total_block + 1) * sizeof(uint32_t);

	/* allocate index block */
	index_buf  = malloc(index_size);
	block_buf1 = malloc(ciso.block_size);
	block_buf2 = malloc(ciso.block_size * 2);

	if (!index_buf || !block_buf1 || !block_buf2)
	{
		printf("Can't allocate memory\n");
		return 1;
	}
	memset(index_buf, 0, index_size);

	/* read index block */
	if (fread(index_buf, 1, index_size, fin) != index_size)
	{
		printf("file read error\n");
		return 1;
	}

	/* show info */
	printf("Decompress '%s' to '%s'\n", fname_in, fname_out);
	printf("Total File Size %u bytes\n", ciso.total_bytes);
	printf("block size      %u  bytes\n", ciso.block_size);
	printf("total blocks    %u  blocks\n", ciso_total_block);
	printf("index align     %d\n", 1 << ciso.align);

	/* init zlib */
	z.zalloc = Z_NULL;
	z.zfree  = Z_NULL;
	z.opaque = Z_NULL;

	/* decompress data */
	uint32_t percent_period = ciso_total_block / 100;
	int32_t percent_cnt = 0;

	for (block = 0; block < ciso_total_block; block++)
	{
		if (--percent_cnt <= 0)
		{
			percent_cnt = percent_period;
			printf("decompress %u%%\n", block / percent_period);
		}

		if (inflateInit2(&z, -15) != Z_OK)
		{
			printf("deflateInit : %s\n", (z.msg) ? z.msg : "???");
			return 1;
		}

		/* check index */
		index1  = index_buf[block];
		plain  = index1 & 0x80000000;
		index1  &= 0x7fffffff;
		read_pos = index1 << (ciso.align);
		if (plain)
		{
			read_size = ciso.block_size;
		}
		else
		{
			index2 = index_buf[block + 1] & 0x7fffffff;
			read_size = (index2 - index1) << (ciso.align);
		}
		fseek(fin, read_pos, SEEK_SET);

		z.avail_in  = fread(block_buf2, 1, read_size, fin);
		if (z.avail_in != read_size)
		{
			printf("block=%u : read error\n", block);
			return 1;
		}

		if (plain)
		{
			memcpy(block_buf1, block_buf2, read_size);
			cmp_size = read_size;
		}
		else
		{
			z.next_out  = block_buf1;
			z.avail_out = ciso.block_size;
			z.next_in   = block_buf2;
			int32_t status = inflate(&z, Z_FULL_FLUSH);
			if (status != Z_STREAM_END)
			{
				printf("block %u:inflate : %s[%d]\n", block, (z.msg) ? z.msg : "error", status);
				return 1;
			}
			cmp_size = ciso.block_size - z.avail_out;
			if (cmp_size != ciso.block_size)
			{
				printf("block %u : block size error %u != %u\n", block, cmp_size, ciso.block_size);
				return 1;
			}
		}
		/* write decompressed block */
		if (fwrite(block_buf1, 1, cmp_size, fout) != cmp_size)
		{
			printf("block %u : Write error\n", block);
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

int comp_ciso(int level)
{
	uint32_t block;
	uint8_t buf4[64];
	uint32_t cmp_size;

	if (fseek(fin, 0, SEEK_END) != 0)
	{
		printf("Can't get file size\n");
		return 1;
	}
	
	int32_t file_size = ftell(fin);
	if (file_size == -1)
	{
		printf("Can't get file size\n");
		return 1;
	}
	
	/* init ciso header */
	memset(&ciso, 0, sizeof(ciso));
	
	ciso.magic[0] = 'C';
	ciso.magic[1] = 'I';
	ciso.magic[2] = 'S';
	ciso.magic[3] = 'O';
	ciso.ver      = 0x01;
	
	ciso.block_size  = 0x800; /* ISO9660 one of sector */
	ciso.total_bytes = file_size;
#if 0
	/* align > 0 has bug */
	for (ciso.align = 0; (ciso.total_bytes >> ciso.align) > 0x80000000LL; ciso.align++);
#endif
	
	uint32_t ciso_total_block = ciso.total_bytes / ciso.block_size;
	uint32_t index_size = (ciso_total_block + 1) * sizeof(uint32_t);
	
	fseek(fin, 0, SEEK_SET);

	/* allocate index block */
	index_buf  = malloc(index_size);
	crc_buf    = malloc(index_size);
	block_buf1 = malloc(ciso.block_size);
	block_buf2 = malloc(ciso.block_size * 2);

	if (!index_buf || !crc_buf || !block_buf1 || !block_buf2)
	{
		printf("Can't allocate memory\n");
		return 1;
	}
	memset(index_buf, 0, index_size);
	memset(crc_buf, 0, index_size);
	memset(buf4, 0, sizeof(buf4));

	/* init zlib */
	z.zalloc = Z_NULL;
	z.zfree  = Z_NULL;
	z.opaque = Z_NULL;

	/* show info */
	printf("Compress '%s' to '%s'\n", fname_in, fname_out);
	printf("Total File Size %u bytes\n", ciso.total_bytes);
	printf("block size      %u  bytes\n", ciso.block_size);
	printf("index align     %d\n", 1 << ciso.align);
	printf("compress level  %d\n", level);

	/* write header block */
	fwrite(&ciso, 1, sizeof(ciso), fout);

	/* dummy write index block */
	fwrite(index_buf, 1, index_size, fout);

	uint32_t write_pos = sizeof(ciso) + index_size;

	/* compress data */
	uint32_t percent_period = ciso_total_block / 100;
	int32_t percent_cnt = ciso_total_block / 100;

	int32_t align_b = 1 << (ciso.align);
	int32_t align_m = align_b - 1;

	for (block = 0; block < ciso_total_block; block++)
	{
		if (--percent_cnt <= 0)
		{
			percent_cnt = percent_period;
			printf("compress %3d%% average rate %3u%%\n"
				, block / percent_period
				, block == 0 ? 0 : (100 * write_pos)/(block * 0x800));
		}

		if (deflateInit2(&z, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		{
			printf("deflateInit : %s\n", (z.msg) ? z.msg : "???");
			return 1;
		}

		/* write align */
		uint32_t align = write_pos & align_m;
		if (align)
		{
			align = align_b - align;
			if (fwrite(buf4, 1, align, fout) != align)
			{
				printf("block %u : Write error\n", block);
				return 1;
			}
			write_pos += align;
		}

		/* mark offset index */
		index_buf[block] = write_pos >> (ciso.align);

		/* read buffer */
		z.next_out  = block_buf2;
		z.avail_out = ciso.block_size * 2;
		z.next_in   = block_buf1;
		z.avail_in  = fread(block_buf1, 1, ciso.block_size, fin);
		if (z.avail_in != ciso.block_size)
		{
			printf("block=%u : read error\n", block);
			return 1;
		}

		/* compress block */
		int32_t status = deflate(&z, Z_FINISH);
		if (status != Z_STREAM_END)
		{
			printf("block %u:deflate : %s[%d]\n", block, (z.msg) ? z.msg : "error", status);
			return 1;
		}

		cmp_size = ciso.block_size * 2 - z.avail_out;

		/* choise plain / compress */
		if (cmp_size >= ciso.block_size)
		{
			cmp_size = ciso.block_size;
			memcpy(block_buf2, block_buf1, cmp_size);
			/* plain block mark */
			index_buf[block] |= 0x80000000;
		}

		/* write compressed block */
		if (fwrite(block_buf2, 1, cmp_size, fout) != cmp_size)
		{
			printf("block %u : Write error\n", block);
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
	index_buf[block] = write_pos >> (ciso.align);

	/* write header & index block */
	fseek(fout, sizeof(ciso), SEEK_SET);
	fwrite(index_buf, 1, index_size, fout);

	printf("ciso compress completed , total size = %8u bytes , rate %u%%\n", write_pos, (write_pos * 100/ciso.total_bytes));
	return 0;
}

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
	if (level < 0 || level > 9)
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

	if (level == 0)
	{
		result = decomp_ciso();
	}
	else
	{
		result = comp_ciso(level);
	}

	/* free memory */
	if (index_buf) free(index_buf);
	if (crc_buf) free(crc_buf);
	if (block_buf1) free(block_buf1);
	if (block_buf2) free(block_buf2);

	/* close files */
	fclose(fin);
	fclose(fout);
	return result;
}
