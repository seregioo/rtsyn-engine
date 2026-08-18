/**
 * @file engine/events/global_command.c
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief RTSyn Engine global command event.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#include <stdatomic.h>

#include "rtsyn/internal/engine/events.h"

bool
rtsyn_engine_event_global_command(rtsyn_engine_t *engine,
                                  const rtsyn_spsc_command_message_t *message)
{
    if (!engine || !message || message->type != RTSYN_SPSC_COMMAND_MESSAGE_TYPE_GLOBAL_COMMAND)
    {
        return false;
    }

    switch ((rtsyn_engine_global_command_t)message->data.global_command.command)
    {
        case RTSYN_ENGINE_GLOBAL_COMMAND_NONE:
            return true;
        case RTSYN_ENGINE_GLOBAL_COMMAND_STOP:
            atomic_store_explicit(&engine->stop_requested, true, memory_order_release);
            return true;
        case RTSYN_ENGINE_GLOBAL_COMMAND_PAUSE:
            atomic_store_explicit(&engine->paused, true, memory_order_release);
            return true;
        case RTSYN_ENGINE_GLOBAL_COMMAND_RESUME:
            atomic_store_explicit(&engine->paused, false, memory_order_release);
            return true;
        default:
            return false;
    }
}
