/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_STATUS_H
#define RTAA_STATUS_H
#include "rtaa/core.h"
/* Borrowed views valid only during the callback. NULL string means absent;
 * non-NULL empty string means present and empty. Text is bounded to 127 bytes.
 * Events preserve optional fields and are not merged snapshots. */
#define RTAA_PHONE_CALLS 4
#define RTAA_NAV_STEPS 4
#define RTAA_NAV_LANES 6
#define RTAA_NAV_DIRECTIONS 3
#define RTAA_NAV_ROADS 3
#define RTAA_NAV_DESTINATIONS 2
typedef struct
{
    uint32_t state, duration_seconds; /* required; state 0..6, see profile */
    const char *number, *display_name, *contact_id;
} rtaa_phone_call;
typedef struct
{
    size_t calls_count;
    rtaa_phone_call calls[RTAA_PHONE_CALLS];
    bool has_signal_strength;
    uint32_t signal_strength; /* Raw upstream value; no invented level scale. */
} rtaa_phone_status;
typedef struct
{
    bool has_state;
    uint32_t state; /* 0 unavailable, 1 active, 2 inactive, 3 rerouting */
} rtaa_navigation_state;
typedef struct
{
    bool has_type, has_exit_number, has_turn_angle;
    uint32_t type;
    int32_t exit_number, turn_angle;
} rtaa_nav_maneuver;
typedef struct
{
    bool has_shape, has_recommended;
    uint32_t shape;
    bool recommended;
} rtaa_nav_direction;
typedef struct
{
    size_t directions_count;
    rtaa_nav_direction directions[RTAA_NAV_DIRECTIONS];
} rtaa_nav_lane;
typedef struct
{
    bool has_maneuver, has_instruction, has_road_info;
    rtaa_nav_maneuver maneuver;
    const char *instruction;
    size_t lanes_count, road_names_count;
    rtaa_nav_lane lanes[RTAA_NAV_LANES];
    const char *road_names[RTAA_NAV_ROADS];
} rtaa_nav_step;
typedef struct
{
    const char *address;
    bool has_charging_station, has_connector_count, has_available_count, has_max_power_kw;
    int32_t connector_count, available_count, max_power_kw;
} rtaa_nav_destination;
typedef struct
{
    size_t steps_count, destinations_count;
    rtaa_nav_step steps[RTAA_NAV_STEPS];
    rtaa_nav_destination destinations[RTAA_NAV_DESTINATIONS];
} rtaa_navigation_notification;
typedef struct
{
    bool has_value, has_unit;
    int32_t value;
    uint32_t unit; /* 0 unknown,1 m,2 km,3 km.1,4 mi,5 mi.1,6 ft,7 yd */
    const char *display_text;
} rtaa_nav_distance;
typedef struct
{
    bool has_distance, has_time_to_arrival;
    rtaa_nav_distance distance;
    const char *estimated_time_of_arrival;
    int64_t time_to_arrival_seconds;
} rtaa_nav_destination_distance;
typedef struct
{
    bool has_step_distance, has_distance, has_time_to_step, has_current_road;
    rtaa_nav_distance distance;
    int64_t time_to_step_seconds;
    size_t destinations_count;
    rtaa_nav_destination_distance destinations[RTAA_NAV_DESTINATIONS];
    const char *current_road;
} rtaa_navigation_position;
/* Legacy 0x8004 text/numeric subset. Icon bytes are skipped. */
typedef struct
{
    const char *road_name;
    bool has_maneuver_type, has_turn_direction, has_distance_meters, has_distance_unit;
    uint32_t maneuver_type, turn_direction;
    int32_t distance_meters, distance_unit;
} rtaa_navigation_turn;
#endif
