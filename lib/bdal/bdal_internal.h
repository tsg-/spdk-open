/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef SPDK_BDAL_INTERNAL_H
#define SPDK_BDAL_INTERNAL_H

#include "spdk/bdal.h"

struct spdk_bdev_fn_table {
	/** Destroy the block device */
	int (*destruct)(struct spdk_bdev *bdev);

	struct spdk_bdev_io_ctx *(*alloc_ctx)(struct spdk_bdev *bdev);
	int (*free_ctx)(struct spdk_bdev_io_ctx *ctx);

	int (*write)(struct spdk_bdev_io_ctx *ctx, void *payload,
		     uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		     void *cb_arg);
	int (*read)(struct spdk_bdev_io_ctx *ctx, void *payload,
		    uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		    void *cb_arg);
	int (*deallocate)(struct spdk_bdev_io_ctx *ctx, void *payload,
			  uint32_t num_ranges, spdk_bdev_cmd_cb cb_fn,
			  void *cb_arg);
	int (*flush)(struct spdk_bdev_io_ctx *ctx, spdk_bdev_cmd_cb cb_fn, void *cb_arg);
	int (*process_completions)(struct spdk_bdev_io_ctx *ctx, uint32_t max_completions);
};

struct spdk_bdev {
	struct spdk_bdev_fn_table fn_table;

	uint32_t ctx_count;
	uint32_t block_size;
	uint64_t block_count;
	uint32_t queue_depth;
};

struct spdk_bdev_io_ctx {
	struct spdk_bdev *bdev;
};

#endif
