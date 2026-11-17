/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/core.h"
#include <string.h>

bool rtaa_aoa_accessory(uint16_t vid, uint16_t pid)
{
    /* 2d02/03 are audio-only and have no accessory bulk interface. */
    return vid == 0x18d1 && (pid == 0x2d00 || pid == 0x2d01 || pid == 0x2d04 || pid == 0x2d05);
}
int rtaa_aoa_begin(rtaa_aoa *a, const rtaa_usb_control *usb, const char *const strings[6],
                   uint64_t now)
{
    if (!a || !usb || !usb->control || !strings)
        return RTAA_EINVAL;
    if (a->state != RTAA_AOA_IDLE && a->state != RTAA_AOA_DETACHED)
        return RTAA_ESTATE;
    size_t lengths[6];
    for (unsigned i = 0; i < 6; ++i)
    {
        if (!strings[i])
            return RTAA_EINVAL;
        for (lengths[i] = 0; lengths[i] < 256 && strings[i][lengths[i]]; ++lengths[i])
        {
        }
        if (!lengths[i] || lengths[i] == 256)
            return RTAA_EBOUNDS;
    }
    a->usb = *usb;
    a->state = RTAA_AOA_FAILED;
    uint8_t version[2] = {0};
    int rc = usb->control(usb->ctx, 0xc0, 51, 0, 0, version, 2, 1000);
    if (rc != 2)
        return rc < 0 ? rc : RTAA_EPROTO;
    a->protocol = (uint16_t)(version[0] | ((uint16_t)version[1] << 8));
    if (a->protocol < 1 || a->protocol > 2)
        return RTAA_ENOTSUP;
    for (unsigned i = 0; i < 6; ++i)
    {
        uint8_t buffer[256];
        memcpy(buffer, strings[i], lengths[i] + 1);
        rc = usb->control(usb->ctx, 0x40, 52, 0, (uint16_t)i, buffer, (uint16_t)(lengths[i] + 1),
                          1000);
        if (rc != (int)lengths[i] + 1)
            return rc < 0 ? rc : RTAA_EIO;
    }
    rc = usb->control(usb->ctx, 0x40, 53, 0, 0, NULL, 0, 1000);
    if (rc)
        return rc < 0 ? rc : RTAA_EIO;
    a->deadline = now + 10000;
    a->state = RTAA_AOA_REENUMERATING;
    return RTAA_OK;
}
int rtaa_aoa_bind(rtaa_aoa *a, uint16_t vid, uint16_t pid, const uint8_t *d, size_t size,
                  uint64_t now)
{
    if (!a || !d)
        return RTAA_EINVAL;
    if (a->state != RTAA_AOA_IDLE && a->state != RTAA_AOA_REENUMERATING &&
        a->state != RTAA_AOA_DETACHED)
        return RTAA_ESTATE;
    if (a->state == RTAA_AOA_REENUMERATING && now >= a->deadline)
    {
        a->state = RTAA_AOA_FAILED;
        return RTAA_ETIMEOUT;
    }
    if (!rtaa_aoa_accessory(vid, pid))
        return RTAA_ENOTSUP;
    if (size < 9 || d[0] < 9 || d[1] != 2)
        return RTAA_EPROTO;
    size_t total = d[2] | ((size_t)d[3] << 8);
    if (total < 9 || total > size)
        return RTAA_EPROTO;
    uint8_t in = 0, out = 0, number = 0;
    unsigned endpoints = 0, expected = 0;
    bool match = false, found = false;
    for (size_t pos = 0; pos < total;)
    {
        if (total - pos < 2 || d[pos] < 2 || d[pos] > total - pos)
            return RTAA_EPROTO;
        const uint8_t *p = d + pos;
        if (p[1] == 4)
        {
            if (p[0] < 9)
                return RTAA_EPROTO;
            if (match && (endpoints != expected || !in || !out))
                return RTAA_EPROTO;
            match = p[3] == 0 && p[5] == 0xff && p[6] == 0xff && p[7] == 0;
            if (match)
            {
                if (found)
                    return RTAA_EPROTO;
                number = p[2];
                expected = p[4];
                endpoints = 0;
                in = out = 0;
                found = true;
            }
        }
        else if (p[1] == 5 && match)
        {
            if (p[0] < 7 || (p[3] & 3) != 2 || !(p[2] & 15) || (p[2] & 0x70) ||
                !(p[4] | ((unsigned)p[5] << 8)))
                return RTAA_EPROTO;
            ++endpoints;
            if (p[2] & 0x80)
            {
                if (in)
                    return RTAA_EPROTO;
                in = p[2];
            }
            else
            {
                if (out)
                    return RTAA_EPROTO;
                out = p[2];
            }
            if (in && out)
            {
                a->interface_number = number;
                a->endpoint_in = in;
                a->endpoint_out = out;
            }
        }
        pos += p[0];
    }
    if (!found || !a->endpoint_in || !a->endpoint_out || (match && endpoints != expected))
        return RTAA_EPROTO;
    a->state = RTAA_AOA_READY;
    return RTAA_OK;
}
int rtaa_aoa_poll(rtaa_aoa *a, uint64_t now)
{
    if (!a)
        return RTAA_EINVAL;
    if (a->state == RTAA_AOA_REENUMERATING && now >= a->deadline)
    {
        a->state = RTAA_AOA_FAILED;
        return RTAA_ETIMEOUT;
    }
    return a->state == RTAA_AOA_REENUMERATING ? RTAA_AGAIN : RTAA_OK;
}
void rtaa_aoa_detach(rtaa_aoa *a)
{
    if (!a)
        return;
    if (a->usb.cancel)
        a->usb.cancel(a->usb.ctx);
    a->endpoint_in = a->endpoint_out = 0;
    a->state = RTAA_AOA_DETACHED;
}
