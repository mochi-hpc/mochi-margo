/*
 * (C) 2018 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_STREAM_H
#define __MARGP_STREAM_H

#include <margo.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct margo_stream* margo_stream_t;
#define MARGO_STREAM_NULL ((margo_stream_t)0)

#define MARGO_STREAM_IN  1
#define MARGO_STREAM_OUT 2

/**
 * @brief Creates a new margo_stream_t object. MARGO_STREAM_IN indicates
 * that the stream can be read from. The corresponding stream on the remote
 * party will be open in write mode. MARGO_STREAM_OUT indicates that the
 * stream can be written to. The corresponding stream on the remote party
 * will be open in read mode.
 *
 * @param[in] mid Margo instance id to use for communications.
 * @param[in] remote_addr Address of the remote party to communicate with.
 * @param[in] mode MARGO_STREAM_IN or MARGO_STREAM_OUT.
 * @param[out] stream Resulting stream.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_create(
        margo_instance_id mid,
        hg_addr_t remote_addr,
        int mode,
        margo_stream_t* stream);

/**
 * @brief Increments the reference count on a stream handle.
 *
 * @param[in] stream Stream in which to increment the reference count.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_ref_incr(margo_stream_t stream);

/**
 * @brief Notifies the remote party that the stream is closed
 * and cannot be used anymore. This method may block until all
 * the data located in the caller's output buffer has been sent
 * to the remote party.
 *
 * @param[in]  stream Stream to close.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_close(margo_stream_t stream);

/**
 * @brief Checks if the stream has been closed (either by this
 * process or by the remote party).
 *
 * @param[in] stream Stream.
 * @param[out] flag 0 if not closed, 1 if closed.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_is_closed(margo_stream_t stream, int* flag);

/**
 * @brief Decrements the stream's internal reference counter.
 * Destroys the stream and frees its resources when the reference
 * counter reaches zero.
 *
 * @param[in] stream Stream to destroy.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_destroy(margo_stream_t stream);

/**
 * @brief Sets the Argobots pool in which I/O and RPC operations
 * should be posted. By default, a stream uses its margo instance
 * id's handler pool.
 *
 * @param[in] stream Stream for which to set the pool.
 * @param[in] pool Argobots pool to use for this stream.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_set_pool(
        margo_stream_t stream,
        ABT_pool pool);

/**
 * @brief Sets the stream's input buffer size (the buffer used
 * to receive data from the remote party). This size cannot be 0.
 * If data is already placed in the buffer, this function will create
 * a new buffer of a different size and the old buffer will be
 * deleted when its data has been read, which may lead to a
 * temporary increase in memory usage.
 *
 * @param[in] stream Stream in which to set the input buffer size.
 * @param[in] size New size of the input buffer.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_set_input_buffer_size(
        margo_stream_t stream,
        size_t size);

/**
 * @brief Sets the stream's output buffer size (the buffer used
 * to send data to the remote party). This size may be 0 (in which
 * case write calls will not return until the data has been sent
 * to the remote party and placed on the remote party's input buffer).
 *
 * @param[in] stream Stream in which to set the output buffer size.
 * @param[in] size New size of the output buffer.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_set_output_buffer_size(
        margo_stream_t stream,
        size_t size);

/**
 * @brief Sets the amount of data that need to be in the output buffer
 * to trigger an RPC to the remote party. To force sending a smaller
 * amount, use margo_stream_flush.
 *
 * @param[in] stream Stream in which to set the transfer size.
 * @param[in] size New transfer size.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_set_xfer_size(
        margo_stream_t stream,
        size_t size);

/**
 * @brief Sets the amount of data that is transfered per bulk operation.
 * One transfer RPC may lead to multiple bulk operations performed in parallel
 * if the bulk size is smaller than the xfer size.
 *
 * @param[in] stream Stream in which to set the bulk size.
 * @param[in] size New bulk size.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_set_bulk_size(
        margo_stream_t stream,
        size_t size);

/**
 * @brief Flushes any data from the output buffer by issuing an RPC to
 * send it to the remote party's input buffer.
 *
 * @param[in] stream Stream to flush.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_flush(margo_stream stream);

/**
 * @brief Write data to the stream. This function will copy the data
 * into the output buffer (if its size is not 0). If the buffer is full,
 * this function will block until enough space is available in the buffer.
 *
 * @param[in] stream Stream in which to write.
 * @param[in] data Pointer to the data to send.
 * @param[in] size Size of the data to send.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_write(
        margo_stream_t stream,
        const void* data,
        size_t size);

/**
 * @brief Reads data from the stream. This function will attempt to
 * read the data from the stream's input buffer, blocking until
 * the requested number of bytes has been read.
 *
 * @param[in] stream Sream from which to read.
 * @param[out] data Pointer to where the data read should be placed.
 * @param[in] size Number of bytes to read.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_read(
        margo_stream_t stream,
        void* data,
        size_t size);

/**
 * @brief Gets the number of bytes available to read in the input
 * buffer. The returned size will necessarily be smaller or equal
 * to the buffer size, and will not reflect potentially pending write
 * operations that are waiting for the buffer to be available.
 *
 * @param[in] stream Stream to check for available input.
 * @param[out] size Available number of bytes.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_available_input(
        margo_stream_t stream,
        size_t* size);

/**
 * @brief Non-blocking version of margo_stream_write.
 *
 * @param[in] stream Sream from which to read.
 * @param[in] data Pointer to where the data read should be placed.
 * @param[in] size Number of bytes to read.
 * @param[out] request Request object to check for termination of the operation.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_iwrite(
        margo_stream_t stream,
        const void* data,
        size_t size,
        margo_request_t* request);

/**
 * @brief Non-blocking version of margo_stream_read.
 *
 * @param[in] stream Sream from which to read.
 * @param[out] data Pointer to where the data read should be placed.
 * @param[in] size Number of bytes to read.
 * @param[out] request Request object to check for termination of the operation.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_iread(
        margo_stream_t stream,
        void* data,
        size_t size,
        margo_request_t* request);

/**
 * @brief Write the stream's output directly to the provided
 * file descriptor.
 *
 * @param[in] stream Stream from which to take the data.
 * @param[in] fd File descriptor in which to write.
 * @param[in] size Size of data to transfer.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_direct_to_fd(
        margo_stream_t stream,
        int fd,
        size_t size);

/**
 * @brief Read the stream's input directly from the provided
 * file descriptor.
 *
 * @param[in] stream Stream to which to direct the data.
 * @param[in] fd File descriptor to read.
 * @param[in] size Size of data to transfer.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_direct_from_fd(
        margo_stream_t stream,
        int fd,
        size_t size);

/**
 * @brief Non-blocking version of margo_stream_direct_to_fd.
 *
 * @param[in] stream Stream from which to take the data.
 * @param[in] fd File descriptor in which to write.
 * @param[in] size Size of data to transfer.
 * @param[out] request Resulting request object.
 *
 * @return 0 on success, -1 on error. 
 */
