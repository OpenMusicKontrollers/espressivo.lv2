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

typedef struct _ref_t ref_t;
typedef struct _handle_t handle_t;

struct _ref_t {
	uint32_t gid;
	float dim[4];
};

struct _handle_t {
	LV2_URID_Map *map;
	espressivo_forge_t cforge;
	LV2_Atom_Forge_Ref ref2;

	espressivo_dict_t dict [ESPRESSIVO_DICT_SIZE];
	ref_t ref [ESPRESSIVO_DICT_SIZE];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	
	int32_t sample;
	int32_t hold_dimension [4];
	bool clone;
	props_t *props;
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

	if(!handle->sample) // was disabled
	{
		uint32_t sid;
		ref_t *ref;
		ESPRESSIVO_DICT_FOREACH(handle->dict, sid, ref)
		{
			const espressivo_event_t cev = {
				.state = ESPRESSIVO_STATE_OFF,
				.sid = sid,
				.gid = ref->gid,
				.dim[0] = ref->dim[0],
				.dim[1] = ref->dim[1],
				.dim[2] = ref->dim[2],
				.dim[3] = ref->dim[3]
			};

			if(handle->ref2)
				handle->ref2 = lv2_atom_forge_frame_time(forge, frames);
			if(handle->ref2)
				handle->ref2 = espressivo_event_forge(&handle->cforge, &cev);
		}

		espressivo_dict_clear(handle->dict);
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	espressivo_forge_init(&handle->cforge, handle->map);
	ESPRESSIVO_DICT_INIT(handle->dict, handle->ref);

	handle->props = props_new(5, descriptor->URI, handle->map, handle);
	if(!handle->props)
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	if(props_register(handle->props, &stat_snh_sample, PROP_EVENT_WRITE, _intercept_sample, &handle->sample)
		&& props_register(handle->props, &stat_snh_hold_dimension[0], PROP_EVENT_NONE, NULL, &handle->hold_dimension[0])
		&& props_register(handle->props, &stat_snh_hold_dimension[1], PROP_EVENT_NONE, NULL, &handle->hold_dimension[1])
		&& props_register(handle->props, &stat_snh_hold_dimension[2], PROP_EVENT_NONE, NULL, &handle->hold_dimension[2])
		&& props_register(handle->props, &stat_snh_hold_dimension[3], PROP_EVENT_NONE, NULL, &handle->hold_dimension[3]) )
	{
		props_sort(handle->props);
	}
	else
	{
		props_free(handle->props);
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

static inline void
_fltr_on(handle_t *handle, int64_t frames, const espressivo_event_t *cev)
{
	ref_t *ref = espressivo_dict_add(handle->dict, cev->sid);
	if(!ref)
		return;

	// save initial values
	ref->gid = cev->gid;
	ref->dim[0] = cev->dim[0];
	ref->dim[1] = cev->dim[1];
	ref->dim[2] = cev->dim[2];
	ref->dim[3] = cev->dim[3];
}

static inline void
_fltr_off(handle_t *handle, int64_t frames, const espressivo_event_t *cev)
{
	if(!handle->sample) // is disabled
	{
		ref_t *ref = espressivo_dict_del(handle->dict, cev->sid);
		if(!ref)
			return;
	}
	else // is enabled
	{
		ref_t *ref = espressivo_dict_ref(handle->dict, cev->sid);
		if(!ref)
			return;

		// save last values
		ref->gid = cev->gid;
		ref->dim[0] = cev->dim[0];
		ref->dim[1] = cev->dim[1];
		ref->dim[2] = cev->dim[2];
		ref->dim[3] = cev->dim[3];

		handle->clone = false; // block this event
	}
}

static inline void
_fltr_set(handle_t *handle, int64_t frames, espressivo_event_t *cev)
{
	ref_t *ref = espressivo_dict_ref(handle->dict, cev->sid);
	if(!ref)
		return;

	if(!handle->sample) // is disabled
	{
		ref->gid = cev->gid;
		ref->dim[0] = cev->dim[0];
		ref->dim[1] = cev->dim[1];
		ref->dim[2] = cev->dim[2];
		ref->dim[3] = cev->dim[3];
	}
	else // is enabled
	{
		// limit changes to one direction only, aka hold maximum
		for(unsigned i=0; i<4; i++)
		{
			if(handle->hold_dimension[i]) // query hold enable state
			{
				if(cev->dim[i] > ref->dim[i])
					ref->dim[i] = cev->dim[i]; // store new held maximum
				else
					cev->dim[i] = ref->dim[i]; // overwrite with held maximum
			}
		}
	}
}

static inline void
_fltr_idle(handle_t *handle, int64_t frames, const espressivo_event_t *cev)
{
	if(!handle->sample) // is disabled
		espressivo_dict_clear(handle->dict);
	else
		handle->clone = false; // block this event
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref2 = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(!handle->ref2)
			break;

		const int64_t frames = ev->time.frames;

		if(espressivo_event_check_type(&handle->cforge, &ev->body))
		{
			espressivo_event_t cev;

			espressivo_event_deforge(&handle->cforge, &ev->body, &cev);

			handle->clone = true;

			switch(cev.state)
			{
				case ESPRESSIVO_STATE_ON:
					_fltr_on(handle, frames, &cev);
					break;
				case ESPRESSIVO_STATE_OFF:
					_fltr_off(handle, frames, &cev);
					break;
				case ESPRESSIVO_STATE_SET:
					_fltr_set(handle, frames, &cev);
					break;
				case ESPRESSIVO_STATE_IDLE:
					_fltr_idle(handle, frames, &cev);
					break;
				default:
					break;
			}

			// clone event
			if(handle->clone)
			{
				if(handle->ref2)
					handle->ref2 = lv2_atom_forge_frame_time(forge, frames);
				if(handle->ref2)
					handle->ref2 = espressivo_event_forge(&handle->cforge, &cev);
			}
		}
		else
			props_advance(handle->props, forge, frames, (const LV2_Atom_Object *)&ev->body, &handle->ref2);
	}

	if(handle->ref2)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = instance;

	if(handle->props)
		props_free(handle->props);
	free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_save(handle->props, &handle->cforge.forge, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_restore(handle->props, &handle->cforge.forge, retrieve, state, flags, features);
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
