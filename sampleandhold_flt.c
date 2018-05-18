/*
 * Copyright (c) 2015-2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#define MAX_NPROPS 7

typedef struct _targetI_t targetI_t;
typedef struct _targetO_t targetO_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _targetI_t {
	xpress_uuid_t uuid;
};

struct _targetO_t {
	bool on_hold;
	xpress_state_t state;
};

struct _plugstate_t {
	int32_t sample;
	int32_t hold_pitch;
	int32_t hold_pressure;
	int32_t hold_timbre;
	int32_t hold_dPitch;
	int32_t hold_dPressure;
	int32_t hold_dTimbre;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpressI, MAX_NVOICES);
	targetI_t targetI [MAX_NVOICES];
	XPRESS_T(xpressO, MAX_NVOICES);
	targetO_t targetO [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	
	bool clone;

	plugstate_t state;
	plugstate_t stash;
};

static void
_intercept_sample(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	if(!handle->state.sample)
	{
		// release all events on hold
		unsigned freed = 0;

		XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
		{
			targetO_t *dst = voice->target;

			if(!dst->on_hold)
				continue; // still playing

			voice->uuid = 0; // mark for removal
			freed += 1;
		}

		if(freed > 0)
		{
			_xpress_sort(&handle->xpressO);
			handle->xpressO.nvoices -= freed;
		}

		if(handle->ref)
			handle->ref = xpress_alive(&handle->xpressO, &handle->forge, frames);
	}
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#snh_sample",
		.offset = offsetof(plugstate_t, sample),
		.type = LV2_ATOM__Bool,
		.event_cb = _intercept_sample
	},
	{
		.property = ESPRESSIVO_URI"#snh_hold_pitch",
		.offset = offsetof(plugstate_t, hold_pitch),
		.type = LV2_ATOM__Bool,
	},
	{
		.property = ESPRESSIVO_URI"#snh_hold_pressure",
		.offset = offsetof(plugstate_t, hold_pressure),
		.type = LV2_ATOM__Bool,
	},
	{
		.property = ESPRESSIVO_URI"#snh_hold_timbre",
		.offset = offsetof(plugstate_t, hold_timbre),
		.type = LV2_ATOM__Bool,
	},
	{
		.property = ESPRESSIVO_URI"#snh_hold_dPitch",
		.offset = offsetof(plugstate_t, hold_dPitch),
		.type = LV2_ATOM__Bool,
	},
	{
		.property = ESPRESSIVO_URI"#snh_hold_dPressure",
		.offset = offsetof(plugstate_t, hold_dPressure),
		.type = LV2_ATOM__Bool,
	},
	{
		.property = ESPRESSIVO_URI"#snh_hold_dTimbre",
		.offset = offsetof(plugstate_t, hold_dTimbre),
		.type = LV2_ATOM__Bool,
	}
};

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;
	targetO_t *dst;

	if((dst = xpress_create(&handle->xpressO, &src->uuid)))
	{
		dst->on_hold = false;
		dst->state = *state;

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, &handle->forge, frames, src->uuid, &dst->state);
	}
}

static void
_set(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;
	targetO_t *dst;
	
	if((dst = xpress_get(&handle->xpressO, src->uuid)))
	{
		if(!(handle->state.hold_pitch && (state->pitch < dst->state.pitch) ))
			dst->state.pitch = state->pitch;

		if(!(handle->state.hold_pressure && (state->pressure < dst->state.pressure) ))
			dst->state.pressure = state->pressure;

		if(!(handle->state.hold_timbre && (state->timbre < dst->state.timbre) ))
			dst->state.timbre = state->timbre;

		if(!(handle->state.hold_dPitch && (state->dPitch < dst->state.dPitch) ))
			dst->state.dPitch = state->dPitch;

		if(!(handle->state.hold_dPressure && (state->dPressure < dst->state.dPressure) ))
			dst->state.dPressure = state->dPressure;

		if(!(handle->state.hold_dTimbre && (state->dTimbre < dst->state.dTimbre) ))
			dst->state.dTimbre = state->dTimbre;

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, &handle->forge, frames, src->uuid, &dst->state);
	}
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;
	targetO_t *dst;

	if(handle->state.sample)
	{
		if((dst = xpress_get(&handle->xpressO, src->uuid)))
			dst->on_hold = true;
		return;
	}

	xpress_free(&handle->xpressO, src->uuid);

	if(handle->ref)
		handle->ref = xpress_alive(&handle->xpressO, &handle->forge, frames);
}

static const xpress_iface_t ifaceI = {
	.size = sizeof(targetI_t),

	.add = _add,
	.set = _set,
	.del = _del
};

static const xpress_iface_t ifaceO = {
	.size = sizeof(targetO_t)
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
		else if(!strcmp(features[i]->URI, XPRESS__voiceMap))
			voice_map = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	if(  !xpress_init(&handle->xpressI, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_ALL, &ifaceI, handle->targetI, handle)
		|| !xpress_init(&handle->xpressO, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_NONE, &ifaceO, handle->targetO, handle) )
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
	xpress_pre(&handle->xpressI);
	xpress_rst(&handle->xpressO);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(!props_advance(&handle->props, forge, frames, obj, &handle->ref))
		{
			xpress_advance(&handle->xpressI, forge, frames, obj, &handle->ref);
		}
	}

	xpress_post(&handle->xpressI, nsamples-1);
	if(handle->ref && !xpress_synced(&handle->xpressO))
		handle->ref = xpress_alive(&handle->xpressO, forge, nsamples-1);

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
