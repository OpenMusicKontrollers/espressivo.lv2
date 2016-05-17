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
#define MAX_NVOICES 64
#define MAX_STRLEN 128

typedef struct _target_t target_t;
typedef struct _state_t state_t;
typedef struct _handle_t handle_t;

struct _target_t {
	xpress_uuid_t uuid;
	xpress_state_t state;
	bool dirty;
	int64_t last;
};

struct _state_t {
	int32_t device_width;
	int32_t device_height;
	char device_name [MAX_STRLEN];
	int32_t octave;
	int32_t sensors_per_semitone;
	float timestamp_offset;
};

struct _handle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;
	LV2_OSC_URID osc_urid;
	LV2_OSC_Schedule *osc_sched;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpress, MAX_NVOICES);
	target_t target [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	int32_t dim;
	int32_t fid;
	bool dirty;
	int64_t last;
	float bot;
	float ran_1;

	state_t state;
	state_t stash;
};

static void
_intercept(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	handle_t *handle = data;

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

static const props_def_t stat_tuio2_deviceWidth = {
	.property = ESPRESSIVO_URI"#tuio2_deviceWidth",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC,
	.event_mask = PROP_EVENT_WRITE,
	.event_cb = _intercept
};

static const props_def_t stat_tuio2_deviceHeight = {
	.property = ESPRESSIVO_URI"#tuio2_deviceHeight",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC,
	.event_mask = PROP_EVENT_WRITE,
	.event_cb = _intercept
};

static const props_def_t stat_tuio2_octave = {
	.property = ESPRESSIVO_URI"#tuio2_octave",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC,
	.event_mask = PROP_EVENT_WRITE,
	.event_cb = _intercept
};

static const props_def_t stat_tuio2_sensorsPerSemitone = {
	.property = ESPRESSIVO_URI"#tuio2_sensorsPerSemitone",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC,
	.event_mask = PROP_EVENT_WRITE,
	.event_cb = _intercept
};

static const props_def_t stat_tuio2_deviceName = {
	.property = ESPRESSIVO_URI"#tuio2_deviceName",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_STATIC,
	.max_size = MAX_STRLEN 
};

static const props_def_t stat_tuio2_timestampOffset = {
	.property = ESPRESSIVO_URI"#tuio2_timestampOffset",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Float,
	.mode = PROP_MODE_STATIC
};

static inline LV2_Atom_Forge_Ref
_frm(handle_t *handle, uint64_t ttag)
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
_tok_2d(handle_t *handle, xpress_uuid_t uuid, const xpress_state_t *state)
{
	/*
		/tuio2/tok s_id tu_id c_id x_pos y_pos angle [x_vel y_vel a_vel m_acc r_acc]
		/tuio2/tok int32 int32 int32 float float float [float float float float float]
	*/

	const int32_t sid = uuid;
	const int32_t tuid = 0;
	const int32_t gid = state->zone;
	const float macc = sqrtf(state->acceleration[0]*state->acceleration[0]
		+ state->acceleration[1]*state->acceleration[1]);

	return lv2_osc_forge_message_vararg(&handle->forge, &handle->osc_urid,
		"/tuio2/tok", "iiiffffffff",
		sid, tuid, gid,
		state->position[0], state->position[1], 0.f,
		state->velocity[0], state->velocity[1], 0.f,
		macc, 0.f);
}

static inline LV2_Atom_Forge_Ref
_alv(handle_t *handle)
{
	/*
		/tuio2/alv s_id0 ... s_idN
		/tuio2/alv int32... int32 
	*/

	LV2_Atom_Forge_Ref ref;
	LV2_Atom_Forge_Frame msg_frame [2];

	ref = lv2_osc_forge_message_head(&handle->forge, &handle->osc_urid, msg_frame,
		"/tuio2/alv");

	XPRESS_VOICE_FOREACH(&handle->xpress, voice)
	{
		target_t *src = voice->target;

		if(ref)
			ref = lv2_osc_forge_int(&handle->forge, &handle->osc_urid, src->uuid);
	}

	if(ref)
		lv2_osc_forge_pop(&handle->forge,  msg_frame);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_tuio2_2d(handle_t *handle, int64_t from, int64_t to)
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

	XPRESS_VOICE_FOREACH(&handle->xpress, voice)
	{
		target_t *src = voice->target;

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
_upd(handle_t *handle, int64_t frames)
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
	handle_t *handle = data;
	target_t *src = target;

	_upd(handle, frames);

	src->uuid = xpress_map(&handle->xpress);

	memcpy(&src->state, state, sizeof(xpress_state_t));
	src->state.position[0] = (state->position[0] - handle->bot) * handle->ran_1;
	src->dirty = true;
	src->last = frames;

	handle->dirty = true;
}

static void
_put(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;

	_upd(handle, frames);

	memcpy(&src->state, state, sizeof(xpress_state_t));
	src->state.position[0] = (state->position[0] - handle->bot) * handle->ran_1;
	src->dirty = true;
	src->last = frames;

	handle->dirty = true;
}

static void
_del(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	handle_t *handle = data;
	target_t *src = target;

	_upd(handle, frames);

	handle->dirty = true;
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

	if(  !props_register(&handle->props, &stat_tuio2_deviceWidth,
			&handle->state.device_width, &handle->stash.device_width)
		|| !props_register(&handle->props, &stat_tuio2_deviceHeight,
			&handle->state.device_height, &handle->stash.device_height)
		|| !props_register(&handle->props, &stat_tuio2_octave,
			&handle->state.octave, &handle->stash.octave)
		|| !props_register(&handle->props, &stat_tuio2_sensorsPerSemitone,
			&handle->state.sensors_per_semitone, &handle->stash.sensors_per_semitone)
		|| !props_register(&handle->props, &stat_tuio2_deviceName,
			handle->state.device_name, handle->stash.device_name)
		|| !props_register(&handle->props, &stat_tuio2_timestampOffset,
			&handle->state.timestamp_offset, &handle->stash.timestamp_offset) )
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

	handle->last = -1;
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(handle->last == -1)
			handle->last = frames;

		if(!props_advance(&handle->props, forge, handle->last, obj, &handle->ref)) //XXX frame time
		{
			xpress_advance(&handle->xpress, forge, frames, obj, &handle->ref);
		}
	}
	_upd(handle, nsamples - 1);

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
