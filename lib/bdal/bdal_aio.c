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
#include "spdk/bdal_aio.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <libaio.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <fcntl.h>

struct spdk_bdev_aio {
	/* Must be first member */
	struct spdk_bdev bdev;

	int fd;
};

struct spdk_bdev_aio_req {
	struct spdk_bdev_aio_io_ctx *ctx;

	spdk_bdev_cmd_cb cb_fn;
	void *cb_arg;

	struct iocb iocb;
	struct iovec iov;

	TAILQ_ENTRY(spdk_bdev_aio_req) link;
};

struct spdk_bdev_aio_io_ctx {
	/* Must be first member */
	struct spdk_bdev_io_ctx ctx;

	io_context_t io_ctx;
	struct io_event *events;

	TAILQ_HEAD(, spdk_bdev_aio_req) free_reqs;
	struct spdk_bdev_aio_req *req_array;
};

static struct spdk_bdev_aio_req *
_spdk_bdev_aio_get_req(struct spdk_bdev_aio_io_ctx *ctx, spdk_bdev_cmd_cb cb_fn, void *cb_arg)
{
	struct spdk_bdev_aio_req *req = NULL;

	if (TAILQ_EMPTY(&ctx->free_reqs)) {
		return NULL;
	}

	req = TAILQ_FIRST(&ctx->free_reqs);
	TAILQ_REMOVE(&ctx->free_reqs, req, link);

	req->ctx = ctx;
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;

	return req;
}

static void
_spdk_bdev_aio_put_req(struct spdk_bdev_aio_io_ctx *ctx, struct spdk_bdev_aio_req *req)
{
	memset(req, 0, sizeof(*req));
	TAILQ_INSERT_HEAD(&ctx->free_reqs, req, link);
}

static int
spdk_bdev_aio_destruct(struct spdk_bdev *bdev)
{
	struct spdk_bdev_aio *bdev_aio = (struct spdk_bdev_aio *)bdev;

	close(bdev_aio->fd);
	free(bdev);

	return 0;
}

static struct spdk_bdev_io_ctx *
spdk_bdev_aio_create_io_ctx(struct spdk_bdev *bdev)
{
	struct spdk_bdev_aio *bdev_aio = (struct spdk_bdev_aio *)bdev;
	struct spdk_bdev_aio_io_ctx *ctx_aio = NULL;
	int i, queue_depth, rc;

	ctx_aio = calloc(1, sizeof(struct spdk_bdev_aio_io_ctx));
	if (ctx_aio == NULL) {
		return NULL;
	}

	ctx_aio->ctx.bdev = bdev;
	TAILQ_INIT(&ctx_aio->free_reqs);

	queue_depth = bdev_aio->bdev.queue_depth;

	rc = io_setup(queue_depth, &ctx_aio->io_ctx);
	if (rc != 0) {
		free(ctx_aio);
		return NULL;
	}

	ctx_aio->events = calloc(queue_depth, sizeof(struct io_event));
	if (ctx_aio->events == NULL) {
		io_destroy(ctx_aio->io_ctx);
		free(ctx_aio);
		return NULL;
	}

	ctx_aio->req_array = calloc(queue_depth, sizeof(struct spdk_bdev_aio_req));
	for (i = 0; i < queue_depth; i++) {
		ctx_aio->req_array[i].ctx = ctx_aio;
		TAILQ_INSERT_HEAD(&ctx_aio->free_reqs, &ctx_aio->req_array[i], link);
	}

	return (struct spdk_bdev_io_ctx *)ctx_aio;
}

static int
spdk_bdev_aio_destroy_io_ctx(struct spdk_bdev_io_ctx *ctx)
{
	struct spdk_bdev_aio_io_ctx *ctx_aio = (struct spdk_bdev_aio_io_ctx *)ctx;

	io_destroy(ctx_aio->io_ctx);

	free(ctx_aio->events);
	free(ctx_aio->req_array);
	free(ctx_aio);

	return 0;
}

static int
spdk_bdev_aio_process_completions(struct spdk_bdev_io_ctx *ctx, uint32_t max_completions)
{
	struct spdk_bdev_aio_io_ctx *ctx_aio = (struct spdk_bdev_aio_io_ctx *)ctx;
	struct timespec timeout;
	struct spdk_bdev_aio_req *req;
	int i, count, limit;

	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;

	if (max_completions > 0 && max_completions < 128) {
		limit = max_completions;
	} else {
		limit = 128;
	}

	count = io_getevents(ctx_aio->io_ctx, 0, limit,
			     ctx_aio->events, &timeout);

	for (i = 0; i < count; i++) {
		req = ctx_aio->events[i].data;
		req->cb_fn(req->cb_arg, ctx_aio->events[i].res > 0 ? 0 : -1);

		_spdk_bdev_aio_put_req(req->ctx, req);
	}

	return count;
}

