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

/** \file
 * This file defines the SPDK Block Device Abstraction Layer (BDAL).
 */

#ifndef SPDK_BDAL_H
#define SPDK_BDAL_H

#include <stdint.h>

/**
 * Opaque handle to a block device.
 */
struct spdk_bdev;

/**
 * \brief Destroy a bdev.
 */
int spdk_bdev_destruct(struct spdk_bdev *bdev);

/**
 * \brief Get the block size in bytes.
 */
uint32_t spdk_bdev_get_block_size(struct spdk_bdev *bdev);

/**
 * \brief Get the number of blocks.
 */
uint64_t spdk_bdev_get_block_count(struct spdk_bdev *bdev);

/**
 * \brief Opaque handle to a bdev I/O context.
 *
 */
struct spdk_bdev_io_ctx;

/**
 * \brief Return the maximum number of I/O contexts for a block device.
 */
int spdk_bdev_max_io_ctx(struct spdk_bdev *bdev);

/**
 * \brief Allocate a bdev context.
 *
 * To submit I/O to a bdev, you must provide an I/O context. A context may
 * only be used from a single thread at a time (mutual exclusion must be
 * enforced by the user). Multiple contexts may be created and the maximum
 * number of contexts can be obtained by calling \spdk_bdev_max_io_contexts()
 *
 * \param bdev Create a context for this block device
 */
struct spdk_bdev_io_ctx *spdk_bdev_create_io_ctx(struct spdk_bdev *bdev);

/**
 * \brief Free an I/O context
 */
int spdk_bdev_destroy_io_ctx(struct spdk_bdev_io_ctx *ctx);

/**
 * Signature for callback function invoked when a command is completed.
 */
typedef void (*spdk_bdev_cmd_cb)(void *, int status);

/**
 * \brief Process pending completions for this context.
 *
 * \param ctx The block device context to check for completions.
 * \param max_completions Limit the number of completions to be processed in one call, or 0
 * for unlimited.
 *
 * \return the number of completions processed.
 */
int spdk_bdev_process_completions(struct spdk_bdev_io_ctx *ctx, uint32_t max_completions);

/**
 * \brief Submits a write I/O to the specified context.
 *
 * \param ctx Block device context to use for I/O submission
 * \param payload virtual address pointer to the data payload
 * \param lba starting LBA to write the data
 * \param lba_count length (in sectors) for the write operation
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 *
 * \return 0 if successfully submitted, ENOMEM if a request
 *	     structure cannot be allocated for the I/O request
 */
int spdk_bdev_write(struct spdk_bdev_io_ctx *ctx, void *payload,
		    uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		    void *cb_arg);

/**
 * \brief Submits a read I/O to the specified context.
 *
 * \param ctx Block device context to use for I/O submission
 * \param payload virtual address pointer to the data payload
 * \param lba starting LBA to read the data
 * \param lba_count length (in sectors) for the read operation
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 *
 * \return 0 if successfully submitted, ENOMEM if a request
 *	     structure cannot be allocated for the I/O request
 */
int spdk_bdev_read(struct spdk_bdev_io_ctx *ctx, void *payload,
		   uint64_t lba, uint32_t lba_count, spdk_bdev_cmd_cb cb_fn,
		   void *cb_arg);

/**
 * \brief Submits a deallocation request to the specified block device. Deallocation
 *	  is sometimes called trim or unmap.
 *
 * \param ctx Block device context to use for I/O submission
 * \param payload virtual address pointer to the list of LBA ranges to
 *                deallocate
 * \param num_ranges number of ranges in the list pointed to by payload.
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 *
 * \return 0 if successfully submitted, ENOMEM if a request
 *	     structure cannot be allocated for the I/O request
 */
int spdk_bdev_deallocate(struct spdk_bdev_io_ctx *ctx, void *payload,
			 uint32_t num_ranges, spdk_bdev_cmd_cb cb_fn,
			 void *cb_arg);

/**
 * \brief Submits a flush request to the specified block device.
 *
 * \param ctx Block device context to use for I/O submission
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 *
 * \return 0 if successfully submitted, ENOMEM if a request
 *	     structure cannot be allocated for the I/O request
 */
int spdk_bdev_flush(struct spdk_bdev_io_ctx *ctx, spdk_bdev_cmd_cb cb_fn, void *cb_arg);


#endif
