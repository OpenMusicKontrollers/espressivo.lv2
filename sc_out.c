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
#include <bsd/string.h>

#include <espressivo.h>
#include <lv2_osc.h>
#include <props.h>

#define SYNTH_NAMES 8
#define STRING_SIZE 256
#define MAX_NPROPS (SYNTH_NAMES + 8)

typedef struct _handle_t handle_t;

struct _handle_t {
	char synth_name [STRING_SIZE][SYNTH_NAMES];

	LV2_URID_Map *map;
	espressivo_forge_t cforge;
	osc_forge_t oforge;
	LV2_Atom_Forge forge;

	PROPS_T(props, MAX_NPROPS);

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *osc_out;

	int32_t out_offset;
	int32_t gid_offset;
	int32_t sid_offset;
	int32_t sid_wrap;
	int32_t arg_offset;
	int32_t allocate;
	int32_t gate;
	int32_t group;
};

static const props_def_t out_offset_def = {
	.property = ESPRESSIVO_URI"#sc_out_offset",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};
static const props_def_t gid_offset_def = {
	.property = ESPRESSIVO_URI"#sc_gid_offset",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};
static const props_def_t sid_offset_def = {
	.property = ESPRESSIVO_URI"#sc_sid_offset",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};
static const props_def_t sid_wrap_def = {
	.property = ESPRESSIVO_URI"#sc_sid_wrap",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};
static const props_def_t arg_offset_def = {
	.property = ESPRESSIVO_URI"#sc_arg_offset",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};
static const props_def_t allocate_def = {
	.property = ESPRESSIVO_URI"#sc_allocate",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Bool,
	.mode = PROP_MODE_STATIC
};
static const props_def_t gate_def = {
	.property = ESPRESSIVO_URI"#sc_gate",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Bool,
	.mode = PROP_MODE_STATIC
};
static const props_def_t group_def = {
	.property = ESPRESSIVO_URI"#sc_group",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Bool,
	.mode = PROP_MODE_STATIC
};

