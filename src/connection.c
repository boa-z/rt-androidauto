/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/connection.h"
#include <string.h>

struct rtaa_connection
{
    rtaa_connection_config c;
    rtaa_receiver *receiver;
    rtaa_connection_status status;
    uint64_t since;
    uint32_t delay, next_delay;
};
static uint64_t clock_ms(rtaa_connection *c)
{
    return c->c.connector.now_ms(c->c.connector.ctx);
}
static void release_transport(rtaa_transport *t)
{
    if (t->cancel)
        t->cancel(t->ctx);
    if (t->close)
        t->close(t->ctx);
}
static int failure(rtaa_connection *c, int error)
{
    if (c->receiver)
    {
        rtaa_receiver_destroy(c->receiver);
        c->receiver = NULL;
    }
    c->status.last_error = error;
    if ((error != RTAA_EIO && error != RTAA_ETIMEOUT) || c->status.attempts >= c->c.max_attempts)
    {
        c->status.state = RTAA_CONNECTION_EXHAUSTED;
        return error;
    }
    c->status.state = RTAA_CONNECTION_WAITING;
    c->since = clock_ms(c);
    c->delay = c->next_delay;
    /* Saturating doubling, with no overflow at UINT32_MAX. */
    if (c->next_delay > c->c.retry_delay_max_ms / 2)
        c->next_delay = c->c.retry_delay_max_ms;
    else
        c->next_delay *= 2;
    return RTAA_AGAIN;
}
int rtaa_connection_create(const rtaa_connection_config *config, rtaa_connection **out)
{
    if (!out)
        return RTAA_EINVAL;
    *out = NULL;
    if (!config || !config->receiver.memory.allocate || !config->receiver.memory.release ||
        !config->connector.open || !config->connector.abandon || !config->connector.now_ms ||
        !config->max_attempts || !config->open_timeout_ms || !config->retry_delay_ms ||
        config->retry_delay_ms > config->retry_delay_max_ms)
        return RTAA_EINVAL;
    if (config->receiver.memory_limit <= sizeof(rtaa_connection))
        return RTAA_EBOUNDS;
    rtaa_connection *c = config->receiver.memory.allocate(config->receiver.memory.ctx, sizeof(*c));
    if (!c)
        return RTAA_EBOUNDS;
    memset(c, 0, sizeof(*c));
    c->c = *config;
    c->c.receiver.memory_limit -= sizeof(*c);
    *out = c;
    return RTAA_OK;
}
void rtaa_connection_stop(rtaa_connection *c)
{
    if (!c)
        return;
    if (c->status.state == RTAA_CONNECTION_OPENING)
        c->c.connector.abandon(c->c.connector.ctx);
    if (c->receiver)
        rtaa_receiver_destroy(c->receiver);
    c->receiver = NULL;
    c->status.state = RTAA_CONNECTION_STOPPED;
}
void rtaa_connection_destroy(rtaa_connection *c)
{
    if (!c)
        return;
    rtaa_connection_stop(c);
    rtaa_allocator memory = c->c.receiver.memory;
    memset(c, 0, sizeof(*c));
    memory.release(memory.ctx, c);
}
int rtaa_connection_start(rtaa_connection *c)
{
    if (!c || (c->status.state != RTAA_CONNECTION_STOPPED &&
               c->status.state != RTAA_CONNECTION_EXHAUSTED))
        return RTAA_ESTATE;
    c->status = (rtaa_connection_status){RTAA_CONNECTION_OPENING, 1, 0};
    c->since = clock_ms(c);
    c->next_delay = c->c.retry_delay_ms;
    return RTAA_OK;
}
static int session_result(rtaa_connection *c, int rc)
{
    if (rtaa_receiver_state(c->receiver) == RTAA_CLOSED)
    {
        rtaa_receiver_destroy(c->receiver);
        c->receiver = NULL;
        c->status.state = RTAA_CONNECTION_STOPPED;
        return RTAA_OK;
    }
    if (rc < 0)
        return failure(c, rc);
    return rc;
}
int rtaa_connection_poll(rtaa_connection *c)
{
    if (!c)
        return RTAA_EINVAL;
    if (c->status.state == RTAA_CONNECTION_WAITING)
    {
        if (clock_ms(c) - c->since < c->delay)
            return RTAA_AGAIN;
        ++c->status.attempts;
        c->status.state = RTAA_CONNECTION_OPENING;
        c->since = clock_ms(c);
    }
    if (c->status.state == RTAA_CONNECTION_OPENING)
    {
        uint64_t elapsed = clock_ms(c) - c->since;
        if (elapsed >= c->c.open_timeout_ms)
        {
            c->c.connector.abandon(c->c.connector.ctx);
            return failure(c, RTAA_ETIMEOUT);
        }
        rtaa_receiver_config config = c->c.receiver;
        memset(&config.transport, 0, sizeof(config.transport));
        int rc = c->c.connector.open(c->c.connector.ctx, c->c.open_timeout_ms - (uint32_t)elapsed,
                                     &config.transport);
        if (rc == RTAA_AGAIN)
            return RTAA_AGAIN;
        if (rc != RTAA_OK)
        {
            c->c.connector.abandon(c->c.connector.ctx);
            return failure(c, rc < 0 ? rc : RTAA_EINVAL);
        }
        /* Ownership has transferred, including when open exceeded its deadline
         * or returned an invalid transport/configuration. Never leak that handle. */
        if (clock_ms(c) - c->since >= c->c.open_timeout_ms)
            rc = RTAA_ETIMEOUT;
        else if (!config.transport.cancel || !config.transport.close)
            rc = RTAA_EINVAL;
        else
            rc = rtaa_receiver_create(&config, &c->receiver);
        if (rc)
        {
            release_transport(&config.transport);
            return failure(c, rc);
        }
        c->status.state = RTAA_CONNECTION_SESSION;
        rc = rtaa_receiver_start(c->receiver);
        return session_result(c, rc);
    }
    if (c->status.state != RTAA_CONNECTION_SESSION)
        return RTAA_ESTATE;
    /* Receiver input APIs can fail between polls. Preserve that exact error,
     * rather than converting it to ESTATE and accidentally losing retry policy. */
    if (rtaa_receiver_state(c->receiver) == RTAA_FAILED)
        return failure(c, rtaa_receiver_error(c->receiver));
    if (rtaa_receiver_state(c->receiver) == RTAA_CLOSED)
        return session_result(c, RTAA_OK);
    int rc = rtaa_receiver_poll(c->receiver);
    return session_result(c, rc);
}
int rtaa_connection_feed(rtaa_connection *c, const uint8_t *data, size_t size)
{
    if (!c || c->status.state != RTAA_CONNECTION_SESSION)
        return RTAA_ESTATE;
    if (rtaa_receiver_state(c->receiver) == RTAA_FAILED)
        return failure(c, rtaa_receiver_error(c->receiver));
    int rc = rtaa_receiver_feed(c->receiver, data, size);
    return session_result(c, rc);
}
void rtaa_connection_get_status(const rtaa_connection *c, rtaa_connection_status *out)
{
    if (c && out)
        *out = c->status;
}
rtaa_receiver *rtaa_connection_receiver(rtaa_connection *c)
{
    return c ? c->receiver : NULL;
}
