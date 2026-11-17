/* SPDX-License-Identifier: GPL-3.0-only */
#include "aap.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    aap_ChannelOpen open = {-1, 197};
    uint8_t bytes[128];
    pb_ostream_t out = pb_ostream_from_buffer(bytes, sizeof(bytes));
    assert(pb_encode(&out, aap_ChannelOpen_fields, &open));
    const uint8_t golden[] = {8, 1, 16, 0xc5, 1};
    assert(out.bytes_written == sizeof(golden) && !memcmp(bytes, golden, sizeof(golden)));
    pb_istream_t in = pb_istream_from_buffer(bytes, out.bytes_written);
    aap_ChannelOpen value = aap_ChannelOpen_init_zero;
    assert(pb_decode(&in, aap_ChannelOpen_fields, &value) && value.priority == -1 &&
           value.channel_id == 197);
    in = pb_istream_from_buffer(bytes, 2);
    assert(!pb_decode(&in, aap_ChannelOpen_fields, &value)); /* missing required field */
    aap_VideoConfig video = aap_VideoConfig_init_zero;
    in = pb_istream_from_buffer(NULL, 0);
    assert(pb_decode(&in, aap_VideoConfig_fields, &video) && !video.has_margin_width);
    const uint8_t explicit_zero[] = {24, 0, 0xf8, 7, 1}; /* zero margin, unknown tag 127 */
    in = pb_istream_from_buffer(explicit_zero, sizeof(explicit_zero));
    assert(pb_decode(&in, aap_VideoConfig_fields, &video) && video.has_margin_width &&
           !video.margin_width);
    const uint8_t overflow[] = {8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1};
    in = pb_istream_from_buffer(overflow, sizeof(overflow));
    assert(!pb_decode(&in, aap_ChannelOpen_fields, &value));
    uint8_t malformed[] = {0x12, 0xff, 0xff, 0xff, 0xff, 0x7f};
    aap_Discovery d = aap_Discovery_init_zero;
    in = pb_istream_from_buffer(malformed, sizeof(malformed));
    assert(!pb_decode(&in, aap_Discovery_fields, &d));
    puts("protobuf: PASS");
    return 0;
}
