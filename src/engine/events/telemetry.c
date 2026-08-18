/**
 * @file engine/events/telemetry.c
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief RTSyn Engine telemetry event helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#include <string.h>
#include <rtsyn/value.h>

#include "rtsyn/internal/engine/events.h"

static bool
rtsyn_engine_event_sample_value(const rtsyn_value_t *value, uint64_t timestamp_ns,
                                uint32_t node_id, uint32_t value_id, uint16_t sample_offset,
                                uint16_t value_kind, rtsyn_spsc_telemetry_source_t source,
                                rtsyn_spsc_telemetry_value_t *sample)
{
    if (!value || !sample)
    {
        return false;
    }

    memset(sample, 0, sizeof(*sample));
    sample->cycle_id = 0;
    sample->timestamp_ns = timestamp_ns;
    sample->node_id = node_id;
    sample->value_id = value_id;
    sample->sample_offset = sample_offset;
    sample->value_kind = value_kind;
    sample->source = source;
    sample->value_type = rtsyn_value_type_get(value);

    switch (sample->value_type)
    {
        case RTSYN_ABI_VALUE_F32:
            return rtsyn_value_get(value, &sample->data.f32);
        case RTSYN_ABI_VALUE_F64:
            return rtsyn_value_get(value, &sample->data.f64);
        case RTSYN_ABI_VALUE_I64:
        {
            int int_value = 0;
            if (!rtsyn_value_get(value, &int_value))
            {
                return false;
            }
            sample->data.i64 = int_value;
            return true;
        }
        case RTSYN_ABI_VALUE_U64:
            return rtsyn_value_get(value, &sample->data.u64);
        case RTSYN_ABI_VALUE_STRING:
        {
            char value_buffer[RTSYN_PORT_VALUE_STRING_MAX_SIZE] = {0};
            if (!rtsyn_value_get(value, value_buffer))
            {
                return false;
            }
            strncpy(sample->data.string, value_buffer,
                    RTSYN_SPSC_TELEMETRY_VALUE_STRING_MAX_SIZE - 1);
            sample->data.string[RTSYN_SPSC_TELEMETRY_VALUE_STRING_MAX_SIZE - 1] = '\0';
            return true;
        }
        default:
            return false;
    }
}

static rtsyn_spsc_telemetry_source_t
rtsyn_engine_get_node_source(rtsyn_engine_t *engine, uint32_t node_id)
{
    rtsyn_node_t *node = rtsyn_runtime_get_node(engine->config.runtime, node_id);
    if (!node)
    {
        return RTSYN_SPSC_TELEMETRY_SOURCE_NONE;
    }

    switch (rtsyn_node_get_type(node))
    {
        case RTSYN_ABI_NODE_DEVICE:
            return RTSYN_SPSC_TELEMETRY_SOURCE_DEVICE;
        case RTSYN_ABI_NODE_PLUGIN:
            return RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
        default:
            return RTSYN_SPSC_TELEMETRY_SOURCE_NONE;
    }
}

static const rtsyn_value_t *
rtsyn_engine_get_node_port_value(rtsyn_engine_t *engine, uint32_t node_id, uint32_t port_id)
{
    const rtsyn_value_t *value = rtsyn_runtime_get_node_port_value(
        engine->config.runtime, node_id, port_id, RTSYN_ABI_PORT_DIRECTION_OUT);
    if (value)
    {
        return value;
    }

    return rtsyn_runtime_get_node_port_value(engine->config.runtime, node_id, port_id,
                                            RTSYN_ABI_PORT_DIRECTION_IN);
}

bool
rtsyn_engine_event_push_telemetry(rtsyn_engine_t *engine,
                                  rtsyn_spsc_telemetry_message_t *message)
{
    if (!engine || !message || !engine->config.telemetry_queue)
    {
        return false;
    }

    message->seq = rtsyn_engine_next_telemetry_seq(engine);
    message->dropped_event_count = engine->dropped_telemetry_events;

    if (!rtsyn_spsc_telemetry_try_push(engine->config.telemetry_queue, message))
    {
        rtsyn_engine_account_dropped_event(engine);
        return false;
    }

    return true;
}

bool
rtsyn_engine_event_publish_cycle_begin(rtsyn_engine_t *engine, uint64_t timestamp_ns)
{
    if (!engine || !engine->config.telemetry_queue)
    {
        return false;
    }

    rtsyn_spsc_telemetry_message_t message = {0};
    message.timestamp_ns = timestamp_ns;
    message.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN;
    message.data.cycle_begin.cycle_id = engine->cycle_id;
    message.data.cycle_begin.scheduled_timestamp_ns = timestamp_ns;

    return rtsyn_engine_event_push_telemetry(engine, &message);
}

bool
rtsyn_engine_event_publish_cycle_end(rtsyn_engine_t *engine, uint64_t started_at_ns,
                                     uint64_t finished_at_ns)
{
    if (!engine || !engine->config.telemetry_queue)
    {
        return false;
    }

    rtsyn_spsc_telemetry_message_t message = {0};
    message.timestamp_ns = finished_at_ns;
    message.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_END;
    message.data.cycle_end.cycle_id = engine->cycle_id;
    message.data.cycle_end.started_at_ns = started_at_ns;
    message.data.cycle_end.finished_at_ns = finished_at_ns;

    return rtsyn_engine_event_push_telemetry(engine, &message);
}

bool
rtsyn_engine_event_publish_measurement(rtsyn_engine_t *engine, uint64_t timestamp_ns,
                                       const rtsyn_measurement_metrics_t *metrics)
{
    if (!engine || !engine->config.telemetry_queue || !metrics)
    {
        return false;
    }

    rtsyn_spsc_telemetry_message_t message = {0};
    message.timestamp_ns = timestamp_ns;
    message.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT;
    message.data.measurement.cycle_id = metrics->cycle_id;
    message.data.measurement.period_ns = metrics->period_ns;
    message.data.measurement.actual_period_ns = metrics->actual_period_ns;
    message.data.measurement.latency_ns = metrics->latency_ns;
    message.data.measurement.wake_lateness_ns = metrics->wake_lateness_ns;
    message.data.measurement.skipped_cycle_count = metrics->skipped_cycle_count;
    message.data.measurement.devices_read_ns = metrics->devices_read_ns;
    message.data.measurement.plugins_time_ns = metrics->plugins_time_ns;
    message.data.measurement.devices_write_ns = metrics->devices_write_ns;
    message.data.measurement.missed_cycle = metrics->missed_cycle ? 1U : 0U;
    message.data.measurement.deadline_missed = metrics->deadline_missed ? 1U : 0U;

    return rtsyn_engine_event_push_telemetry(engine, &message);
}

bool
rtsyn_engine_event_publish_dropped(rtsyn_engine_t *engine, uint64_t timestamp_ns)
{
    if (!engine || !engine->config.telemetry_queue
        || (engine->dropped_telemetry_events == 0 && engine->dropped_telemetry_values == 0))
    {
        return false;
    }

    rtsyn_spsc_telemetry_message_t message = {0};
    message.timestamp_ns = timestamp_ns;
    message.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_DROPPED;
    message.data.dropped.cycle_id = engine->cycle_id;
    message.data.dropped.dropped_event_count = engine->dropped_telemetry_events;
    message.data.dropped.dropped_value_count = engine->dropped_telemetry_values;

    return rtsyn_engine_event_push_telemetry(engine, &message);
}

bool
rtsyn_engine_event_publish_values(rtsyn_engine_t *engine, uint64_t timestamp_ns,
                                  uint32_t node_id, rtsyn_spsc_telemetry_source_t source,
                                  const rtsyn_spsc_telemetry_value_t *samples,
                                  size_t sample_count)
{
    if (!engine || !engine->config.telemetry_queue || !engine->config.telemetry_values || !samples
        || sample_count == 0)
    {
        return false;
    }

    rtsyn_spsc_telemetry_message_t message = {0};
    message.seq = rtsyn_engine_next_telemetry_seq(engine);
    message.timestamp_ns = timestamp_ns;
    message.dropped_event_count = engine->dropped_telemetry_events;
    message.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN;
    message.data.values_written.cycle_id = engine->cycle_id;
    message.data.values_written.node_id = node_id;
    message.data.values_written.source = source;

    if (!rtsyn_spsc_telemetry_try_publish_values(engine->config.telemetry_queue,
                                                 engine->config.telemetry_values, &message,
                                                 samples, sample_count))
    {
        rtsyn_engine_account_dropped_values(engine, sample_count);
        return false;
    }

    return true;
}

bool
rtsyn_engine_event_publish_requested_values(rtsyn_engine_t *engine, uint64_t timestamp_ns)
{
    if (!engine || !engine->config.runtime)
    {
        return false;
    }

    bool published = true;
    for (uint32_t i = 0; i < RTSYN_ENGINE_DEFAULT_TELEMETRY_SUBSCRIPTION_CAPACITY; i++)
    {
        const rtsyn_engine_telemetry_subscription_t *subscription =
            &engine->telemetry_subscriptions[i];
        if (!subscription->used)
        {
            continue;
        }

        const rtsyn_spsc_telemetry_source_t source =
            rtsyn_engine_get_node_source(engine, subscription->node_id);
        if (source == RTSYN_SPSC_TELEMETRY_SOURCE_NONE)
        {
            continue;
        }

        rtsyn_spsc_telemetry_value_t samples[128] = {0};
        size_t sample_count = 0;
        for (uint32_t port_id = 0; port_id < 64; port_id++)
        {
            if ((subscription->port_values_mask & (UINT64_C(1) << port_id)) == 0)
            {
                continue;
            }

            const rtsyn_value_t *value =
                rtsyn_engine_get_node_port_value(engine, subscription->node_id, port_id);
            if (!value)
            {
                continue;
            }

            if (rtsyn_engine_event_sample_value(value, timestamp_ns, subscription->node_id, port_id,
                                                (uint16_t)sample_count,
                                                RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT, source,
                                                &samples[sample_count]))
            {
                samples[sample_count].cycle_id = engine->cycle_id;
                sample_count++;
            }
        }

        for (uint32_t state_id = 0; state_id < 64; state_id++)
        {
            if ((subscription->variable_mask & (UINT64_C(1) << state_id)) == 0)
            {
                continue;
            }

            const rtsyn_value_t *value =
                rtsyn_runtime_get_node_state(engine->config.runtime, subscription->node_id,
                                             state_id);
            if (!value)
            {
                continue;
            }

            if (rtsyn_engine_event_sample_value(value, timestamp_ns, subscription->node_id,
                                                state_id, (uint16_t)sample_count,
                                                RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE, source,
                                                &samples[sample_count]))
            {
                samples[sample_count].cycle_id = engine->cycle_id;
                sample_count++;
            }
        }

        if (sample_count > 0
            && !rtsyn_engine_event_publish_values(engine, timestamp_ns, subscription->node_id,
                                                  source, samples, sample_count))
        {
            published = false;
        }
    }

    return published;
}
