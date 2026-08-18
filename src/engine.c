/**
 * @file engine.c
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief Compilation unit for the RTSyn Engine.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#include <pthread.h>
#include <rtsyn/collection.h>
#include <rtsyn/engine/defaults.h>
#include <rtsyn/module_loader.h>
#include <rtsyn/node/instance/device.h>
#include <rtsyn/node/instance/plugin.h>
#include <stdlib.h>
#include <string.h>

#include "rtsyn/engine.h"
#include "rtsyn/internal/engine.h"
#include "rtsyn/internal/engine/events.h"

static bool
rtsyn_engine_loaded_node_cmp_by_name(rtsyn_engine_loaded_node_t *loaded_node, const char *name)
{
    return loaded_node && loaded_node->descriptor && loaded_node->descriptor->name && name
           && strcmp(loaded_node->descriptor->name, name) == 0;
}

static void
rtsyn_engine_loaded_node_destroy(rtsyn_engine_loaded_node_t *loaded_node)
{
    if (!loaded_node)
    {
        return;
    }

    rtsyn_module_loader_destroy(loaded_node->loader);
    free(loaded_node);
}

static rtsyn_node_t *
rtsyn_engine_create_node_from_descriptor(const rtsyn_abi_node_descriptor_t *descriptor)
{
    if (!descriptor)
    {
        return nullptr;
    }

    switch (descriptor->node_type)
    {
        case RTSYN_ABI_NODE_PLUGIN:
            return rtsyn_node_create_plugin(descriptor);
        case RTSYN_ABI_NODE_DEVICE:
            return rtsyn_node_create_device(descriptor);
        default:
            return nullptr;
    }
}

static bool
rtsyn_engine_thread_config_is_valid(const rtsyn_engine_thread_config_t *config)
{
    return config && (config->inheritsched == PTHREAD_INHERIT_SCHED
                      || config->inheritsched == PTHREAD_EXPLICIT_SCHED);
}

static rtsyn_thread_t *
rtsyn_engine_create_thread(const rtsyn_engine_thread_config_t *config)
{
    return rtsyn_thread_create(config->priority, config->stack_size, config->policy,
                               config->inheritsched, config->cpuset, config->use_affinity);
}

static void
rtsyn_engine_config_init_rt_thread(rtsyn_engine_thread_config_t *thread_config)
{
#if defined(RTSYN_ENGINE_THREAD_CORE_PREEMPT_RT) || defined(RTSYN_ENGINE_THREAD_CORE_XENOMAI)
    thread_config->priority = RTSYN_ENGINE_DEFAULT_RT_THREAD_PRIORITY;
    thread_config->policy = RTSYN_ENGINE_DEFAULT_RT_THREAD_POLICY;
    thread_config->inheritsched = RTSYN_ENGINE_DEFAULT_RT_THREAD_INHERITSCHED;
#else
    thread_config->priority = RTSYN_ENGINE_DEFAULT_WAIT_THREAD_PRIORITY;
    thread_config->policy = RTSYN_ENGINE_DEFAULT_WAIT_THREAD_POLICY;
    thread_config->inheritsched = RTSYN_ENGINE_DEFAULT_WAIT_THREAD_INHERITSCHED;
#endif
}

static rtsyn_measurement_probe_stage_t
rtsyn_engine_measurement_stage_from_runtime(rtsyn_runtime_probe_stage_t stage)
{
    switch (stage)
    {
        case RTSYN_RUNTIME_PROBE_STAGE_BEFORE_DEVICE_FIRST:
            return RTSYN_MEASUREMENT_PROBE_STAGE_BEFORE_DEVICE_FIRST;
        case RTSYN_RUNTIME_PROBE_STAGE_AFTER_DEVICE_FIRST:
            return RTSYN_MEASUREMENT_PROBE_STAGE_AFTER_DEVICE_FIRST;
        case RTSYN_RUNTIME_PROBE_STAGE_BEFORE_PLUGIN:
            return RTSYN_MEASUREMENT_PROBE_STAGE_BEFORE_PLUGIN;
        case RTSYN_RUNTIME_PROBE_STAGE_AFTER_PLUGIN:
            return RTSYN_MEASUREMENT_PROBE_STAGE_AFTER_PLUGIN;
        case RTSYN_RUNTIME_PROBE_STAGE_BEFORE_DEVICE_FINAL:
            return RTSYN_MEASUREMENT_PROBE_STAGE_BEFORE_DEVICE_FINAL;
        case RTSYN_RUNTIME_PROBE_STAGE_AFTER_DEVICE_FINAL:
            return RTSYN_MEASUREMENT_PROBE_STAGE_AFTER_DEVICE_FINAL;
        default:
            return RTSYN_MEASUREMENT_PROBE_STAGE_COUNT;
    }
}

static void
rtsyn_engine_runtime_probe(void *user_data, uint64_t cycle_index,
                           rtsyn_runtime_probe_stage_t stage, uint64_t timestamp_ns)
{
    rtsyn_engine_t *engine = (rtsyn_engine_t *)user_data;
    if (!engine || !engine->measurement_tool)
    {
        return;
    }

    const rtsyn_measurement_probe_stage_t measurement_stage =
        rtsyn_engine_measurement_stage_from_runtime(stage);
    if (measurement_stage >= RTSYN_MEASUREMENT_PROBE_STAGE_COUNT)
    {
        return;
    }

    (void)rtsyn_measurement_tool_record_probe(engine->measurement_tool, cycle_index,
                                             measurement_stage, timestamp_ns);
}

void
rtsyn_engine_config_init(rtsyn_engine_config_t *config)
{
    if (!config)
    {
        return;
    }

    memset(config, 0, sizeof(*config));
    rtsyn_engine_config_init_rt_thread(&config->rt_thread);
    config->wait_thread.priority = RTSYN_ENGINE_DEFAULT_WAIT_THREAD_PRIORITY;
    config->wait_thread.policy = RTSYN_ENGINE_DEFAULT_WAIT_THREAD_POLICY;
    config->wait_thread.inheritsched = RTSYN_ENGINE_DEFAULT_WAIT_THREAD_INHERITSCHED;
    config->max_commands_per_cycle = RTSYN_ENGINE_DEFAULT_COMMAND_BUDGET;
}

rtsyn_engine_t *
rtsyn_engine_create(const rtsyn_engine_config_t *config)
{
    if (!config || !config->runtime || !config->command_queue
        || !rtsyn_engine_thread_config_is_valid(&config->rt_thread)
        || !rtsyn_engine_thread_config_is_valid(&config->wait_thread))
    {
        return NULL;
    }

    rtsyn_engine_t *engine = (rtsyn_engine_t *)malloc(sizeof(rtsyn_engine_t));
    if (!engine)
    {
        return NULL;
    }

    memset(engine, 0, sizeof(*engine));
    engine->config = *config;
    if (engine->config.max_commands_per_cycle == 0)
    {
        engine->config.max_commands_per_cycle = RTSYN_ENGINE_DEFAULT_COMMAND_BUDGET;
    }

    atomic_init(&engine->running, false);
    atomic_init(&engine->started, false);
    atomic_init(&engine->paused, false);
    atomic_init(&engine->stop_requested, false);
    atomic_init(&engine->rt_joined, false);
    atomic_init(&engine->wait_joined, false);

    engine->loaded_nodes = rtsyn_collection_create_linked_list(
        (rtsyn_collection_cmp_key_fn_t)rtsyn_engine_loaded_node_cmp_by_name,
        (rtsyn_collection_destroy_elem_fn_t)rtsyn_engine_loaded_node_destroy);
    if (!engine->loaded_nodes)
    {
        free(engine);
        return NULL;
    }

    engine->measurement_tool = rtsyn_measurement_tool_create();
    if (!engine->measurement_tool)
    {
        rtsyn_collection_destroy(engine->loaded_nodes);
        free(engine);
        return NULL;
    }
    rtsyn_runtime_set_probe_callback(engine->config.runtime, rtsyn_engine_runtime_probe, engine);

    engine->rt_thread = rtsyn_engine_create_thread(&engine->config.rt_thread);
    if (!engine->rt_thread)
    {
        rtsyn_runtime_set_probe_callback(engine->config.runtime, NULL, NULL);
        rtsyn_measurement_tool_destroy(engine->measurement_tool);
        rtsyn_collection_destroy(engine->loaded_nodes);
        free(engine);
        return NULL;
    }

    engine->wait_thread = rtsyn_engine_create_thread(&engine->config.wait_thread);
    if (!engine->wait_thread)
    {
        rtsyn_runtime_set_probe_callback(engine->config.runtime, NULL, NULL);
        rtsyn_measurement_tool_destroy(engine->measurement_tool);
        rtsyn_thread_destroy(engine->rt_thread);
        rtsyn_collection_destroy(engine->loaded_nodes);
        free(engine);
        return NULL;
    }

    return engine;
}

void
rtsyn_engine_destroy(rtsyn_engine_t *engine)
{
    if (!engine)
    {
        return;
    }

    rtsyn_engine_request_stop(engine);
    if (atomic_load_explicit(&engine->started, memory_order_acquire)
        && !atomic_load_explicit(&engine->wait_joined, memory_order_acquire))
    {
        (void)rtsyn_engine_join(engine);
    }

    rtsyn_runtime_set_probe_callback(engine->config.runtime, NULL, NULL);
    rtsyn_measurement_tool_destroy(engine->measurement_tool);
    rtsyn_thread_destroy(engine->wait_thread);
    rtsyn_thread_destroy(engine->rt_thread);
    rtsyn_collection_destroy(engine->loaded_nodes);
    free(engine);
}

bool
rtsyn_engine_start(rtsyn_engine_t *engine)
{
    if (!engine || atomic_load_explicit(&engine->running, memory_order_acquire))
    {
        return false;
    }

    atomic_store_explicit(&engine->stop_requested, false, memory_order_release);
    atomic_store_explicit(&engine->paused, false, memory_order_release);
    atomic_store_explicit(&engine->rt_joined, false, memory_order_release);
    atomic_store_explicit(&engine->wait_joined, false, memory_order_release);
    atomic_store_explicit(&engine->running, true, memory_order_release);
    atomic_store_explicit(&engine->started, true, memory_order_release);

    if (rtsyn_thread_run(engine->rt_thread, rtsyn_engine_rt_thread_main, engine) != 0)
    {
        atomic_store_explicit(&engine->started, false, memory_order_release);
        atomic_store_explicit(&engine->running, false, memory_order_release);
        return false;
    }

    if (rtsyn_thread_run(engine->wait_thread, rtsyn_engine_wait_thread_main, engine) != 0)
    {
        rtsyn_engine_request_stop(engine);
        (void)rtsyn_thread_join(engine->rt_thread, NULL);
        atomic_store_explicit(&engine->rt_joined, true, memory_order_release);
        atomic_store_explicit(&engine->started, false, memory_order_release);
        atomic_store_explicit(&engine->running, false, memory_order_release);
        return false;
    }

    return true;
}

void
rtsyn_engine_request_stop(rtsyn_engine_t *engine)
{
    if (!engine)
    {
        return;
    }

    atomic_store_explicit(&engine->stop_requested, true, memory_order_release);
}

bool
rtsyn_engine_join(rtsyn_engine_t *engine)
{
    if (!engine || !atomic_load_explicit(&engine->started, memory_order_acquire))
    {
        return false;
    }

    if (atomic_load_explicit(&engine->wait_joined, memory_order_acquire))
    {
        return true;
    }

    if (rtsyn_thread_join(engine->wait_thread, NULL) != 0)
    {
        return false;
    }

    atomic_store_explicit(&engine->wait_joined, true, memory_order_release);
    atomic_store_explicit(&engine->started, false, memory_order_release);
    return true;
}

bool
rtsyn_engine_is_running(const rtsyn_engine_t *engine)
{
    return engine && atomic_load_explicit(&engine->running, memory_order_acquire);
}

bool
rtsyn_engine_is_stop_requested(const rtsyn_engine_t *engine)
{
    return engine && atomic_load_explicit(&engine->stop_requested, memory_order_acquire);
}

bool
rtsyn_engine_set_rt_thread_priority(rtsyn_engine_t *engine, int priority)
{
    if (!engine || priority < 0 || priority > 99)
    {
        return false;
    }

    engine->config.rt_thread.priority = priority;
    return rtsyn_thread_set_priority(engine->rt_thread, priority) == 0;
}

bool
rtsyn_engine_set_deadline_tolerance(rtsyn_engine_t *engine, uint64_t tolerance_ns)
{
    if (!engine)
    {
        return false;
    }

    engine->config.deadline_tolerance_ns = tolerance_ns;
    return true;
}

bool
rtsyn_engine_load_node(rtsyn_engine_t *engine, const char *module_path)
{
    return rtsyn_engine_load_node_as(engine, module_path, RTSYN_ABI_NODE_INVALID);
}

bool
rtsyn_engine_load_node_as(rtsyn_engine_t *engine, const char *module_path,
                          rtsyn_abi_node_type_t node_type)
{
    return rtsyn_engine_load_node_descriptor_as(engine, module_path, node_type) != nullptr;
}

const rtsyn_abi_node_descriptor_t *
rtsyn_engine_load_node_descriptor_as(rtsyn_engine_t *engine, const char *module_path,
                                     rtsyn_abi_node_type_t node_type)
{
    if (!engine || !module_path)
    {
        return nullptr;
    }

    rtsyn_module_loader_t *loader = rtsyn_module_loader_create(module_path);
    if (!loader)
    {
        return nullptr;
    }

    const rtsyn_abi_node_descriptor_t *descriptor = rtsyn_module_loader_get_descriptor(loader);
    if (!descriptor || !descriptor->name)
    {
        rtsyn_module_loader_destroy(loader);
        return nullptr;
    }
    if (node_type != RTSYN_ABI_NODE_INVALID && descriptor->node_type != node_type)
    {
        rtsyn_module_loader_destroy(loader);
        return nullptr;
    }

    rtsyn_engine_loaded_node_t *loaded_node =
        (rtsyn_engine_loaded_node_t *)malloc(sizeof(rtsyn_engine_loaded_node_t));
    if (!loaded_node)
    {
        rtsyn_module_loader_destroy(loader);
        return nullptr;
    }

    loaded_node->loader = loader;
    loaded_node->descriptor = descriptor;

    (void)rtsyn_runtime_remove_nodes_by_name(engine->config.runtime, descriptor->name);

    if (rtsyn_collection_contains(engine->loaded_nodes, (void *)descriptor->name))
    {
        (void)rtsyn_collection_remove(engine->loaded_nodes, (void *)descriptor->name);
    }

    if (!rtsyn_collection_add(engine->loaded_nodes, loaded_node, (void *)descriptor->name))
    {
        rtsyn_engine_loaded_node_destroy(loaded_node);
        return nullptr;
    }

    return descriptor;
}

rtsyn_node_t *
rtsyn_engine_add_node(rtsyn_engine_t *engine, const char *node_name)
{
    if (!engine || !node_name)
    {
        return nullptr;
    }

    rtsyn_engine_loaded_node_t *loaded_node =
        (rtsyn_engine_loaded_node_t *)rtsyn_collection_get(engine->loaded_nodes, (void *)node_name);
    if (!loaded_node)
    {
        return nullptr;
    }

    rtsyn_node_t *node = rtsyn_engine_create_node_from_descriptor(loaded_node->descriptor);
    if (!node)
    {
        return nullptr;
    }

    if (!rtsyn_runtime_add_node(engine->config.runtime, node))
    {
        rtsyn_node_destroy(node);
        return nullptr;
    }

    return node;
}

rtsyn_node_t *
rtsyn_engine_add_node_as(rtsyn_engine_t *engine, const char *node_name,
                         rtsyn_abi_node_type_t node_type)
{
    if (!engine || !node_name || !rtsyn_abi_node_type_is_valid(node_type))
    {
        return nullptr;
    }

    rtsyn_engine_loaded_node_t *loaded_node =
        (rtsyn_engine_loaded_node_t *)rtsyn_collection_get(engine->loaded_nodes, (void *)node_name);
    if (!loaded_node || !loaded_node->descriptor || loaded_node->descriptor->node_type != node_type)
    {
        return nullptr;
    }

    return rtsyn_engine_add_node(engine, node_name);
}

bool
rtsyn_engine_dispatch_command(rtsyn_engine_t *engine,
                              const rtsyn_spsc_command_message_t *message)
{
    return rtsyn_engine_event_dispatch_command(engine, message);
}

bool
rtsyn_engine_publish_values(rtsyn_engine_t *engine, uint64_t timestamp_ns, uint32_t node_id,
                            rtsyn_spsc_telemetry_source_t source,
                            const rtsyn_spsc_telemetry_value_t *samples, size_t sample_count)
{
    return rtsyn_engine_event_publish_values(engine, timestamp_ns, node_id, source, samples,
                                            sample_count);
}

uint64_t
rtsyn_engine_next_telemetry_seq(rtsyn_engine_t *engine)
{
    return engine ? ++engine->telemetry_seq : 0;
}

void
rtsyn_engine_account_dropped_event(rtsyn_engine_t *engine)
{
    if (engine)
    {
        engine->dropped_telemetry_events++;
    }
}

void
rtsyn_engine_account_dropped_values(rtsyn_engine_t *engine, uint64_t count)
{
    if (engine)
    {
        engine->dropped_telemetry_values += count;
    }
}
