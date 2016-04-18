/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <espressivo.h>
#include <props.h>

#define MAX_NPROPS 2
#define MAX_NVOICES 64

typedef struct _target_t target_t;
typedef struct _state_t state_t;
typedef struct _handle_t handle_t;

struct _target_t {
	xpress_uuid_t uuid;
	bool below;
	float x;
};

struct _state_t {
	float position_threshold;
	float velocity_threshold;
};

struct _handle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpress, MAX_NVOICES);
	target_t target [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	state_t state;
	state_t stash;
};

static const props_def_t stat_reducto_position_threshold = {
	.property = ESPRESSIVO_URI"#reducto_position_threshold",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Float,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat_reducto_velocity_threshold = {
	.property = ESPRESSIVO_URI"#reducto_velocity_threshold",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Float,
	.mode = PROP_MODE_STATIC
};

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;

	LV2_Atom_Forge *forge = &handle->forge;

	src->uuid = xpress_map(&handle->xpress);
	src->below = true;
	src->x = state->position[0];

	if(handle->ref)
		handle->ref = xpress_put(&handle->xpress, forge, frames, src->uuid, state);
}

static void
_put(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;

	bool spawn_new = false;
	const float vel_x_abs = fabs(state->velocity[0]);

	if(src->below)
	{
		if(vel_x_abs >= handle->state.velocity_threshold)
			src->below = false;
	}
	else // !src->below
	{
		if(vel_x_abs < handle->state.velocity_threshold)
		{
			const float pos_x_diff_abs = fabs(state->position[0] - src->x);

			if(pos_x_diff_abs >= handle->state.position_threshold)
				spawn_new = true;
		}
	}

	if(spawn_new)
	{
		LV2_Atom_Forge *forge = &handle->forge;

		// delete previous event
		if(handle->ref)
			handle->ref = xpress_del(&handle->xpress, forge, frames, src->uuid);

		// create new event
		src->uuid = xpress_map(&handle->xpress);
		src->below = true;
		src->x = state->position[0];

		if(handle->ref)
			handle->ref = xpress_put(&handle->xpress, forge, frames, src->uuid, state);
	}
}

static void
_del(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;

	LV2_Atom_Forge *forge = &handle->forge;

	if(handle->ref)
		handle->ref = xpress_del(&handle->xpress, forge, frames, src->uuid);
}

static const xpress_iface_t iface = {
	.size = sizeof(target_t),

	.add = _add,
	.put = _put,
	.del = _del
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	xpress_map_t *voice_map = NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, XPRESS_VOICE_MAP))
			voice_map = features[i]->data;
	}

	if(!voice_map)
		voice_map = &voice_map_fallback;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	if(!xpress_init(&handle->xpress, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_ALL, &iface, handle->target, handle))
	{
		free(handle);
		return NULL;
	}

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	if(  !props_register(&handle->props, &stat_reducto_velocity_threshold,
			&handle->state.velocity_threshold, &handle->stash.velocity_threshold)
		|| !props_register(&handle->props, &stat_reducto_position_threshold,
			&handle->state.position_threshold, &handle->stash.position_threshold) )
	{
		free(handle);
		return NULL;
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(!props_advance(&handle->props, forge, frames, obj, &handle->ref))
		{
			xpress_advance(&handle->xpress, forge, frames, obj, &handle->ref);
		}
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = instance;

	if(handle)
		free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_save(&handle->props, &handle->forge, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_restore(&handle->props, &handle->forge, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	return NULL;
}

const LV2_Descriptor reducto = {
	.URI						= ESPRESSIVO_REDUCTO_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
