/*
 * effect.c
 * Copyright 2010-2012 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <glib.h>
#include <pthread.h>
#include <string.h>

#include "debug.h"
#include "drct.h"
#include "effect.h"
#include "misc.h"
#include "plugin.h"
#include "plugins.h"

typedef struct {
    PluginHandle * plugin;
    EffectPlugin * header;
    int channels_returned, rate_returned;
    bool_t remove_flag;
} RunningEffect;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static GList * running_effects = NULL; /* (RunningEffect *) */
static int input_channels, input_rate;
static float * prev_data;

typedef struct {
    int * channels, * rate;
} EffectStartState;

static bool_t effect_start_cb (PluginHandle * plugin, EffectStartState * state)
{
    AUDDBG ("Starting %s at %d channels, %d Hz.\n", plugin_get_name (plugin),
     * state->channels, * state->rate);
    EffectPlugin * header = plugin_get_header (plugin);
    g_return_val_if_fail (header != NULL, TRUE);
    header->start (state->channels, state->rate);

    RunningEffect * effect = g_slice_new (RunningEffect);
    effect->plugin = plugin;
    effect->header = header;
    effect->channels_returned = * state->channels;
    effect->rate_returned = * state->rate;
    effect->remove_flag = FALSE;

    running_effects = g_list_prepend (running_effects, effect);
    return TRUE;
}

void effect_start (int * channels, int * rate)
{
    pthread_mutex_lock (& mutex);

    AUDDBG ("Starting effects.\n");

    for (GList * node = running_effects; node; node = node->next)
        g_slice_free (RunningEffect, node->data);

    g_list_free (running_effects);
    running_effects = NULL;

    input_channels = * channels;
    input_rate = * rate;

    EffectStartState state = {channels, rate};
    plugin_for_enabled (PLUGIN_TYPE_EFFECT, (PluginForEachFunc) effect_start_cb,
     & state);
    running_effects = g_list_reverse (running_effects);

    pthread_mutex_unlock (& mutex);
}

typedef struct {
    float * * data;
    int * samples;
} EffectProcessState;

static void effect_process_cb (RunningEffect * effect, EffectProcessState *
 state)
{
    if (effect->remove_flag)
    {
        effect->header->finish (state->data, state->samples);

        // simulate end-of-playlist call
        // first save the current data
        void * save_data = g_memdup (* state->data, * state->samples * sizeof (float));
        int save_samples = * state->samples;

        // now free the previous saved data (after the memdup)
        g_free (prev_data);
        prev_data = save_data;

        float * new_data = NULL;
        int new_samples = 0;

        effect->header->finish (& new_data, & new_samples);

        // combine the saved and new data
        save_data = g_realloc (save_data, (save_samples + new_samples) * sizeof (float));
        memcpy (save_data + save_samples, new_data, new_samples * sizeof (float));
        save_samples += new_samples;

        * state->data = save_data;
        * state->samples = save_samples;

        running_effects = g_list_remove (running_effects, effect);
        g_slice_free (RunningEffect, effect);
    }
    else
        effect->header->process (state->data, state->samples);
}

void effect_process (float * * data, int * samples)
{
    pthread_mutex_lock (& mutex);

    g_free (prev_data);
    prev_data = NULL;

    EffectProcessState state = {data, samples};
    g_list_foreach (running_effects, (GFunc) effect_process_cb, & state);

    pthread_mutex_unlock (& mutex);
}

void effect_flush (void)
{
    pthread_mutex_lock (& mutex);

    for (GList * node = running_effects; node != NULL; node = node->next)
    {
        if (PLUGIN_HAS_FUNC (((RunningEffect *) node->data)->header, flush))
            ((RunningEffect *) node->data)->header->flush ();
    }

    pthread_mutex_unlock (& mutex);
}

