/**
 * @file rtsyn/internal/engine.h
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief Private RTSyn Engine definitions.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#ifndef RTSYN_INTERNAL_ENGINE_H
#define RTSYN_INTERNAL_ENGINE_H

#ifdef __cplusplus
#include <atomic>
typedef std::atomic_bool rtsyn_engine_atomic_bool_t;
#else
#include <stdatomic.h>
typedef atomic_bool rtsyn_engine_atomic_bool_t;
#endif

#include <rtsyn/engine/defaults.h>
#include <rtsyn/collection.h>
#include <rtsyn/measurement_tool.h>
#include <rtsyn/module_loader.h>

#include "rtsyn/engine.h"
#include "rtsyn/thread.h"

typedef struct rtsyn_engine_telemetry_subscription_s {
    bool used;
    uint32_t node_id;
    uint64_t port_values_mask;
    uint64_t variable_mask;
} rtsyn_engine_telemetry_subscription_t;

typedef struct rtsyn_engine_loaded_node_s {
    rtsyn_module_loader_t *loader;
    const rtsyn_abi_node_descriptor_t *descriptor;
} rtsyn_engine_loaded_node_t;

struct rtsyn_engine_s {
    rtsyn_engine_config_t config;
    rtsyn_collection_t *loaded_nodes;
    rtsyn_thread_t *rt_thread;
    rtsyn_thread_t *wait_thread;
    rtsyn_engine_atomic_bool_t running;
    rtsyn_engine_atomic_bool_t started;
    rtsyn_engine_atomic_bool_t paused;
    rtsyn_engine_atomic_bool_t stop_requested;
    rtsyn_engine_atomic_bool_t rt_joined;
    rtsyn_engine_atomic_bool_t wait_joined;
    uint64_t cycle_id;
    uint64_t telemetry_seq;
    uint64_t dropped_telemetry_events;
    uint64_t dropped_telemetry_values;
    rtsyn_measurement_tool_t *measurement_tool;
    rtsyn_engine_telemetry_subscription_t
        telemetry_subscriptions[RTSYN_ENGINE_DEFAULT_TELEMETRY_SUBSCRIPTION_CAPACITY];
};

uint64_t
rtsyn_engine_next_telemetry_seq(rtsyn_engine_t *engine);

void
rtsyn_engine_account_dropped_event(rtsyn_engine_t *engine);

void
rtsyn_engine_account_dropped_values(rtsyn_engine_t *engine, uint64_t count);

void *
rtsyn_engine_rt_thread_main(void *arg);

void *
rtsyn_engine_wait_thread_main(void *arg);

const rtsyn_abi_node_descriptor_t *
rtsyn_engine_load_node_descriptor_as(rtsyn_engine_t *engine, const char *module_path,
                                     rtsyn_abi_node_type_t node_type);

#endif /* RTSYN_INTERNAL_ENGINE_H */
