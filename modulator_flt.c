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

#define MAX_NPROPS 9

typedef enum _enum_t enum_t;
typedef enum _op_t op_t;
typedef struct _targetI_t targetI_t;
typedef struct _targetO_t targetO_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

enum _enum_t {
	ENUM_PITCH = 0,
	ENUM_PRESSURE,
	ENUM_TIMBRE,
	ENUM_DPITCH,
	ENUM_DPRESSURE,
	ENUM_DTIMBRE
};

enum _op_t {
	OP_ADD = 0,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_POW,
	OP_SET
};

struct _targetI_t {
	xpress_uuid_t uuid;
	int32_t zone_mask;
};

struct _targetO_t {
	xpress_state_t state;
};

struct _plugstate_t {
	int32_t zone_mask_src;
	int32_t zone_mask_mod;;
	int32_t zone_offset;;
	int32_t enum_src;
	int32_t enum_mod;
	float multiplier;
	float adder;
	int32_t op;
	int32_t reset;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpressI, MAX_NVOICES);
	XPRESS_T(xpressO, MAX_NVOICES);
	targetI_t targetI [MAX_NVOICES];
	targetO_t targetO [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	plugstate_t state;
	plugstate_t stash;

	xpress_uuid_t uuid;
	xpress_state_t modu;
};

static const xpress_state_t empty_state = {
	.zone = 0,

	.pitch = 0.f,
	.pressure = 0.f,
	.timbre = 0.f,

	.dPitch = 0.f,
	.dPressure = 0.f,
	.dTimbre = 0.f
};

static inline float
_clip(float min, float val, float max)
{
	if(val < min)
		return min;
	else if(val > max)
		return max;
	return val;
}

static inline float
_op(plughandle_t *handle, float dst, float val)
{
	switch((op_t)handle->state.op)
	{
		case OP_ADD:
			return dst + val;
		case OP_SUB:
			return dst - val;
		case OP_MUL:
			return dst * val;
		case OP_DIV:
			return (val == 0.f) ? 0.f : dst / val;
		case OP_POW:
			return powf(dst, val);
		case OP_SET:
			return val;
	}

	return 0.f;
}

static inline void
_modulate(plughandle_t *handle, xpress_state_t *state)
{
	float val = 0.f;

	switch((enum_t)handle->state.enum_mod)
	{
		case ENUM_PITCH:
		{
			val = handle->modu.pitch;
		}	break;
		case ENUM_PRESSURE:
		{
			val = handle->modu.pressure;
		}	break;
		case ENUM_TIMBRE:
		{
			val = handle->modu.timbre;
		}	break;
		case ENUM_DPITCH:
		{
			val = handle->modu.dPitch;
		}	break;
		case ENUM_DPRESSURE:
		{
			val = handle->modu.dPressure;
		}	break;
		case ENUM_DTIMBRE:
		{
			val = handle->modu.dTimbre;
		}	break;
	}

	val *= handle->state.multiplier;
	val += handle->state.adder;

	switch((enum_t)handle->state.enum_src)
	{
		case ENUM_PITCH:
		{
			state->pitch = _op(handle, state->pitch, val);
			state->pitch = _clip(0.f, state->pitch, 1.f);
		}	break;
		case ENUM_PRESSURE:
		{
			state->pressure = _op(handle, state->pressure, val);
			state->pressure = _clip(0.f, state->pressure, 1.f);
		}	break;
		case ENUM_TIMBRE:
		{
			state->timbre = _op(handle, state->timbre, val);
			state->timbre = _clip(0.f, state->timbre, 1.f);
		}	break;
		case ENUM_DPITCH:
		{
			state->dPitch = _op(handle, state->dPitch, val);
		}	break;
		case ENUM_DPRESSURE:
		{
			state->dPressure = _op(handle, state->dPressure, val);
		}	break;
		case ENUM_DTIMBRE:
		{
			state->dTimbre = _op(handle, state->dTimbre, val);
		}	break;
	}
}