int margo_stream_idirect_to_fd(
        margo_stream_t stream,
        int fd,
        size_t size,
        margo_request_t* request);

/**
 * @brief Non-blocking version of margo_stream_direct_from_fd.
 *
 * @param[in] stream Stream in which to write the data.
 * @param[in] fd File descriptor from which to read the data.
 * @param[in] size Size of data to transfer.
 * @param[out] request Resulting request object.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_idirect_from_fd(
        margo_stream_t stream,
        int fd,
        size_t size,
        margo_request_t* request);

/**
 * @brief Binds the stream to a file descriptor. If the stream was
 * open with MARGO_STREAM_IN, the stream will continuously read from the
 * remote end and write the data to the provided file descriptor.
 * If the stream was open with MARGO_STREAM_OUT, the stream will continuously
 * read from the file descriptor and send data to the remote end.
 * This call is blocking and will return only when either the file descriptor
 * is closed or has been read entirely (for streams open with MARGO_STREAM_OUT),
 * or when the stream is closed by the remote party (for streams open with
 * MARGO_STREAM_IN).
 *
 * @param[in] stream Stream to bind to the file descriptor.
 * @param[in] fd File descriptor.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_bind_fd(
        margo_stream_t stream,
        int fd);

/**
 * @brief Non-blocking version of margo_stream_bind_fd. The margo_request_t
 * object built will complete when margo_stream_bind_fd would complete.
 *
 * @param stream Stream to bind to the file descriptor.
 * @param fd File descriptor.
 * @param request Margo request object.
 *
 * @return 0 on success, -1 on error.
 */
int margo_stream_ibind_fd(
        margo_stream_t stream,
        int fd,
        margo_request_t* request);

#ifdef __cplusplus
}
#endif

#endif