static int
spdk_bdev_aio_write(struct spdk_bdev_io_ctx *ctx, void *payload,
		    uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		    void *cb_arg)
{
	struct iocb *iocb;
	struct iovec *iov;
	struct spdk_bdev_aio_io_ctx *ctx_aio = (struct spdk_bdev_aio_io_ctx *)ctx;
	struct spdk_bdev_aio *bdev_aio = (struct spdk_bdev_aio *)ctx_aio->ctx.bdev;
	struct spdk_bdev_aio_req *req = _spdk_bdev_aio_get_req(ctx_aio, cb_fn, cb_arg);

	if (req == NULL) {
		return -1;
	}

	iocb = &req->iocb;
	iov = &req->iov;

	iov->iov_base = payload;
	iov->iov_len = lba_count * bdev_aio->bdev.block_size;

	iocb->aio_fildes = bdev_aio->fd;
	iocb->aio_lio_opcode = IO_CMD_PWRITEV;
	iocb->aio_reqprio = 0;
	iocb->u.v.vec = iov;
	iocb->u.v.nr = 1;
	iocb->u.v.offset = lba * bdev_aio->bdev.block_size;
	iocb->data = req;

	io_submit(ctx_aio->io_ctx, 1, &iocb);

	return 0;
}

static int
spdk_bdev_aio_read(struct spdk_bdev_io_ctx *ctx, void *payload,
		   uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		   void *cb_arg)
{
	struct iocb *iocb;
	struct spdk_bdev_aio_io_ctx *ctx_aio = (struct spdk_bdev_aio_io_ctx *)ctx;
	struct spdk_bdev_aio *bdev_aio = (struct spdk_bdev_aio *)ctx_aio->ctx.bdev;
	struct spdk_bdev_aio_req *req = _spdk_bdev_aio_get_req(ctx_aio, cb_fn, cb_arg);

	if (req == NULL) {
		return -1;
	}

	iocb = &req->iocb;

	iocb->aio_fildes = bdev_aio->fd;
	iocb->aio_reqprio = 0;
	iocb->aio_lio_opcode = IO_CMD_PREAD;
	iocb->u.c.buf = payload;
	/* TODO: Need the real block size */
	iocb->u.c.nbytes = lba_count * bdev_aio->bdev.block_size;
	iocb->u.c.offset = lba * bdev_aio->bdev.block_size;
	iocb->data = req;

	io_submit(ctx_aio->io_ctx, 1, &iocb);

	return 0;
}

static int
spdk_bdev_aio_deallocate(struct spdk_bdev_io_ctx *ctx, void *payload,
			 uint32_t num_ranges, spdk_bdev_cmd_cb cb_fn,
			 void *cb_arg)
{
	/* Not supported */
	return -1;
}


static int
spdk_bdev_aio_flush(struct spdk_bdev_io_ctx *ctx, spdk_bdev_cmd_cb cb_fn, void *cb_arg)
{
	struct spdk_bdev_aio_io_ctx *ctx_aio = (struct spdk_bdev_aio_io_ctx *)ctx;
	struct spdk_bdev_aio *bdev_aio = (struct spdk_bdev_aio *)ctx_aio->ctx.bdev;

	/* The kernel doesn't support asynchronous flushes, so do it synchronously. */
	fsync(bdev_aio->fd);
	cb_fn(cb_arg, 0);

	return 0;
}

struct spdk_bdev *
spdk_bdev_create_from_aio(const char *dev)
{
	struct spdk_bdev_aio *bdev_aio = NULL;

	bdev_aio = calloc(1, sizeof(struct spdk_bdev_aio));
	if (bdev_aio == NULL) {
		return NULL;
	}

	bdev_aio->fd = open(dev, O_RDWR | O_DIRECT);
	if (bdev_aio->fd < 0) {
		free(bdev_aio);
		return NULL;
	}

	bdev_aio->bdev.ctx_count = 128;
	bdev_aio->bdev.block_size = 512;
	bdev_aio->bdev.block_count = 1024 * 1024;
	bdev_aio->bdev.queue_depth = 128;

	bdev_aio->bdev.fn_table.destruct = spdk_bdev_aio_destruct;
	bdev_aio->bdev.fn_table.alloc_ctx = spdk_bdev_aio_create_io_ctx;
	bdev_aio->bdev.fn_table.free_ctx = spdk_bdev_aio_destroy_io_ctx;
	bdev_aio->bdev.fn_table.write = spdk_bdev_aio_write;
	bdev_aio->bdev.fn_table.read = spdk_bdev_aio_read;
	bdev_aio->bdev.fn_table.deallocate = spdk_bdev_aio_deallocate;
	bdev_aio->bdev.fn_table.flush = spdk_bdev_aio_flush;
	bdev_aio->bdev.fn_table.process_completions = spdk_bdev_aio_process_completions;

	return (struct spdk_bdev *)bdev_aio;
}