static inline void
_upd(plughandle_t *handle, int64_t frames)
{
	XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
	{
		LV2_Atom_Forge *forge = &handle->forge;

		targetO_t *dst = voice->target;

		xpress_state_t new_state = dst->state;
		new_state.zone += handle->state.zone_offset;
		_modulate(handle, &new_state);

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, voice->uuid, &new_state);
	}
}

static void
_intercept(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_upd(handle, frames);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#modulator_zone_mask_src",
		.offset = offsetof(plugstate_t, zone_mask_src),
		.type = LV2_ATOM__Int
	},
	{
		.property = ESPRESSIVO_URI"#modulator_zone_mask_mod",
		.offset = offsetof(plugstate_t, zone_mask_mod),
		.type = LV2_ATOM__Int
	},
	{
		.property = ESPRESSIVO_URI"#modulator_enum_src",
		.offset = offsetof(plugstate_t, enum_src),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#modulator_enum_mod",
		.offset = offsetof(plugstate_t, enum_mod),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#modulator_zone_offset",
		.offset = offsetof(plugstate_t, zone_offset),
		.type = LV2_ATOM__Int
	},
	{
		.property = ESPRESSIVO_URI"#modulator_multiplier",
		.offset = offsetof(plugstate_t, multiplier),
		.type = LV2_ATOM__Float,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#modulator_adder",
		.offset = offsetof(plugstate_t, adder),
		.type = LV2_ATOM__Float,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#modulator_op",
		.offset = offsetof(plugstate_t, op),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#modulator_reset",
		.offset = offsetof(plugstate_t, reset),
		.type = LV2_ATOM__Bool
	}
};

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;
	src->zone_mask = 1 << state->zone;

	if(src->zone_mask & handle->state.zone_mask_src)
	{
		LV2_Atom_Forge *forge = &handle->forge;

		targetO_t *dst = xpress_create(&handle->xpressO, &src->uuid);
		dst->state = *state;

		xpress_state_t new_state = dst->state;
		new_state.zone += handle->state.zone_offset;
		_modulate(handle, &new_state);

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, src->uuid, &new_state);
	}

	if(src->zone_mask & handle->state.zone_mask_mod)
	{
		if(handle->uuid == 0) // no modulator registered, yet
		{
			handle->uuid = uuid;
			handle->modu = *state;

			_upd(handle, frames);
		}
	}
}

static void
_set(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	if(src->zone_mask & handle->state.zone_mask_src)
	{
		LV2_Atom_Forge *forge = &handle->forge;

		targetO_t *dst = xpress_get(&handle->xpressO, src->uuid);
		dst->state = *state;

		xpress_state_t new_state = dst->state;
		new_state.zone += handle->state.zone_offset;
		_modulate(handle, &new_state);

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, src->uuid, &new_state);
	}

	if(src->zone_mask & handle->state.zone_mask_mod)
	{
		if(handle->uuid == uuid) // this is our modulator
		{
			handle->modu = *state;

			_upd(handle, frames);
		}
	}
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	if(src->zone_mask & handle->state.zone_mask_src)
	{
		LV2_Atom_Forge *forge = &handle->forge;

		xpress_free(&handle->xpressO, src->uuid);

		if(handle->ref)
			handle->ref = xpress_alive(&handle->xpressO, forge, frames);
	}

	if(src->zone_mask & handle->state.zone_mask_mod)
	{
		if(handle->uuid == uuid) // this is our modulator
		{
			handle->uuid = 0;
			if(handle->state.reset)
				handle->modu = empty_state;

			_upd(handle, frames);
		}
	}
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
	{
		xpress_deinit(&handle->xpressI);
		xpress_deinit(&handle->xpressO);
		free(handle);
	}
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

const LV2_Descriptor modulator = {
	.URI						= ESPRESSIVO_MODULATOR_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