static const props_def_t synth_name_def [SYNTH_NAMES] = {
	[0] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_0",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[1] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[2] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[3] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[4] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[5] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[6] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[7] = {
		.property = ESPRESSIVO_URI"#sc_synth_name_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
};

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

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(int i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	osc_forge_init(&handle->oforge, handle->map);
	espressivo_forge_init(&handle->cforge, handle->map);
	lv2_atom_forge_init(&handle->forge, handle->map);

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	LV2_URID urid = 1;
	for(unsigned i=0; (i<SYNTH_NAMES) && urid; i++)
	{
		sprintf(handle->synth_name[i], "synth_%i", i);
		urid = props_register(&handle->props, &synth_name_def[i], PROP_EVENT_NONE, NULL, &handle->synth_name[i]);
	}
	if(urid
		&& props_register(&handle->props, &out_offset_def, PROP_EVENT_NONE, NULL, &handle->out_offset)
		&& props_register(&handle->props, &gid_offset_def, PROP_EVENT_NONE, NULL, &handle->gid_offset)
		&& props_register(&handle->props, &sid_offset_def, PROP_EVENT_NONE, NULL, &handle->sid_offset)
		&& props_register(&handle->props, &sid_wrap_def, PROP_EVENT_NONE, NULL, &handle->sid_wrap)
		&& props_register(&handle->props, &arg_offset_def, PROP_EVENT_NONE, NULL, &handle->arg_offset)
		&& props_register(&handle->props, &allocate_def, PROP_EVENT_NONE, NULL, &handle->allocate)
		&& props_register(&handle->props, &gate_def, PROP_EVENT_NONE, NULL, &handle->gate)
		&& props_register(&handle->props, &group_def, PROP_EVENT_NONE, NULL, &handle->group) )
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
	handle_t *handle = (handle_t *)instance;

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

static LV2_Atom_Forge_Ref
_osc_on(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const espressivo_event_t *cev)
{
	const int32_t sid = handle->sid_offset + cev->sid % handle->sid_wrap;
	const int32_t gid = handle->gid_offset + cev->gid;
	const int32_t out = handle->out_offset + cev->gid;
	const int32_t id = handle->group ? gid : sid;
	const int32_t arg_offset = handle->arg_offset;
	const int32_t arg_num = 4;

	LV2_Atom_Forge_Ref ref;

	if(handle->allocate)
	{
		ref = lv2_atom_forge_frame_time(forge, frames);
		if(handle->gate)
		{
			if(ref)
			{
				ref = osc_forge_message_vararg(&handle->oforge, forge,
					"/s_new", "siiiiisisi",
					handle->synth_name[cev->gid], id, 0, gid,
					arg_offset + 4, 128,
					"gate", 1,
					"out", out);
				(void)ref;
			}
		}
		else // !handle->gate
		{
			if(ref)
			{
				ref = osc_forge_message_vararg(&handle->oforge, forge,
					"/s_new", "siiiiisi",
					handle->synth_name[cev->gid], id, 0, gid,
					arg_offset + 4, 128,
					"out", out);
				(void)ref;
			}
		}
	}
	else if(handle->gate)
	{
		ref = lv2_atom_forge_frame_time(forge, frames);
		if(ref)
		{
			ref = osc_forge_message_vararg(&handle->oforge, forge,
				"/n_set", "isi",
				id,
				"gate", 1);
			(void)ref;
		}
	}

	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
	{
		ref = osc_forge_message_vararg(&handle->oforge, forge,
			"/n_setn", "iiiffff",
			id, arg_offset, arg_num,
			cev->dim[0], cev->dim[1], cev->dim[2], cev->dim[3]);
	}

	return ref;
}

static LV2_Atom_Forge_Ref
_osc_off(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const espressivo_event_t *cev)
{
	const int32_t sid = handle->sid_offset + cev->sid % handle->sid_wrap;
	const int32_t gid = handle->gid_offset + cev->gid;
	const int32_t id = handle->group ? gid : sid;

	LV2_Atom_Forge_Ref ref = 1;

	if(handle->gate)
	{
		ref = lv2_atom_forge_frame_time(forge, frames);
		if(ref)
		{
			ref = osc_forge_message_vararg(&handle->oforge, forge,
				"/n_set", "isi",
				id,
				"gate", 0);
		}
	}

	return ref;
}

static LV2_Atom_Forge_Ref
_osc_set(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const espressivo_event_t *cev)
{
	const int32_t sid = handle->sid_offset + cev->sid % handle->sid_wrap;
	const int32_t gid = handle->gid_offset + cev->gid;
	const int32_t id = handle->group ? gid : sid;
	const int32_t arg_offset = handle->arg_offset;
	const int32_t arg_num = 4;

	LV2_Atom_Forge_Ref ref;

	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
	{
		ref = osc_forge_message_vararg(&handle->oforge, forge,
			"/n_setn", "iiiffff",
			id, arg_offset, arg_num,
			cev->dim[0], cev->dim[1], cev->dim[2], cev->dim[3]);
	}

	return ref;
}

static LV2_Atom_Forge_Ref
_osc_idle(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const espressivo_event_t *cev)
{
	return 1;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;
	
	// prepare osc atom forge
	const uint32_t capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref;
	ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		int64_t frames = ev->time.frames;

		if(  !props_advance(&handle->props, forge, frames, obj, &ref)
			&& espressivo_event_check_type(&handle->cforge, &obj->atom) && ref)
		{
			espressivo_event_t cev;
			espressivo_event_deforge(&handle->cforge, &obj->atom, &cev);

			switch(cev.state)
			{
				case ESPRESSIVO_STATE_ON:
					ref = _osc_on(handle, forge, frames, &cev);
					break;
				case ESPRESSIVO_STATE_SET:
					ref = _osc_set(handle, forge, frames, &cev);
					break;
				case ESPRESSIVO_STATE_OFF:
					ref = _osc_off(handle, forge, frames, &cev);
					break;
				case ESPRESSIVO_STATE_IDLE:
					ref = _osc_idle(handle, forge, frames, &cev);
					break;
			}
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->osc_out);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

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
