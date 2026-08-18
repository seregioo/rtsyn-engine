/**
 * @file rtsyn/engine.h
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief Header file for the RTSyn Engine.
 *
 * The engine owns the runtime execution threads and adapts command and telemetry
 * SPSC queues to the runtime loop. It does not provide a CLI or create runtime
 * graphs by itself.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#ifndef RTSYN_ENGINE_H
#define RTSYN_ENGINE_H

#include <pthread.h>
#include <rtsyn/runtime.h>
#include <rtsyn/spsc/command/spsc.h>
#include <rtsyn/spsc/result/spsc.h>
#include <rtsyn/spsc/telemetry/spsc.h>
#include <rtsyn/spsc/telemetry/values.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtsyn_engine_s rtsyn_engine_t;

typedef enum rtsyn_engine_global_command_e : uint8_t {
    RTSYN_ENGINE_GLOBAL_COMMAND_NONE = 0,
    RTSYN_ENGINE_GLOBAL_COMMAND_STOP,
    RTSYN_ENGINE_GLOBAL_COMMAND_PAUSE,
    RTSYN_ENGINE_GLOBAL_COMMAND_RESUME,
    RTSYN_ENGINE_GLOBAL_COMMAND_COUNT,
} rtsyn_engine_global_command_t;

typedef struct rtsyn_engine_thread_config_e {
    int priority;
    size_t stack_size;
    int policy;
    int inheritsched;
    cpu_set_t cpuset;
    bool use_affinity;
} rtsyn_engine_thread_config_t;

typedef struct rtsyn_engine_config_e {
    rtsyn_runtime_t *runtime;
    rtsyn_spsc_command_queue_t *command_queue;
    rtsyn_spsc_result_queue_t *result_queue;
    rtsyn_spsc_telemetry_queue_t *telemetry_queue;
    rtsyn_spsc_telemetry_values_t *telemetry_values;
    rtsyn_engine_thread_config_t rt_thread;
    rtsyn_engine_thread_config_t wait_thread;
    uint32_t max_commands_per_cycle;
} rtsyn_engine_config_t;

void
rtsyn_engine_config_init(rtsyn_engine_config_t *config);

rtsyn_engine_t *
rtsyn_engine_create(const rtsyn_engine_config_t *config);

void
rtsyn_engine_destroy(rtsyn_engine_t *engine);

bool
rtsyn_engine_start(rtsyn_engine_t *engine);

void
rtsyn_engine_request_stop(rtsyn_engine_t *engine);

bool
rtsyn_engine_join(rtsyn_engine_t *engine);

bool
rtsyn_engine_is_running(const rtsyn_engine_t *engine);

bool
rtsyn_engine_is_stop_requested(const rtsyn_engine_t *engine);

bool
rtsyn_engine_set_rt_thread_priority(rtsyn_engine_t *engine, int priority);

bool
rtsyn_engine_set_deadline_tolerance(rtsyn_engine_t *engine, uint64_t tolerance_ns);

bool
rtsyn_engine_load_node(rtsyn_engine_t *engine, const char *module_path);

bool
rtsyn_engine_load_node_as(rtsyn_engine_t *engine, const char *module_path,
                          rtsyn_abi_node_type_t node_type);

rtsyn_node_t *
rtsyn_engine_add_node(rtsyn_engine_t *engine, const char *node_name);

rtsyn_node_t *
rtsyn_engine_add_node_as(rtsyn_engine_t *engine, const char *node_name,
                         rtsyn_abi_node_type_t node_type);

bool
rtsyn_engine_dispatch_command(rtsyn_engine_t *engine,
                              const rtsyn_spsc_command_message_t *message);

bool
rtsyn_engine_publish_values(rtsyn_engine_t *engine, uint64_t timestamp_ns, uint32_t node_id,
                            rtsyn_spsc_telemetry_source_t source,
                            const rtsyn_spsc_telemetry_value_t *samples, size_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* RTSYN_ENGINE_H */
