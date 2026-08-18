/**
 * @file engine/events/telemetry_command.c
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief RTSyn Engine telemetry command events.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#include <string.h>

#include "rtsyn/internal/engine/events.h"

static rtsyn_engine_telemetry_subscription_t *
rtsyn_engine_find_subscription(rtsyn_engine_t *engine, uint32_t node_id)
{
    for (uint32_t i = 0; i < RTSYN_ENGINE_DEFAULT_TELEMETRY_SUBSCRIPTION_CAPACITY; i++)
    {
        rtsyn_engine_telemetry_subscription_t *subscription = &engine->telemetry_subscriptions[i];
        if (subscription->used && subscription->node_id == node_id)
        {
            return subscription;
        }
    }

    return NULL;
}

static rtsyn_engine_telemetry_subscription_t *
rtsyn_engine_get_or_create_subscription(rtsyn_engine_t *engine, uint32_t node_id)
{
    rtsyn_engine_telemetry_subscription_t *subscription =
        rtsyn_engine_find_subscription(engine, node_id);
    if (subscription)
    {
        return subscription;
    }

    for (uint32_t i = 0; i < RTSYN_ENGINE_DEFAULT_TELEMETRY_SUBSCRIPTION_CAPACITY; i++)
    {
        subscription = &engine->telemetry_subscriptions[i];
        if (!subscription->used)
        {
            memset(subscription, 0, sizeof(*subscription));
            subscription->used = true;
            subscription->node_id = node_id;
            return subscription;
        }
    }

    return NULL;
}

static void
rtsyn_engine_cleanup_subscription(rtsyn_engine_telemetry_subscription_t *subscription)
{
    if (!subscription || subscription->port_values_mask != 0 || subscription->variable_mask != 0)
    {
        return;
    }

    memset(subscription, 0, sizeof(*subscription));
}

static void
rtsyn_engine_copy_string(char *destination, size_t destination_size, const char *source)
{
    if (!destination || destination_size == 0)
    {
        return;
    }

    if (!source)
    {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

static void
rtsyn_engine_result_copy_descriptor(rtsyn_spsc_result_node_descriptor_t *result,
                                    const rtsyn_abi_node_descriptor_t *descriptor)
{
    if (!result || !descriptor)
    {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->node_type = descriptor->node_type;
    result->port_count = descriptor->port_count;
    result->param_count = descriptor->param_count;
    result->state_count = descriptor->state_count;
    rtsyn_engine_copy_string(result->name, sizeof(result->name), descriptor->name);

    const uint32_t port_count = descriptor->port_count < RTSYN_SPSC_RESULT_PORT_CAPACITY
                                    ? descriptor->port_count
                                    : RTSYN_SPSC_RESULT_PORT_CAPACITY;
    for (uint32_t i = 0; i < port_count; i++)
    {
        result->ports[i].id = i;
        result->ports[i].value_type = descriptor->ports[i].value_type;
        result->ports[i].direction = descriptor->ports[i].direction;
        rtsyn_engine_copy_string(result->ports[i].name, sizeof(result->ports[i].name),
                                 descriptor->ports[i].name);
    }

    const uint32_t param_count = descriptor->param_count < RTSYN_SPSC_RESULT_PARAM_CAPACITY
                                     ? descriptor->param_count
                                     : RTSYN_SPSC_RESULT_PARAM_CAPACITY;
    for (uint32_t i = 0; i < param_count; i++)
    {
        result->params[i].id = i;
        result->params[i].value_type = descriptor->params[i].value_type;
        rtsyn_engine_copy_string(result->params[i].name, sizeof(result->params[i].name),
                                 descriptor->params[i].name);
        rtsyn_engine_copy_string(result->params[i].description,
                                 sizeof(result->params[i].description),
                                 descriptor->params[i].description);
    }

    const uint32_t state_count = descriptor->state_count < RTSYN_SPSC_RESULT_STATE_CAPACITY
                                     ? descriptor->state_count
                                     : RTSYN_SPSC_RESULT_STATE_CAPACITY;
    for (uint32_t i = 0; i < state_count; i++)
    {
        result->states[i].id = i;
        result->states[i].value_type = descriptor->states[i].value_type;
        rtsyn_engine_copy_string(result->states[i].name, sizeof(result->states[i].name),
                                 descriptor->states[i].name);
        rtsyn_engine_copy_string(result->states[i].description,
                                 sizeof(result->states[i].description),
                                 descriptor->states[i].description);
    }
}

typedef struct rtsyn_engine_runtime_snapshot_context_s {
    rtsyn_engine_t *engine;
    uint64_t seq;
    bool ok;
} rtsyn_engine_runtime_snapshot_context_t;

static _Thread_local rtsyn_engine_runtime_snapshot_context_t
    *RTSYN_ENGINE_RUNTIME_SNAPSHOT_CONTEXT = NULL;

static bool
rtsyn_engine_event_push_node_snapshot(rtsyn_node_t *node, void *user_data)
{
    (void)user_data;
    rtsyn_engine_runtime_snapshot_context_t *context = RTSYN_ENGINE_RUNTIME_SNAPSHOT_CONTEXT;
    if (!context || !context->engine || !node)
    {
        return false;
    }

    const char *node_name = rtsyn_node_get_name(node);
    rtsyn_engine_loaded_node_t *loaded_node =
        (rtsyn_engine_loaded_node_t *)rtsyn_collection_get(context->engine->loaded_nodes,
                                                          (void *)node_name);
    if (!loaded_node || !loaded_node->descriptor)
    {
        return true;
    }

    rtsyn_node_runtime_state_t runtime_state = rtsyn_node_get_runtime_state(node);
    if (runtime_state == RTSYN_NODE_RUNTIME_STATE_INVALID)
    {
        runtime_state = RTSYN_NODE_RUNTIME_STATE_STOP;
    }

    rtsyn_spsc_result_message_t result = {0};
    result.seq = context->seq;
    result.command_type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES;
    result.status = RTSYN_SPSC_RESULT_STATUS_OK;
    result.status_code = (uint32_t)runtime_state;
    result.node_id = rtsyn_node_get_id(node);
    rtsyn_engine_result_copy_descriptor(&result.node, loaded_node->descriptor);
    context->ok = rtsyn_engine_event_push_result(context->engine, &result) && context->ok;
    return true;
}

static bool
rtsyn_engine_event_push_connection_snapshot(rtsyn_runtime_connection_t *connection,
                                            void *user_data)
{
    (void)user_data;
    rtsyn_engine_runtime_snapshot_context_t *context = RTSYN_ENGINE_RUNTIME_SNAPSHOT_CONTEXT;
    if (!context || !context->engine || !connection)
    {
        return false;
    }

    rtsyn_port_t *source_port = rtsyn_runtime_connection_get_source_port(connection);
    rtsyn_port_t *destination_port = rtsyn_runtime_connection_get_destination_port(connection);
    if (!source_port || !destination_port)
    {
        return true;
    }

    rtsyn_spsc_result_message_t result = {0};
    result.seq = context->seq;
    result.command_type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES;
    result.status = RTSYN_SPSC_RESULT_STATUS_OK;
    result.status_code = 0;
    result.node_id = RTSYN_NODE_ID_INVALID;
    result.connection.connection_id = rtsyn_runtime_connection_get_id(connection);
    result.connection.source_node_id = rtsyn_runtime_connection_get_source_node_id(connection);
    result.connection.source_port_id = rtsyn_port_get_id(source_port);
    result.connection.destination_node_id =
        rtsyn_runtime_connection_get_destination_node_id(connection);
    result.connection.destination_port_id = rtsyn_port_get_id(destination_port);
    result.connection.timing = (uint32_t)rtsyn_runtime_connection_get_timing(connection);
    context->ok = rtsyn_engine_event_push_result(context->engine, &result) && context->ok;
    return true;
}

bool
rtsyn_engine_event_push_result(rtsyn_engine_t *engine, rtsyn_spsc_result_message_t *message)
{
    if (!engine || !message || !engine->config.result_queue)
    {
        return false;
    }

    return rtsyn_spsc_result_try_push(engine->config.result_queue, message);
}

static bool
rtsyn_engine_update_subscription_mask(rtsyn_engine_t *engine, uint32_t node_id, uint64_t mask,
                                      bool send, bool variables)
{
    if (!engine || node_id == RTSYN_NODE_ID_INVALID || mask == 0)
    {
        return false;
    }

    rtsyn_engine_telemetry_subscription_t *subscription =
        send ? rtsyn_engine_get_or_create_subscription(engine, node_id)
             : rtsyn_engine_find_subscription(engine, node_id);
    if (!subscription)
    {
        return !send;
    }

    uint64_t *target_mask =
        variables ? &subscription->variable_mask : &subscription->port_values_mask;
    if (send)
    {
        *target_mask |= mask;
    }
    else
    {
        *target_mask &= ~mask;
        rtsyn_engine_cleanup_subscription(subscription);
    }

    return true;
}

bool
rtsyn_engine_event_plugin_update(rtsyn_engine_t *engine,
                                 const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_PLUGIN_UPDATE)
    {
        return false;
    }

    return rtsyn_runtime_transition_node(
        engine->config.runtime, message->data.plugin_update.plugin_id,
        (rtsyn_node_runtime_state_t)message->data.plugin_update.plugin_state);
}

bool
rtsyn_engine_event_request_port_values(rtsyn_engine_t *engine,
                                       const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message
        || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VALUES)
    {
        return false;
    }

    return rtsyn_engine_update_subscription_mask(
        engine, message->data.plugin_request_ports.plugin_id,
        message->data.plugin_request_ports.portsyn_mask, message->data.plugin_request_ports.send,
        false);
}

bool
rtsyn_engine_event_request_port_variables(rtsyn_engine_t *engine,
                                          const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message
        || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VARIABLES)
    {
        return false;
    }

    return rtsyn_engine_update_subscription_mask(
        engine, message->data.plugin_request_variables.plugin_id,
        message->data.plugin_request_variables.variable_mask,
        message->data.plugin_request_variables.send, true);
}

bool
rtsyn_engine_event_load_node(rtsyn_engine_t *engine, const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE
        || !rtsyn_abi_node_type_is_valid(message->data.load_node.node_type)
        || message->data.load_node.module_path[0] == '\0')
    {
        return false;
    }

    char module_path[RTSYN_SPSC_COMMAND_MODULE_PATH_MAX_SIZE] = {0};
    strncpy(module_path, message->data.load_node.module_path, sizeof(module_path) - 1);
    const rtsyn_abi_node_descriptor_t *descriptor =
        rtsyn_engine_load_node_descriptor_as(engine, module_path, message->data.load_node.node_type);
    const bool loaded = descriptor != nullptr;

    rtsyn_spsc_result_message_t result = {0};
    result.seq = message->seq;
    result.command_type = message->type;
    result.status = loaded ? RTSYN_SPSC_RESULT_STATUS_OK : RTSYN_SPSC_RESULT_STATUS_ERROR;
    result.status_code = loaded ? 0 : 1;

    if (loaded)
    {
        rtsyn_engine_result_copy_descriptor(&result.node, descriptor);
    }

    (void)rtsyn_engine_event_push_result(engine, &result);
    return loaded;
}

bool
rtsyn_engine_event_add_node(rtsyn_engine_t *engine, const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE
        || !rtsyn_abi_node_type_is_valid(message->data.add_node.node_type)
        || message->data.add_node.node_name[0] == '\0')
    {
        return false;
    }

    char node_name[RTSYN_SPSC_COMMAND_NODE_NAME_MAX_SIZE] = {0};
    strncpy(node_name, message->data.add_node.node_name, sizeof(node_name) - 1);
    rtsyn_node_t *node =
        rtsyn_engine_add_node_as(engine, node_name, message->data.add_node.node_type);

    rtsyn_spsc_result_message_t result = {0};
    result.seq = message->seq;
    result.command_type = message->type;
    result.status = node ? RTSYN_SPSC_RESULT_STATUS_OK : RTSYN_SPSC_RESULT_STATUS_ERROR;
    result.status_code = node ? 0 : 1;
    result.node_id = node ? rtsyn_node_get_id(node) : RTSYN_NODE_ID_INVALID;

    rtsyn_engine_loaded_node_t *loaded_node =
        (rtsyn_engine_loaded_node_t *)rtsyn_collection_get(engine->loaded_nodes, (void *)node_name);
    if (loaded_node)
    {
        rtsyn_engine_result_copy_descriptor(&result.node, loaded_node->descriptor);
    }

    (void)rtsyn_engine_event_push_result(engine, &result);
    return node != nullptr;
}

bool
rtsyn_engine_event_request_runtime_nodes(rtsyn_engine_t *engine,
                                         const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message
        || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES
        || !engine->config.runtime)
    {
        return false;
    }

    rtsyn_engine_runtime_snapshot_context_t context = {
        .engine = engine,
        .seq = message->seq,
        .ok = true,
    };
    RTSYN_ENGINE_RUNTIME_SNAPSHOT_CONTEXT = &context;
    (void)rtsyn_runtime_for_each_node(engine->config.runtime,
                                      rtsyn_engine_event_push_node_snapshot, NULL);
    (void)rtsyn_runtime_for_each_connection(engine->config.runtime,
                                            rtsyn_engine_event_push_connection_snapshot, NULL);
    RTSYN_ENGINE_RUNTIME_SNAPSHOT_CONTEXT = NULL;

    rtsyn_spsc_result_message_t done = {0};
    done.seq = message->seq;
    done.command_type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES;
    done.status = RTSYN_SPSC_RESULT_STATUS_OK;
    done.status_code = UINT32_MAX;
    done.node_id = RTSYN_NODE_ID_INVALID;
    context.ok = rtsyn_engine_event_push_result(engine, &done) && context.ok;
    return context.ok;
}

bool
rtsyn_engine_event_remove_node(rtsyn_engine_t *engine, const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_NODE
        || message->data.remove_node.node_id == RTSYN_NODE_ID_INVALID)
    {
        return false;
    }

    const bool removed = rtsyn_runtime_remove_node_by_id(engine->config.runtime,
                                                        message->data.remove_node.node_id);
    rtsyn_spsc_result_message_t result = {0};
    result.seq = message->seq;
    result.command_type = message->type;
    result.status = removed ? RTSYN_SPSC_RESULT_STATUS_OK : RTSYN_SPSC_RESULT_STATUS_ERROR;
    result.status_code = removed ? 0 : 1;
    result.node_id = message->data.remove_node.node_id;
    (void)rtsyn_engine_event_push_result(engine, &result);
    return removed;
}

bool
rtsyn_engine_event_add_connection(rtsyn_engine_t *engine,
                                  const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_CONNECTION
        || message->data.add_connection.connection_id == RTSYN_RUNTIME_CONNECTION_ID_INVALID
        || message->data.add_connection.source_node_id == RTSYN_NODE_ID_INVALID
        || message->data.add_connection.source_port_id == RTSYN_PORT_ID_INVALID
        || message->data.add_connection.destination_node_id == RTSYN_NODE_ID_INVALID
        || message->data.add_connection.destination_port_id == RTSYN_PORT_ID_INVALID)
    {
        return false;
    }

    return rtsyn_runtime_add_connection_between(
        engine->config.runtime, message->data.add_connection.connection_id,
        message->data.add_connection.source_node_id, message->data.add_connection.source_port_id,
        message->data.add_connection.destination_node_id,
        message->data.add_connection.destination_port_id);
}

bool
rtsyn_engine_event_remove_connection(rtsyn_engine_t *engine,
                                     const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_CONNECTION
        || message->data.remove_connection.connection_id == RTSYN_RUNTIME_CONNECTION_ID_INVALID)
    {
        return false;
    }

    return rtsyn_runtime_remove_connection(engine->config.runtime,
                                           message->data.remove_connection.connection_id);
}

bool
rtsyn_engine_event_set_param(rtsyn_engine_t *engine,
                             const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_PARAM
        || message->data.set_param.node_id == RTSYN_NODE_ID_INVALID
        || message->data.set_param.param_id == RTSYN_NODE_VALUE_ID_INVALID
        || !rtsyn_abi_value_is_valid(message->data.set_param.value_type))
    {
        return false;
    }

    switch (message->data.set_param.value_type)
    {
        case RTSYN_ABI_VALUE_F32:
            return rtsyn_runtime_set_node_param(engine->config.runtime,
                                                message->data.set_param.node_id,
                                                message->data.set_param.param_id,
                                                &message->data.set_param.value.f32);
        case RTSYN_ABI_VALUE_F64:
            return rtsyn_runtime_set_node_param(engine->config.runtime,
                                                message->data.set_param.node_id,
                                                message->data.set_param.param_id,
                                                &message->data.set_param.value.f64);
        case RTSYN_ABI_VALUE_I64:
        {
            const int value = (int)message->data.set_param.value.i64;
            return rtsyn_runtime_set_node_param(engine->config.runtime,
                                                message->data.set_param.node_id,
                                                message->data.set_param.param_id, &value);
        }
        case RTSYN_ABI_VALUE_U64:
            return rtsyn_runtime_set_node_param(engine->config.runtime,
                                                message->data.set_param.node_id,
                                                message->data.set_param.param_id,
                                                &message->data.set_param.value.u64);
        case RTSYN_ABI_VALUE_STRING:
        {
            char value[RTSYN_SPSC_COMMAND_PARAM_STRING_MAX_SIZE] = {0};
            strncpy(value, message->data.set_param.value.string, sizeof(value) - 1);
            return rtsyn_runtime_set_node_param(engine->config.runtime,
                                                message->data.set_param.node_id,
                                                message->data.set_param.param_id, value);
        }
        default:
            return false;
    }
}
