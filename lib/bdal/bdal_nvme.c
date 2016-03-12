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

#include "spdk/bdal_nvme.h"
#include "spdk/nvme.h"
#include "spdk/queue.h"

#include <stdlib.h>
#include <string.h>

struct spdk_bdev_nvme {
	/* Must be first member */
	struct spdk_bdev bdev;

	struct spdk_nvme_ctrlr *ctrlr;
	struct spdk_nvme_ns *ns;
};

struct spdk_bdev_nvme_req {
	struct spdk_bdev_nvme_io_ctx *ctx;

	spdk_bdev_cmd_cb cb_fn;
	void *cb_arg;

	TAILQ_ENTRY(spdk_bdev_nvme_req) link;
};

struct spdk_bdev_nvme_io_ctx {
	/* Must be first member */
	struct spdk_bdev_io_ctx ctx;

	/* Queue pair will go here */
	struct spdk_nvme_qpair *qpair;

	TAILQ_HEAD(, spdk_bdev_nvme_req) free_reqs;
	struct spdk_bdev_nvme_req *req_array;
};

static struct spdk_bdev_nvme_req *
_spdk_bdev_nvme_get_req(struct spdk_bdev_nvme_io_ctx *ctx, spdk_bdev_cmd_cb cb_fn, void *cb_arg)
{
	struct spdk_bdev_nvme_req *req = NULL;

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
_spdk_bdev_nvme_put_req(struct spdk_bdev_nvme_io_ctx *ctx, struct spdk_bdev_nvme_req *req)
{
	memset(req, 0, sizeof(*req));
	TAILQ_INSERT_HEAD(&ctx->free_reqs, req, link);
}

static void
_spdk_bdev_nvme_completion(void *cb_ctx, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_bdev_nvme_req *req = cb_ctx;
	void *cb_arg = req->cb_arg;
	spdk_bdev_cmd_cb cb_fn = req->cb_fn;
	int status = spdk_nvme_cpl_is_error(cpl);

	_spdk_bdev_nvme_put_req(req->ctx, req);

	cb_fn(cb_arg, status);
}

static int
spdk_bdev_nvme_destruct(struct spdk_bdev *bdev)
{
	free(bdev);

	return 0;
}

static struct spdk_bdev_io_ctx *
spdk_bdev_nvme_create_io_ctx(struct spdk_bdev *bdev)
{
	struct spdk_bdev_nvme *bdev_nvme = (struct spdk_bdev_nvme *)bdev;
	struct spdk_bdev_nvme_io_ctx *ctx_nvme = NULL;
	int i, queue_depth;

	ctx_nvme = calloc(1, sizeof(struct spdk_bdev_nvme_io_ctx));
	if (ctx_nvme == NULL) {
		return NULL;
	}

	ctx_nvme->qpair = spdk_nvme_ctrlr_alloc_io_qpair(bdev_nvme->ctrlr, 0);
	ctx_nvme->ctx.bdev = bdev;
	TAILQ_INIT(&ctx_nvme->free_reqs);

	queue_depth = ctx_nvme->ctx.bdev->queue_depth;

	ctx_nvme->req_array = calloc(queue_depth, sizeof(struct spdk_bdev_nvme_req));
	for (i = 0; i < queue_depth; i++) {
		ctx_nvme->req_array[i].ctx = ctx_nvme;
		TAILQ_INSERT_HEAD(&ctx_nvme->free_reqs, &ctx_nvme->req_array[i], link);
	}

	return (struct spdk_bdev_io_ctx *)ctx_nvme;
}

static int
spdk_bdev_nvme_destroy_io_ctx(struct spdk_bdev_io_ctx *ctx)
{
	struct spdk_bdev_nvme_io_ctx *ctx_nvme = (struct spdk_bdev_nvme_io_ctx *)ctx;

	spdk_nvme_ctrlr_free_io_qpair(ctx_nvme->qpair);
	free(ctx_nvme->req_array);
	free(ctx_nvme);

	return 0;
}

static int
spdk_bdev_nvme_process_completions(struct spdk_bdev_io_ctx *ctx, uint32_t max_completions)
{
	struct spdk_bdev_nvme_io_ctx *ctx_nvme = (struct spdk_bdev_nvme_io_ctx *)ctx;

	return spdk_nvme_qpair_process_completions(ctx_nvme->qpair, max_completions);
}

static int
spdk_bdev_nvme_write(struct spdk_bdev_io_ctx *ctx, void *payload,
		     uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		     void *cb_arg)
{
	struct spdk_bdev_nvme_io_ctx *ctx_nvme = (struct spdk_bdev_nvme_io_ctx *)ctx;
	struct spdk_bdev_nvme *bdev_nvme = (struct spdk_bdev_nvme *)ctx_nvme->ctx.bdev;
	struct spdk_bdev_nvme_req *req = _spdk_bdev_nvme_get_req(ctx_nvme, cb_fn, cb_arg);

	if (req == NULL) {
		return -1;
	}

	return spdk_nvme_ns_cmd_write(bdev_nvme->ns, ctx_nvme->qpair,
				      payload, lba, lba_count,
				      _spdk_bdev_nvme_completion, req,
				      0);
}

static int
spdk_bdev_nvme_read(struct spdk_bdev_io_ctx *ctx, void *payload,
		    uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		    void *cb_arg)
{
	struct spdk_bdev_nvme_io_ctx *ctx_nvme = (struct spdk_bdev_nvme_io_ctx *)ctx;
	struct spdk_bdev_nvme *bdev_nvme = (struct spdk_bdev_nvme *)ctx_nvme->ctx.bdev;
	struct spdk_bdev_nvme_req *req = _spdk_bdev_nvme_get_req(ctx_nvme, cb_fn, cb_arg);

	if (req == NULL) {
		return -1;
	}

	return spdk_nvme_ns_cmd_read(bdev_nvme->ns, ctx_nvme->qpair,
				     payload, lba, lba_count,
				     _spdk_bdev_nvme_completion, req,
				     0);
}

static int
spdk_bdev_nvme_deallocate(struct spdk_bdev_io_ctx *ctx, void *payload,
			  uint32_t num_ranges, spdk_bdev_cmd_cb cb_fn,
			  void *cb_arg)
{
	struct spdk_bdev_nvme_io_ctx *ctx_nvme = (struct spdk_bdev_nvme_io_ctx *)ctx;
	struct spdk_bdev_nvme *bdev_nvme = (struct spdk_bdev_nvme *)ctx_nvme->ctx.bdev;
	struct spdk_bdev_nvme_req *req = _spdk_bdev_nvme_get_req(ctx_nvme, cb_fn, cb_arg);

	if (req == NULL) {
		return -1;
	}


	return spdk_nvme_ns_cmd_deallocate(bdev_nvme->ns, ctx_nvme->qpair,
					   payload, num_ranges,
					   _spdk_bdev_nvme_completion, req);
}


static int
spdk_bdev_nvme_flush(struct spdk_bdev_io_ctx *ctx, spdk_bdev_cmd_cb cb_fn, void *cb_arg)
{
	struct spdk_bdev_nvme_io_ctx *ctx_nvme = (struct spdk_bdev_nvme_io_ctx *)ctx;
	struct spdk_bdev_nvme *bdev_nvme = (struct spdk_bdev_nvme *)ctx_nvme->ctx.bdev;
	struct spdk_bdev_nvme_req *req = _spdk_bdev_nvme_get_req(ctx_nvme, cb_fn, cb_arg);

	return spdk_nvme_ns_cmd_flush(bdev_nvme->ns, ctx_nvme->qpair,
				      _spdk_bdev_nvme_completion, req);
}

struct spdk_bdev *
spdk_bdev_create_from_nvme(struct spdk_nvme_ctrlr *ctrlr,
			   struct spdk_nvme_ns *ns)
{
	struct spdk_bdev_nvme *bdev_nvme = NULL;

	bdev_nvme = calloc(1, sizeof(struct spdk_bdev_nvme));
	if (bdev_nvme == NULL) {
		return NULL;
	}

	bdev_nvme->ctrlr = ctrlr;
	bdev_nvme->ns = ns;

	bdev_nvme->bdev.ctx_count = 128;
	bdev_nvme->bdev.block_size = 512;
	bdev_nvme->bdev.block_count = 1024 * 1024;
	bdev_nvme->bdev.queue_depth = 128;

	bdev_nvme->bdev.fn_table.destruct = spdk_bdev_nvme_destruct;
	bdev_nvme->bdev.fn_table.alloc_ctx = spdk_bdev_nvme_create_io_ctx;
	bdev_nvme->bdev.fn_table.free_ctx = spdk_bdev_nvme_destroy_io_ctx;
	bdev_nvme->bdev.fn_table.write = spdk_bdev_nvme_write;
	bdev_nvme->bdev.fn_table.read = spdk_bdev_nvme_read;
	bdev_nvme->bdev.fn_table.deallocate = spdk_bdev_nvme_deallocate;
	bdev_nvme->bdev.fn_table.flush = spdk_bdev_nvme_flush;
	bdev_nvme->bdev.fn_table.process_completions = spdk_bdev_nvme_process_completions;

	return &bdev_nvme->bdev;
}
