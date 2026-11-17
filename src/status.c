/* SPDX-License-Identifier: GPL-3.0-only */
#include "status.h"
#include "aap.pb.h"
#include <pb_decode.h>
#define ARRAY_SIZE(v) (sizeof(v) / sizeof((v)[0]))
_Static_assert(ARRAY_SIZE(((aap_PhoneStatus *)0)->calls) == RTAA_PHONE_CALLS, "phone bound");
_Static_assert(ARRAY_SIZE(((aap_NavigationNotification *)0)->steps) == RTAA_NAV_STEPS,
               "step bound");
_Static_assert(ARRAY_SIZE(((aap_NavStep *)0)->lanes) == RTAA_NAV_LANES, "lane bound");
_Static_assert(ARRAY_SIZE(((aap_NavLane *)0)->directions) == RTAA_NAV_DIRECTIONS,
               "direction bound");
_Static_assert(ARRAY_SIZE(((aap_NavRoadInfo *)0)->road_names) == RTAA_NAV_ROADS, "road bound");
_Static_assert(ARRAY_SIZE(((aap_NavigationNotification *)0)->destinations) == RTAA_NAV_DESTINATIONS,
               "destination bound");
_Static_assert(ARRAY_SIZE(((aap_NavigationPosition *)0)->destinations) == RTAA_NAV_DESTINATIONS,
               "position bound");

