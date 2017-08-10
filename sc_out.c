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

#include <espressivo.h>
#include <osc.lv2/forge.h>
#include <props.h>

#define SYNTH_NAMES 8
#define STRING_SIZE 256
#define MAX_NPROPS (SYNTH_NAMES + 8)

typedef struct _targetI_t targetI_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _targetI_t {
	int32_t sid;
	int32_t zone;
};

struct _plugstate_t {
	char synth_name [SYNTH_NAMES][STRING_SIZE];
	int32_t out_offset;
	int32_t gid_offset;
	int32_t sid_offset;
	int32_t sid_wrap;
	int32_t arg_offset;
	int32_t allocate;
	int32_t gate;
	int32_t group;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_OSC_URID osc_urid;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpressI, MAX_NVOICES);
	targetI_t targetI [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *osc_out;

	int32_t sid;

	plugstate_t state;
	plugstate_t stash;
};

#define SYNTH_NAME(NUM) \
{ \
	.property = ESPRESSIVO_URI"#sc_synth_name_"#NUM, \
	.offset = offsetof(plugstate_t, synth_name) + (NUM-1)*STRING_SIZE, \
	.type = LV2_ATOM__String, \
	.max_size = STRING_SIZE \
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#sc_out_offset",
		.offset = offsetof(plugstate_t, out_offset),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#sc_gid_offset",
		.offset = offsetof(plugstate_t, gid_offset),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#sc_sid_offset",
		.offset = offsetof(plugstate_t, sid_offset),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#sc_sid_wrap",
		.offset = offsetof(plugstate_t, sid_wrap),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#sc_arg_offset",
		.offset = offsetof(plugstate_t, arg_offset),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#sc_allocate",
		.offset = offsetof(plugstate_t, allocate),
		.type = LV2_ATOM__Bool,
	},
	{
		.property = ESPRESSIVO_URI"#sc_gate",
		.offset = offsetof(plugstate_t, gate),
		.type = LV2_ATOM__Bool,
	},
	{
		.property = ESPRESSIVO_URI"#sc_group",
		.offset = offsetof(plugstate_t, group),
		.type = LV2_ATOM__Bool,
	},

	SYNTH_NAME(1),
	SYNTH_NAME(2),
	SYNTH_NAME(3),
	SYNTH_NAME(4),
	SYNTH_NAME(5),
	SYNTH_NAME(6),
	SYNTH_NAME(7),
	SYNTH_NAME(8)
};

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

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	targetI_t *src = target;

	const int32_t sid = handle->state.sid_offset + (handle->state.sid_wrap
		? handle->sid++ % handle->state.sid_wrap
		: handle->sid++);
	src->sid = sid;
	src->zone = state->zone;
	const int32_t gid = handle->state.gid_offset + state->zone;
	const int32_t out = handle->state.out_offset + state->zone;
	const int32_t id = handle->state.group ? gid : sid;
	const int32_t arg_num = 4;

	if(handle->state.allocate)
	{
		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(forge, frames);
		if(handle->state.gate)
		{
			if(handle->ref)
				handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid,
					"/s_new", "siiiiisisi",
					handle->state.synth_name[state->zone], id, 0, gid,
					handle->state.arg_offset + 4, 128,
					"gate", 1,
					"out", out);
		}
		else // !handle->state.gate
		{
			if(handle->ref)
				handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid,
					"/s_new", "siiiiisi",
					handle->state.synth_name[state->zone], id, 0, gid,
					handle->state.arg_offset + 4, 128,
					"out", out);
		}
	}
	else if(handle->state.gate)
	{
		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(forge, frames);
		if(handle->ref)
			handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid,
				"/n_set", "isi",
				id,
				"gate", 1);
	}

	if(handle->ref)
		handle->ref = lv2_atom_forge_frame_time(forge, frames);
	if(handle->ref)
		handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid,
			"/n_setn", "iiiffff",
			id, handle->state.arg_offset, arg_num,
			_midi2cps(state->pitch), state->pressure,
			state->dPitch, state->dPressure);
}

static void
_set(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	targetI_t *src = target;

	const int32_t sid = src->sid;
	const int32_t gid = handle->state.gid_offset + state->zone;
	const int32_t id = handle->state.group ? gid : sid;
	const int32_t arg_num = 4;

	if(handle->ref)
		handle->ref = lv2_atom_forge_frame_time(forge, frames);
	if(handle->ref)
		handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid,
			"/n_setn", "iiiffff",
			id, handle->state.arg_offset, arg_num,
			_midi2cps(state->pitch), state->pressure,
			state->dPitch, state->dPressure);
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	targetI_t *src = target;

	const int32_t sid = src->sid;
	const int32_t gid = handle->state.gid_offset + src->zone;
	const int32_t id = handle->state.group ? gid : sid;

	if(handle->state.gate)
	{
		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(forge, frames);
		if(handle->ref)
			handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid,
				"/n_set", "isi",
				id,
				"gate", 0);
	}
}

static const xpress_iface_t ifaceI = {
	.size = sizeof(targetI_t),

	.add = _add,
	.set = _set,
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

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, XPRESS__voiceMap))
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
	lv2_osc_urid_init(&handle->osc_urid, handle->map);

	if(!xpress_init(&handle->xpressI, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_ALL, &ifaceI, handle->targetI, handle) )
	{
		free(handle);
		return NULL;
	}

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->osc_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;
	
	// prepare osc atom forge
	const uint32_t capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, forge, 0, &handle->ref);
	xpress_pre(&handle->xpressI);

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

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->osc_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	if(handle)
		free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;

	return NULL;
}

const LV2_Descriptor sc_out = {
	.URI						= ESPRESSIVO_SC_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
