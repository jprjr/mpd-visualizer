/*
 * ringbuf.c - C ring buffer (FIFO) implementation.
 *
 * Written in 2011 by Drew Hess <dhess-src@bothan.net>.
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to
 * the public domain worldwide. This software is distributed without
 * any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication
 * along with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "ringbuf.h"

/*
 * The code is written for clarity, not cleverness or performance, and
 * contains many assert()s to enforce invariant assumptions and catch
 * bugs. Feel free to optimize the code and to remove asserts for use
 * in your own projects, once you're comfortable that it functions as
 * intended.
 */

ringbuf_t
ringbuf_new(size_t capacity, size_t (*read)(uint8_t *, size_t, void *), void *read_context, size_t (*write)(uint8_t *, size_t, void *), void *write_context)
{
    ringbuf_t rb = malloc(sizeof(struct ringbuf_t));
    if (rb) {

        /* One byte is used for detecting the full condition. */
        rb->size = capacity + 1;
        rb->buf = malloc(rb->size);
        if (rb->buf)
            ringbuf_reset(rb);
        else {
            free(rb);
            return 0;
        }
        rb->read = read;
        rb->write = write;
        rb->read_context = read_context;
        rb->write_context = write_context;
    }
    return rb;
}


void
ringbuf_free(ringbuf_t *rb)
{
    free((*rb)->buf);
    free(*rb);
    *rb = 0;
}



size_t
ringbuf_findchr(const struct ringbuf_t *rb, int c, size_t offset)
{
    const uint8_t *bufend = ringbuf_end(rb);
    size_t bytes_used = ringbuf_bytes_used(rb);
    if (offset >= bytes_used)
        return bytes_used;

    const uint8_t *start = rb->buf +
        (((rb->tail - rb->buf) + offset) % ringbuf_buffer_size(rb));
    size_t n = MIN((unsigned)(bufend - start), (unsigned)(bytes_used - offset));
    const uint8_t *found = memchr(start, c, n);
    if (found)
        return offset + (found - start);
    else
        return ringbuf_findchr(rb, c, offset + n);
}

size_t
ringbuf_memset(ringbuf_t dst, int c, size_t len)
{
    const uint8_t *bufend = ringbuf_end(dst);
    size_t nwritten = 0;
    size_t count = MIN(len, ringbuf_buffer_size(dst));
    int overflow = count > ringbuf_bytes_free(dst);

    while (nwritten != count) {

        /* don't copy beyond the end of the buffer */
        size_t n = MIN((unsigned)(bufend - dst->head), (unsigned)(count - nwritten));
        memset(dst->head, c, n);
        dst->head += n;
        nwritten += n;

        /* wrap? */
        if (dst->head == bufend)
            dst->head = dst->buf;
    }

    if (overflow) {
        dst->tail = ringbuf_nextp(dst, dst->head);
    }

    return nwritten;
}

char ringbuf_qget(ringbuf_t dst)
{
    char res[1] = { 0 };
    if (ringbuf_bytes_used(dst) > 0) {
        ringbuf_memcpy_from(&res,dst,1);
        return res[0];
    }
    return 0;
}

ssize_t
ringbuf_append(ringbuf_t dst, char byte)
{
    if(ringbuf_bytes_free(dst) > 0) {
        ringbuf_memcpy_into(dst,&byte,1);
        return 1;
    }
    return 0;
}

void *
ringbuf_memcpy_into(ringbuf_t dst, const void *src, size_t count)
{
    const uint8_t *u8src = src;
    const uint8_t *bufend = ringbuf_end(dst);
    int overflow = count > ringbuf_bytes_free(dst);
    size_t nread = 0;

    while (nread != count) {
        /* don't copy beyond the end of the buffer */
        size_t n = MIN((unsigned)(bufend - dst->head), (unsigned)(count - nread));
        memcpy(dst->head, u8src + nread, n);
        dst->head += n;
        nread += n;

        /* wrap? */
        if (dst->head == bufend)
            dst->head = dst->buf;
    }

    if (overflow) {
        dst->tail = ringbuf_nextp(dst, dst->head);
    }

    return dst->head;
}

ssize_t
ringbuf_read(ringbuf_t rb, size_t count)
{
    const uint8_t *bufend = ringbuf_end(rb);
    size_t nfree = ringbuf_bytes_free(rb);

    /* don't write beyond the end of the buffer */
    count = MIN((unsigned)(bufend - rb->head), count);
    ssize_t n = rb->read(rb->head, count, rb->read_context);

    if (n > 0) {
        rb->head += n;

        /* wrap? */
        if (rb->head == bufend)
            rb->head = rb->buf;

        /* fix up the tail pointer if an overflow occurred */
        if ((size_t)n > nfree) {
            rb->tail = ringbuf_nextp(rb, rb->head);
        }
    }

    return n;
}

void *
ringbuf_memcpy_from(void *dst, ringbuf_t src, size_t count)
{
    size_t bytes_used = ringbuf_bytes_used(src);
    if (count > bytes_used)
        return 0;

    uint8_t *u8dst = dst;
    const uint8_t *bufend = ringbuf_end(src);
    size_t nwritten = 0;
    while (nwritten != count) {
        size_t n = MIN((unsigned)(bufend - src->tail), (unsigned)(count - nwritten));
        memcpy(u8dst + nwritten, src->tail, n);
        src->tail += n;
        nwritten += n;

        /* wrap ? */
        if (src->tail == bufend)
            src->tail = src->buf;
    }

    return src->tail;
}

ssize_t
ringbuf_write(ringbuf_t rb, size_t count)
{
    size_t bytes_used = ringbuf_bytes_used(rb);
    if (count > bytes_used)
        return 0;

    const uint8_t *bufend = ringbuf_end(rb);
    count = MIN((unsigned)(bufend - rb->tail), count);
    ssize_t n = rb->write(rb->tail,count,rb->write_context);
    if (n > 0) {
        rb->tail += n;

        /* wrap? */
        if (rb->tail == bufend)
            rb->tail = rb->buf;

    }

    return n;
}

void *
ringbuf_copy(ringbuf_t dst, ringbuf_t src, size_t count)
{
    size_t src_bytes_used = ringbuf_bytes_used(src);
    if (count > src_bytes_used)
        return 0;
    int overflow = count > ringbuf_bytes_free(dst);

    const uint8_t *src_bufend = ringbuf_end(src);
    const uint8_t *dst_bufend = ringbuf_end(dst);
    size_t ncopied = 0;
    while (ncopied != count) {
        size_t nsrc = MIN((unsigned)(src_bufend - src->tail), (unsigned)(count - ncopied));
        size_t n = MIN((unsigned)(dst_bufend - dst->head), nsrc);
        memcpy(dst->head, src->tail, n);
        src->tail += n;
        dst->head += n;
        ncopied += n;

        /* wrap ? */
        if (src->tail == src_bufend)
            src->tail = src->buf;
        if (dst->head == dst_bufend)
            dst->head = dst->buf;
    }

    
    if (overflow) {
        dst->tail = ringbuf_nextp(dst, dst->head);
    }

    return dst->head;
}
