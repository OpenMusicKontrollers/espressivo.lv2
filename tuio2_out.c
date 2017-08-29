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
#include <osc.lv2/forge.h>

#define MAX_NPROPS 6
#define MAX_STRLEN 128

typedef struct _targetI_t targetI_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _targetI_t {
	xpress_uuid_t uuid;
	xpress_state_t state;
	bool dirty;
	int64_t last;
};

struct _plugstate_t {
	int32_t device_width;
	int32_t device_height;
	char device_name [MAX_STRLEN];
	int32_t octave;
	int32_t sensors_per_semitone;
	float timestamp_offset;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;
	LV2_OSC_URID osc_urid;
	LV2_OSC_Schedule *osc_sched;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpressI, MAX_NVOICES);
	targetI_t targetI [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	int32_t dim;
	int32_t fid;
	bool dirty;
	int64_t last;
	float bot;
	float ran_1;

	plugstate_t state;
	plugstate_t stash;
};

static void
_intercept(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	int32_t w = handle->state.device_width;
	const int32_t h = handle->state.device_height;
	const float oct = handle->state.octave;
	int32_t sps = handle->state.sensors_per_semitone; 

	if(w <= 0)
		w = 1;
	if(sps <= 0)
		sps = 1;

	handle->ran_1 = (float)sps / w;
	handle->bot = oct*12.f - 0.5 - (w % (6*sps) / (2.f*sps));

	handle->dim = (w << 16) | h;
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#tuio2_deviceWidth",
		.offset = offsetof(plugstate_t, device_width),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_deviceHeight",
		.offset = offsetof(plugstate_t, device_height),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_octave",
		.offset = offsetof(plugstate_t, octave),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_sensorsPerSemitone",
		.offset = offsetof(plugstate_t, sensors_per_semitone),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_deviceName",
		.offset = offsetof(plugstate_t, device_name),
		.type = LV2_ATOM__String,
		.max_size = MAX_STRLEN 
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_timestampOffset",
		.offset = offsetof(plugstate_t, timestamp_offset),
		.type = LV2_ATOM__Float,
	}
};

static inline LV2_Atom_Forge_Ref
_frm(plughandle_t *handle, uint64_t ttag)
{
	/*
		/tuio2/frm f_id time dim source
		/tuio2/frm int32 ttag int32 string
	*/

	return lv2_osc_forge_message_vararg(&handle->forge, &handle->osc_urid,
		"/tuio2/frm", "itis",
		++handle->fid, ttag, handle->dim, handle->state.device_name);
}

static inline LV2_Atom_Forge_Ref
_tok_2d(plughandle_t *handle, xpress_uuid_t uuid, const xpress_state_t *state)
{
	/*
		/tuio2/tok s_id tu_id c_id x_pos y_pos angle [x_vel y_vel a_vel m_acc r_acc]
		/tuio2/tok int32 int32 int32 float float float [float float float float float]
	*/

	const int32_t sid = uuid;
	const int32_t tuid = 0;
	const int32_t gid = state->zone;
	/*FIXME
	const float macc = sqrtf(state->acceleration[0]*state->acceleration[0]
		+ state->acceleration[1]*state->acceleration[1]);
	*/
	const float macc = 0.f;

	return lv2_osc_forge_message_vararg(&handle->forge, &handle->osc_urid,
		"/tuio2/tok", "iiiffffffff",
		sid, tuid, gid,
		state->pitch, state->pressure, 0.f,
		state->dPitch, state->dPressure, 0.f,
		macc, 0.f);
}

