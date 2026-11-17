/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_STATUS_INTERNAL_H
#define RTAA_STATUS_INTERNAL_H
#include "rtaa/receiver.h"
int rtaa_status_message(const rtaa_sink *, rtaa_role, uint16_t, const uint8_t *, size_t);
#endif