static int decode(const pb_msgdesc_t *fields, void *out, const uint8_t *p, size_t n)
{
    pb_istream_t in = pb_istream_from_buffer(p, n);
    return pb_decode(&in, fields, out) ? RTAA_OK : RTAA_EPROTO;
}
static bool maneuver_valid(uint32_t value)
{
    return value <= 50 && value != 30 && value != 31;
}
static int phone_status(const rtaa_sink *sink, const uint8_t *p, size_t n)
{
    aap_PhoneStatus v = aap_PhoneStatus_init_zero;
    int rc = decode(aap_PhoneStatus_fields, &v, p, n);
    if (rc)
        return rc;
    rtaa_phone_status out = {0};
    out.calls_count = v.calls_count;
    out.has_signal_strength = v.has_signal_strength;
    out.signal_strength = v.signal_strength;
    for (size_t i = 0; i < v.calls_count; ++i)
    {
        const aap_PhoneCall *c = &v.calls[i];
        if (c->state > 6)
            return RTAA_EPROTO;
        out.calls[i] = (rtaa_phone_call){
            c->state, c->duration_seconds, c->has_number ? c->number : NULL,
            c->has_display_name ? c->display_name : NULL, c->has_contact_id ? c->contact_id : NULL};
    }
    return sink->phone_status(sink->ctx, &out);
}
static int notification(const rtaa_sink *sink, const uint8_t *p, size_t n)
{
    aap_NavigationNotification v = aap_NavigationNotification_init_zero;
    int rc = decode(aap_NavigationNotification_fields, &v, p, n);
    if (rc)
        return rc;
    rtaa_navigation_notification out = {0};
    out.steps_count = v.steps_count;
    out.destinations_count = v.destinations_count;
    for (size_t i = 0; i < v.steps_count; ++i)
    {
        const aap_NavStep *s = &v.steps[i];
        rtaa_nav_step *d = &out.steps[i];
        d->has_maneuver = s->has_maneuver;
        if (s->has_maneuver)
        {
            const aap_NavManeuver *m = &s->maneuver;
            if (m->has_type && !maneuver_valid(m->type))
                return RTAA_EPROTO;
            d->maneuver = (rtaa_nav_maneuver){m->has_type, m->has_exit_number, m->has_turn_angle,
                                              m->type,     m->exit_number,     m->turn_angle};
        }
        d->has_instruction = s->has_instruction;
        d->instruction = s->has_instruction && s->instruction.has_text ? s->instruction.text : NULL;
        d->lanes_count = s->lanes_count;
        for (size_t j = 0; j < s->lanes_count; ++j)
        {
            d->lanes[j].directions_count = s->lanes[j].directions_count;
            for (size_t k = 0; k < s->lanes[j].directions_count; ++k)
            {
                const aap_NavDirection *a = &s->lanes[j].directions[k];
                if (a->has_shape && a->shape > 9)
                    return RTAA_EPROTO;
                d->lanes[j].directions[k] = (rtaa_nav_direction){a->has_shape, a->has_recommended,
                                                                 a->shape, a->recommended};
            }
        }
        d->has_road_info = s->has_road_info;
        if (s->has_road_info)
        {
            d->road_names_count = s->road_info.road_names_count;
            for (size_t j = 0; j < d->road_names_count; ++j)
                d->road_names[j] = s->road_info.road_names[j];
        }
    }
    for (size_t i = 0; i < v.destinations_count; ++i)
    {
        const aap_NavDestination *s = &v.destinations[i];
        rtaa_nav_destination *d = &out.destinations[i];
        d->address = s->has_address ? s->address : NULL;
        d->has_charging_station = s->has_charging_station;
        if (s->has_charging_station)
        {
            const aap_NavChargingStation *c = &s->charging_station;
            d->has_connector_count = c->has_connector_count;
            d->has_available_count = c->has_available_count;
            d->has_max_power_kw = c->has_max_power_kw;
            d->connector_count = c->connector_count;
            d->available_count = c->available_count;
            d->max_power_kw = c->max_power_kw;
        }
    }
    return sink->navigation_notification(sink->ctx, &out);
}
static int distance(rtaa_nav_distance *out, const aap_NavDistance *v)
{
    if (v->has_unit && v->unit > 7)
        return RTAA_EPROTO;
    *out = (rtaa_nav_distance){v->has_value, v->has_unit, v->value, v->unit,
                               v->has_display_text ? v->display_text : NULL};
    return RTAA_OK;
}
static int position(const rtaa_sink *sink, const uint8_t *p, size_t n)
{
    aap_NavigationPosition v = aap_NavigationPosition_init_zero;
    int rc = decode(aap_NavigationPosition_fields, &v, p, n);
    if (rc)
        return rc;
    rtaa_navigation_position out = {0};
    out.has_step_distance = v.has_step_distance;
    if (v.has_step_distance)
    {
        out.has_distance = v.step_distance.has_distance;
        if (out.has_distance && distance(&out.distance, &v.step_distance.distance))
            return RTAA_EPROTO;
        out.has_time_to_step = v.step_distance.has_time_to_step_seconds;
        out.time_to_step_seconds = v.step_distance.time_to_step_seconds;
    }
    out.destinations_count = v.destinations_count;
    for (size_t i = 0; i < v.destinations_count; ++i)
    {
        const aap_NavDestinationDistance *s = &v.destinations[i];
        rtaa_nav_destination_distance *d = &out.destinations[i];
        d->has_distance = s->has_distance;
        if (s->has_distance && distance(&d->distance, &s->distance))
            return RTAA_EPROTO;
        d->estimated_time_of_arrival =
            s->has_estimated_time_of_arrival ? s->estimated_time_of_arrival : NULL;
        d->has_time_to_arrival = s->has_time_to_arrival_seconds;
        d->time_to_arrival_seconds = s->time_to_arrival_seconds;
    }
    out.has_current_road = v.has_current_road;
    out.current_road = v.has_current_road && v.current_road.has_text ? v.current_road.text : NULL;
    return sink->navigation_position(sink->ctx, &out);
}
int rtaa_status_message(const rtaa_sink *sink, rtaa_role role, uint16_t message, const uint8_t *p,
                        size_t n)
{
    if (role == RTAA_PHONE)
        return message == 0x8001 ? phone_status(sink, p, n) : RTAA_ENOTSUP;
    if (message == 0x8003)
    {
        aap_NavigationState v = aap_NavigationState_init_zero;
        int rc = decode(aap_NavigationState_fields, &v, p, n);
        if (rc || (v.has_state && v.state > 3))
            return RTAA_EPROTO;
        rtaa_navigation_state out = {v.has_state, v.state};
        return sink->navigation_state(sink->ctx, &out);
    }
    if (message == 0x8004)
    {
        aap_NavigationTurn v = aap_NavigationTurn_init_zero;
        int rc = decode(aap_NavigationTurn_fields, &v, p, n);
        if (rc || (v.has_maneuver_type && !maneuver_valid(v.maneuver_type)) ||
            (v.has_turn_direction && v.turn_direction > 2))
            return RTAA_EPROTO;
        rtaa_navigation_turn out = {v.has_road_name ? v.road_name : NULL,
                                    v.has_maneuver_type,
                                    v.has_turn_direction,
                                    v.has_distance_meters,
                                    v.has_distance_unit,
                                    v.maneuver_type,
                                    v.turn_direction,
                                    v.distance_meters,
                                    v.distance_unit};
        return sink->navigation_turn(sink->ctx, &out);
    }
    if (message == 0x8006)
        return notification(sink, p, n);
    if (message == 0x8007)
        return position(sink, p, n);
    return RTAA_ENOTSUP;
}
