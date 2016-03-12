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

#include "bdal_internal.h"

#include "spdk/queue.h"

int
spdk_bdev_destruct(struct spdk_bdev *bdev)
{
	return bdev->fn_table.destruct(bdev);
}


uint32_t
spdk_bdev_get_block_size(struct spdk_bdev *bdev)
{
	return bdev->block_size;
}

uint64_t
spdk_bdev_get_block_count(struct spdk_bdev *bdev)
{
	return bdev->block_count;
}

int
spdk_bdev_max_io_ctx(struct spdk_bdev *bdev)
{
	return bdev->ctx_count;
}

struct spdk_bdev_io_ctx *
spdk_bdev_create_io_ctx(struct spdk_bdev *bdev)
{
	return bdev->fn_table.alloc_ctx(bdev);
}

int
spdk_bdev_destroy_io_ctx(struct spdk_bdev_io_ctx *ctx)
{
	return ctx->bdev->fn_table.free_ctx(ctx);
}

int
spdk_bdev_process_completions(struct spdk_bdev_io_ctx *ctx, uint32_t max_completions)
{
	return ctx->bdev->fn_table.process_completions(ctx, max_completions);
}

int
spdk_bdev_write(struct spdk_bdev_io_ctx *ctx, void *payload,
		uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		void *cb_arg)
{
	return ctx->bdev->fn_table.write(ctx, payload, lba, lba_count,
					 cb_fn, cb_arg);
}

int
spdk_bdev_read(struct spdk_bdev_io_ctx *ctx, void *payload,
	       uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
	       void *cb_arg)
{
	return ctx->bdev->fn_table.read(ctx, payload, lba, lba_count,
					cb_fn, cb_arg);
}

int
spdk_bdev_deallocate(struct spdk_bdev_io_ctx *ctx, void *payload,
		     uint32_t num_ranges, spdk_bdev_cmd_cb cb_fn,
		     void *cb_arg)
{
	return ctx->bdev->fn_table.deallocate(ctx, payload, num_ranges,
					      cb_fn, cb_arg);
}


int
spdk_bdev_flush(struct spdk_bdev_io_ctx *ctx, spdk_bdev_cmd_cb cb_fn, void *cb_arg)
{
	return ctx->bdev->fn_table.flush(ctx, cb_fn, cb_arg);
}