static inline LV2_Atom_Forge_Ref
_alv(plughandle_t *handle)
{
	/*
		/tuio2/alv s_id0 ... s_idN
		/tuio2/alv int32... int32 
	*/

	LV2_Atom_Forge_Ref ref;
	LV2_Atom_Forge_Frame msg_frame [2];

	ref = lv2_osc_forge_message_head(&handle->forge, &handle->osc_urid, msg_frame,
		"/tuio2/alv");

	XPRESS_VOICE_FOREACH(&handle->xpressI, voice)
	{
		targetI_t *src = voice->target;

		if(ref)
			ref = lv2_osc_forge_int(&handle->forge, &handle->osc_urid, src->uuid);
	}

	if(ref)
		lv2_osc_forge_pop(&handle->forge,  msg_frame);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_tuio2_2d(plughandle_t *handle, int64_t from, int64_t to)
{
	LV2_Atom_Forge_Frame bndl_frame [2];
	LV2_Atom_Forge_Frame msg_frame [2];
	LV2_Atom_Forge_Ref ref;

	uint64_t ttag0 = 1ULL; // immediate
	LV2_OSC_Timetag ttag1 = {.integral = 0, .fraction = 1};
	if(handle->osc_sched)
	{
		// get timetag corresponding to frame time
		ttag0 = handle->osc_sched->frames2osc(handle->osc_sched->handle, from);

		// calculate bundle timetag
		if(handle->state.timestamp_offset == 0.f)
		{
			lv2_osc_timetag_create(&ttag1, ttag0);
		}
		else
		{
			uint64_t sec = ttag0 >> 32;
			double frac = (ttag0 & 0xffffffff) * 0x1p-32;
			frac += handle->state.timestamp_offset * 1e-3;
			while(frac >= 1.0)
			{
				sec += 1;
				frac -= 1.0;
			}
			ttag1.integral = sec;
			ttag1.fraction = frac * 0x1p32;
		}
	}

	ref = lv2_atom_forge_frame_time(&handle->forge, from);
	if(ref)
		ref = lv2_osc_forge_bundle_head(&handle->forge, &handle->osc_urid, bndl_frame, &ttag1);
	if(ref)
		ref = _frm(handle, ttag0);

	XPRESS_VOICE_FOREACH(&handle->xpressI, voice)
	{
		targetI_t *src = voice->target;

		if(src->dirty && (src->last < to) )
		{
			if(ref)
				ref = _tok_2d(handle, src->uuid, &src->state);

			src->dirty= false;
		}
	}

	if(ref)
		ref = _alv(handle);
	if(ref)
		lv2_osc_forge_pop(&handle->forge, bndl_frame);

	return ref;
}

static inline void
_upd(plughandle_t *handle, int64_t frames)
{
	if(handle->dirty && (frames > handle->last) )
	{
		if(handle->ref)
			handle->ref = _tuio2_2d(handle, handle->last, frames);

		handle->last = frames;
		handle->dirty = false;
	}
}

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	_upd(handle, frames);

	src->uuid = xpress_map(&handle->xpressI);
	src->state = *state;
	src->state.pitch = (state->pitch*0x7f - handle->bot) * handle->ran_1;
	src->dirty = true;
	src->last = frames;

	handle->dirty = true;
}

static void
_set(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	_upd(handle, frames);

	src->state = *state;
	src->state.pitch = (state->pitch*0x7f - handle->bot) * handle->ran_1;
	src->dirty = true;
	src->last = frames;

	handle->dirty = true;
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	_upd(handle, frames);

	handle->dirty = true;
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

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, XPRESS__voiceMap))
			voice_map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OSC__schedule))
			handle->osc_sched = features[i]->data;
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
			XPRESS_EVENT_ALL, &ifaceI, handle->targetI, handle))
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

	handle->last = -1;
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(handle->last == -1)
			handle->last = frames;

		if(!props_advance(&handle->props, forge, handle->last, obj, &handle->ref)) //XXX frame time
		{
			xpress_advance(&handle->xpressI, forge, frames, obj, &handle->ref);
		}
	}
	_upd(handle, nsamples - 1);

	xpress_post(&handle->xpressI, nsamples-1);

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

const LV2_Descriptor tuio2_out= {
	.URI						= ESPRESSIVO_TUIO2_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
