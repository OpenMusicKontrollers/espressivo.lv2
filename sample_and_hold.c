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

#define MAX_NPROPS 5
#define MAX_NVOICES 64

typedef struct _target_t target_t;
typedef struct _target2_t target2_t;
typedef struct _handle_t handle_t;

struct _target_t {
	xpress_uuid_t uuid;
};

struct _target2_t {
	bool on_hold;
	xpress_state_t state;
};

struct _handle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpress, MAX_NVOICES);
	target_t target [MAX_NVOICES];
	XPRESS_T(xpress2, MAX_NVOICES);
	target2_t target2 [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	
	int32_t sample;
	int32_t hold_dimension [4];
	bool clone;
};

static const props_def_t stat_snh_sample = {
	.property = ESPRESSIVO_URI"#snh_sample",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Bool,
	.mode = PROP_MODE_STATIC
};
static const props_def_t stat_snh_hold_dimension [4] = {
	[0] = {
		.property = ESPRESSIVO_URI"#snh_hold_dimension_0",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[1] = {
		.property = ESPRESSIVO_URI"#snh_hold_dimension_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[2] = {
		.property = ESPRESSIVO_URI"#snh_hold_dimension_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[3] = {
		.property = ESPRESSIVO_URI"#snh_hold_dimension_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
};

static void
_intercept_sample(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	handle_t *handle = data;

	if(!handle->sample)
	{
		// release all events on hold
		unsigned removed = 0;
		XPRESS_VOICE_FOREACH(&handle->xpress2, voice)
		{
			target2_t *dst = voice->target;

			if(!dst->on_hold)
				continue; // still playing

			if(handle->ref)
				handle->ref = xpress_del(&handle->xpress2, forge, frames, voice->uuid);

			voice->uuid = 0; // mark for removal
			removed++;
		}
		_xpress_sort(&handle->xpress2);
		handle->xpress2.nvoices -= removed;
	}
}

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;
	target2_t *dst;

	if((dst = xpress_create(&handle->xpress2, &src->uuid)))
	{
		dst->on_hold = false;
		memcpy(&dst->state, state, sizeof(xpress_state_t));

		if(handle->ref)
			handle->ref = xpress_put(&handle->xpress2, &handle->forge, frames, src->uuid, &dst->state);
	}
}

static void
_put(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;
	target2_t *dst;
	
	if((dst = xpress_get(&handle->xpress2, src->uuid)))
	{
		for(unsigned i=0; i<3; i++)
			if(!(handle->hold_dimension[i] && (state->position[i] < dst->state.position[i]) ))
				dst->state.position[i] = state->position[i];

		if(handle->ref)
			handle->ref = xpress_put(&handle->xpress2, &handle->forge, frames, src->uuid, &dst->state);
	}
}

static void
_del(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;
	target2_t *dst;

	if(handle->sample)
	{
		if((dst = xpress_get(&handle->xpress2, src->uuid)))
			dst->on_hold = true;
		return;
	}

	if(handle->ref)
		handle->ref = xpress_del(&handle->xpress2, &handle->forge, frames, src->uuid);

	xpress_free(&handle->xpress2, src->uuid);
}

static const xpress_iface_t iface = {
	.size = sizeof(target_t),

	.add = _add,
	.put = _put,
	.del = _del
};

static const xpress_iface_t iface2 = {
	.size = sizeof(target2_t)
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

	if(  !xpress_init(&handle->xpress, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_ALL, &iface, handle->target, handle)
		|| !xpress_init(&handle->xpress2, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_NONE, &iface2, handle->target2, NULL) )
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

	if(props_register(&handle->props, &stat_snh_sample, PROP_EVENT_WRITE, _intercept_sample, &handle->sample)
		&& props_register(&handle->props, &stat_snh_hold_dimension[0], PROP_EVENT_NONE, NULL, &handle->hold_dimension[0])
		&& props_register(&handle->props, &stat_snh_hold_dimension[1], PROP_EVENT_NONE, NULL, &handle->hold_dimension[1])
		&& props_register(&handle->props, &stat_snh_hold_dimension[2], PROP_EVENT_NONE, NULL, &handle->hold_dimension[2])
		&& props_register(&handle->props, &stat_snh_hold_dimension[3], PROP_EVENT_NONE, NULL, &handle->hold_dimension[3]) )
	{
		props_sort(&handle->props);
	}
	else
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

const LV2_Descriptor snh = {
	.URI						= ESPRESSIVO_SNH_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