void effect_finish (float * * data, int * samples)
{
    pthread_mutex_lock (& mutex);

    g_free (prev_data);
    prev_data = NULL;

    for (GList * node = running_effects; node != NULL; node = node->next)
        ((RunningEffect *) node->data)->header->finish (data, samples);

    pthread_mutex_unlock (& mutex);
}

int effect_adjust_delay (int delay)
{
    pthread_mutex_lock (& mutex);

    for (GList * node = g_list_last (running_effects); node != NULL; node = node->prev)
    {
        if (PLUGIN_HAS_FUNC (((RunningEffect *) node->data)->header, adjust_delay))
            delay = ((RunningEffect *) node->data)->header->adjust_delay (delay);
    }

    pthread_mutex_unlock (& mutex);
    return delay;
}

static int effect_find_cb (RunningEffect * effect, PluginHandle * plugin)
{
    return (effect->plugin == plugin) ? 0 : -1;
}

static int effect_compare (RunningEffect * a, RunningEffect * b)
{
    return plugin_compare (a->plugin, b->plugin);
}

static void effect_insert (PluginHandle * plugin, EffectPlugin * header)
{
    GList * node = g_list_find_custom (running_effects, plugin, (GCompareFunc) effect_find_cb);

    if (node)
    {
        ((RunningEffect *) node->data)->remove_flag = FALSE;
        return;
    }

    AUDDBG ("Adding %s without reset.\n", plugin_get_name (plugin));
    RunningEffect * effect = g_slice_new (RunningEffect);
    effect->plugin = plugin;
    effect->header = header;
    effect->remove_flag = FALSE;

    running_effects = g_list_insert_sorted (running_effects, effect,
     (GCompareFunc) effect_compare);
    node = g_list_find (running_effects, effect);

    int channels, rate;
    if (node->prev != NULL)
    {
        RunningEffect * prev = node->prev->data;
        AUDDBG ("Added %s after %s.\n", plugin_get_name (plugin),
         plugin_get_name (prev->plugin));
        channels = prev->channels_returned;
        rate = prev->rate_returned;
    }
    else
    {
        AUDDBG ("Added %s as first effect.\n", plugin_get_name (plugin));
        channels = input_channels;
        rate = input_rate;
    }

    AUDDBG ("Starting %s at %d channels, %d Hz.\n", plugin_get_name (plugin),
     channels, rate);
    header->start (& channels, & rate);
    effect->channels_returned = channels;
    effect->rate_returned = rate;
}

static void effect_remove (PluginHandle * plugin)
{
    GList * node = g_list_find_custom (running_effects, plugin, (GCompareFunc)
     effect_find_cb);
    if (node == NULL)
        return;

    AUDDBG ("Removing %s without reset.\n", plugin_get_name (plugin));
    ((RunningEffect *) node->data)->remove_flag = TRUE;
}

static void effect_enable (PluginHandle * plugin, EffectPlugin * ep, bool_t
 enable)
{
    if (ep->preserves_format)
    {
        pthread_mutex_lock (& mutex);

        if (enable)
            effect_insert (plugin, ep);
        else
            effect_remove (plugin);

        pthread_mutex_unlock (& mutex);
    }
    else
    {
        AUDDBG ("Reset to add/remove %s.\n", plugin_get_name (plugin));
        output_reset (OUTPUT_RESET_EFFECTS_ONLY);
    }
}

bool_t effect_plugin_start (PluginHandle * plugin)
{
    if (drct_get_playing ())
    {
        EffectPlugin * ep = plugin_get_header (plugin);
        g_return_val_if_fail (ep != NULL, FALSE);
        effect_enable (plugin, ep, TRUE);
    }

    return TRUE;
}

void effect_plugin_stop (PluginHandle * plugin)
{
    if (drct_get_playing ())
    {
        EffectPlugin * ep = plugin_get_header (plugin);
        g_return_if_fail (ep != NULL);
        effect_enable (plugin, ep, FALSE);
    }
}
