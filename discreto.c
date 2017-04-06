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
typedef struct _plughandle_t plughandle_t;

struct _target_t {
	xpress_uuid_t uuid;
};

struct _state_t {
	int32_t position_order;
	int32_t velocity_order;
};

struct _plughandle_t {
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

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#discreto_position_order",
		.offset = offsetof(state_t, position_order),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#discreto_velocity_order",
		.offset = offsetof(state_t, velocity_order),
		.type = LV2_ATOM__Int,
	}
};


static void
_set(plughandle_t *handle, int64_t frames, const xpress_state_t *state,
	target_t *src)
{
	LV2_Atom_Forge *forge = &handle->forge;

	xpress_state_t new_state;
	memcpy(&new_state, state, sizeof(xpress_state_t));

	float x = state->pitch;

	switch(handle->state.position_order)
	{
		case 0:
		{
			const float ro = floor(x + 0.5f);
			x = ro;
			break;
		}

		case 1:
		{
			// do nothing, e.g. (x = x)
			break;
		}

		default:
		{
			const float v_abs = fabsf(state->dPitch);
			const float v2 = v_abs >= 1.f
				? 0.f
				: 1.f - v_abs;
			float k2;
			switch(handle->state.velocity_order)
			{
				case 0:
					k2 = handle->state.position_order;
					break;
				case 1:
					k2 = 1.f + (handle->state.position_order - 1.f) * v2;
					break;
				default:
					k2 = 1.f + (handle->state.position_order - 1.f) * powf(v2, handle->state.velocity_order);
					break;
			}
			const float ex = exp2f( (k2 - 1.f) / k2);

			const float ro = floor(x + 0.5f);
			float rel = x - ro;
			const float sign = rel < 0.f ? -1.f : 1.f;
			rel = powf(fabsf(rel)* ex, k2) * sign;
			x = ro + rel;
			break;
		}
	}

	new_state.pitch = x;

	if(handle->ref)
		handle->ref = xpress_put(&handle->xpress, forge, frames, src->uuid, &new_state);
}

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	target_t *src = target;

	src->uuid = xpress_map(&handle->xpress);

	_set(handle, frames, state, src);
}

static void
_put(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	target_t *src = target;

	_set(handle, frames, state, src);
}

static void
_del(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
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
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
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

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

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
	plughandle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, forge, 0, &handle->ref);

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
	plughandle_t *handle = instance;

	if(handle)
		free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_save(&handle->props, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_restore(&handle->props, retrieve, state, flags, features);
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

const LV2_Descriptor discreto = {
	.URI						= ESPRESSIVO_DISCRETO_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
