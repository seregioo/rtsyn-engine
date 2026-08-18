/**
 * @file engine/events/command.c
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief RTSyn Engine command event dispatcher.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#include "rtsyn/internal/engine/events.h"

bool
rtsyn_engine_event_dispatch_command(rtsyn_engine_t *engine,
                                    const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message)
    {
        return false;
    }

    switch (message->type)
    {
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_NONE:
            return true;
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_GLOBAL_COMMAND:
            return rtsyn_engine_event_global_command(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_PLUGIN_UPDATE:
            return rtsyn_engine_event_plugin_update(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VALUES:
            return rtsyn_engine_event_request_port_values(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VARIABLES:
            return rtsyn_engine_event_request_port_variables(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE:
            return rtsyn_engine_event_load_node(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE:
            return rtsyn_engine_event_add_node(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_NODE:
            return rtsyn_engine_event_remove_node(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_CONNECTION:
            return rtsyn_engine_event_add_connection(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_CONNECTION:
            return rtsyn_engine_event_remove_connection(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_PARAM:
            return rtsyn_engine_event_set_param(engine, message);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PERIOD:
            return rtsyn_runtime_set_period(engine->config.runtime,
                                            message->data.set_runtime_period.period_ns);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PRIORITY:
            return rtsyn_engine_set_rt_thread_priority(
                engine, message->data.set_runtime_priority.priority);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_DEADLINE_TOLERANCE:
            return rtsyn_engine_set_deadline_tolerance(
                engine, message->data.set_runtime_deadline_tolerance.tolerance_ns);
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES:
            return rtsyn_engine_event_request_runtime_nodes(engine, message);
        default:
            return false;
    }
}
