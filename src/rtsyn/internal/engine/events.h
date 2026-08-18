/**
 * @file rtsyn/internal/engine/events.h
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief Private RTSyn Engine event dispatcher declarations.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#ifndef RTSYN_INTERNAL_ENGINE_EVENTS_H
#define RTSYN_INTERNAL_ENGINE_EVENTS_H

#include <rtsyn/spsc/command/message.h>
#include <rtsyn/spsc/result/message.h>
#include <rtsyn/spsc/telemetry/message.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rtsyn/internal/engine.h"

bool
rtsyn_engine_event_dispatch_command(rtsyn_engine_t *engine,
                                    const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_global_command(rtsyn_engine_t *engine,
                                  const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_plugin_update(rtsyn_engine_t *engine,
                                 const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_request_port_values(rtsyn_engine_t *engine,
                                       const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_request_port_variables(rtsyn_engine_t *engine,
                                          const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_load_node(rtsyn_engine_t *engine,
                             const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_add_node(rtsyn_engine_t *engine,
                            const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_remove_node(rtsyn_engine_t *engine,
                               const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_add_connection(rtsyn_engine_t *engine,
                                  const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_remove_connection(rtsyn_engine_t *engine,
                                     const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_set_param(rtsyn_engine_t *engine,
                             const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_request_runtime_nodes(rtsyn_engine_t *engine,
                                         const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_event_publish_cycle_begin(rtsyn_engine_t *engine, uint64_t timestamp_ns);

bool
rtsyn_engine_event_publish_cycle_end(rtsyn_engine_t *engine, uint64_t started_at_ns,
                                     uint64_t finished_at_ns);

bool
rtsyn_engine_event_publish_measurement(rtsyn_engine_t *engine, uint64_t timestamp_ns,
                                       const rtsyn_measurement_metrics_t *metrics);

bool
rtsyn_engine_event_publish_dropped(rtsyn_engine_t *engine, uint64_t timestamp_ns);

bool
rtsyn_engine_event_publish_values(rtsyn_engine_t *engine, uint64_t timestamp_ns,
                                  uint32_t node_id, rtsyn_spsc_telemetry_source_t source,
                                  const rtsyn_spsc_telemetry_value_t *samples,
                                  size_t sample_count);

bool
rtsyn_engine_event_publish_requested_values(rtsyn_engine_t *engine, uint64_t timestamp_ns);

bool
rtsyn_engine_event_push_telemetry(rtsyn_engine_t *engine,
                                  rtsyn_spsc_telemetry_message_t *message);

bool
rtsyn_engine_event_push_result(rtsyn_engine_t *engine, rtsyn_spsc_result_message_t *message);

#endif /* RTSYN_INTERNAL_ENGINE_EVENTS_H */
