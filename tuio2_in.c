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
#include <string.h>

#include <espressivo.h>
#include <osc.lv2/util.h>
#include <props.h>

#define MAX_NPROPS 6
#define MAX_STRLEN 128

typedef struct _pos_t pos_t;
typedef struct _targetO_t targetO_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _pos_t {
	uint64_t stamp;

	float x;
	float z;
	float a;
	struct {
		float f1;
		float f11;
	} vx;
	struct {
		float f1;
		float f11;
	} vz;
	float v;
	float A;
	float m;
	float R;
};

struct _targetO_t {
	uint32_t sid;
	uint32_t gid;
	uint32_t tuid;

	bool active;
	pos_t pos;
};

struct _plugstate_t {
	int32_t device_width;
	int32_t device_height;
	char device_name [MAX_STRLEN];
	int32_t octave;
	int32_t sensors_per_semitone;
	int32_t filter_stiffness;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_OSC_URID osc_urid;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;
	
	XPRESS_T(xpressO, MAX_NVOICES);
	targetO_t targetO [MAX_NVOICES];
	
	float rate;
	float s;
	float sm1;

	int64_t frames;

	float bot;
	float ran;

	struct {
		uint32_t fid;
		uint64_t last;
		int32_t missed;
		uint16_t width;
		uint16_t height;
		bool ignore;
	} tuio2;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *event_out;

	LV2_Atom_Forge_Ref ref;

	struct {
		LV2_URID device_width;
		LV2_URID device_height;
		LV2_URID device_name;
	} urid;

	PROPS_T(props, MAX_NPROPS);

	plugstate_t state;
	plugstate_t stash;
};

static const targetO_t targetO_vanilla;
static const pos_t pos_vanilla;

static inline void
_stiffness_set(plughandle_t *handle, int32_t stiffness)
{
	handle->s = 1.f / stiffness;
	handle->sm1 = 1.f - handle->s;
	handle->s *= 0.5;
}

static void
_intercept_filter_stiffness(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_stiffness_set(handle, handle->state.filter_stiffness);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#tuio2_deviceWidth",
		.offset = offsetof(plugstate_t, device_width),
		.access = LV2_PATCH__readable,
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_deviceHeight",
		.offset = offsetof(plugstate_t, device_height),
		.access = LV2_PATCH__readable,
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_deviceName",
		.offset = offsetof(plugstate_t, device_name),
		.access = LV2_PATCH__readable,
		.type = LV2_ATOM__String,
		.max_size = MAX_STRLEN 
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_octave",
		.offset = offsetof(plugstate_t, octave),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_sensorsPerSemitone",
		.offset = offsetof(plugstate_t, sensors_per_semitone),
		.type = LV2_ATOM__Int,
	},
	{
		.property = ESPRESSIVO_URI"#tuio2_filterStiffness",
		.offset = offsetof(plugstate_t, filter_stiffness),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept_filter_stiffness
	}
};

static inline void
_pos_init(pos_t *dst, uint64_t stamp)
{
	*dst = pos_vanilla;
	dst->stamp = stamp;
}

static inline void
_pos_clone(pos_t *dst, pos_t *src)
{
	*dst = *src;
}

static inline void
_pos_deriv(plughandle_t *handle, pos_t *neu, pos_t *old)
{
	if(neu->stamp <= old->stamp)
	{
		neu->stamp = old->stamp;
		neu->vx.f1 = old->vx.f1;
		neu->vx.f11 = old->vx.f11;
		neu->vz.f1 = old->vz.f1;
		neu->vz.f11 = old->vz.f11;
		neu->v = old->v;
		neu->A = old->A;
		neu->m = old->m;
		neu->R = old->R;
	}
	else
	{
		const uint32_t sec0 = old->stamp >> 32;
		const uint32_t frc0 = old->stamp & 0xffffffff;
		const uint32_t sec1 = neu->stamp >> 32;
		const uint32_t frc1 = neu->stamp & 0xffffffff;
		const double diff = (sec1 - sec0) + (frc1 - frc0) * 0x1p-32;
		const float rate = 1.0 / diff;

		const float dx = neu->x - old->x;
		neu->vx.f1 = dx * rate;

		const float dz = neu->z - old->z;
		neu->vz.f1 = dz * rate;

		// first-order IIR filter
		neu->vx.f11 = handle->s*(neu->vx.f1 + old->vx.f1) + old->vx.f11*handle->sm1;
		neu->vz.f11 = handle->s*(neu->vz.f1 + old->vz.f1) + old->vz.f11*handle->sm1;

		neu->v = sqrtf(neu->vx.f11 * neu->vx.f11 + neu->vz.f11 * neu->vz.f11);

		const float dv =  neu->v - old->v;
		neu->A = 0.f;
		neu->m = dv * rate;
		neu->R = 0.f;
	}
}

static targetO_t *
_tuio2_get(plughandle_t *handle, uint32_t sid, xpress_uuid_t *uuid)
{
	XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
	{
		targetO_t *dst = voice->target;

		if(dst->sid == sid)
		{
			*uuid = voice->uuid;
			return dst;
		}
	}

	*uuid = 0;
	return NULL;
}

static void
_tuio2_reset(plughandle_t *handle)
{
	XPRESS_VOICE_FREE(&handle->xpressO, voice)
	{}

	handle->tuio2.fid = 0;
	handle->tuio2.last = 0;
	handle->tuio2.missed = 0;
	handle->tuio2.width = 0;
	handle->tuio2.height = 0;
	handle->tuio2.ignore = false;
}

// rt
static int
_tuio2_frm(const char *path, const LV2_Atom_Tuple *args,
	void *data)
{
	plughandle_t *handle = data;
	LV2_OSC_URID *osc_urid= &handle->osc_urid;
	LV2_Atom_Forge *forge = &handle->forge;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);
	uint32_t fid;
	uint64_t last;

	ptr = lv2_osc_int32_get(osc_urid, ptr, (int32_t *)&fid);
	LV2_OSC_Timetag stamp;
	ptr = lv2_osc_timetag_get(osc_urid, ptr, &stamp);
	last = lv2_osc_timetag_parse(&stamp);

	if(!ptr)
		return 1;

	if(fid > handle->tuio2.fid)
	{
		if(last < handle->tuio2.last)
		{
			if(handle->log)
				lv2_log_trace(&handle->logger, "time warp: %08lx must not be smaller than %08lx",
					last, handle->tuio2.last);
		}

		if( (fid > handle->tuio2.fid + 1) && (handle->tuio2.fid > 0) )
		{
			// we have missed one or several bundles
			handle->tuio2.missed += fid - 1 - handle->tuio2.fid;

			if(handle->log)
				lv2_log_trace(&handle->logger, "missed events: %u .. %u (missing: %i)",
					handle->tuio2.fid + 1, fid - 1, handle->tuio2.missed);
		}

		uint32_t dim;
		const char *source;

		handle->tuio2.fid = fid;
		handle->tuio2.last = last;

		ptr = lv2_osc_int32_get(osc_urid, ptr, (int32_t *)&dim);
		if(ptr)
		{
			handle->tuio2.width = dim >> 16;
			handle->tuio2.height = dim & 0xffff;

			if(handle->state.device_width != handle->tuio2.width)
			{
				handle->state.device_width = handle->tuio2.width;
				props_set(&handle->props, forge, handle->frames, handle->urid.device_width, &handle->ref);
			}

			if(handle->state.device_height != handle->tuio2.height)
			{
				handle->state.device_height = handle->tuio2.height;
				props_set(&handle->props, forge, handle->frames, handle->urid.device_height, &handle->ref);
			}
			
			const int n = handle->tuio2.width;
			const float oct = handle->state.octave;
			const int sps = handle->state.sensors_per_semitone; 

			handle->ran = (float)n / sps;
			handle->bot = oct*12.f - 0.5 - (n % (6*sps) / (2.f*sps));
		}
		
		ptr = lv2_osc_string_get(osc_urid, ptr, &source);
		if(ptr && strcmp(handle->state.device_name, source))
		{
			strncpy(handle->state.device_name, source, MAX_STRLEN);
			props_set(&handle->props, forge, handle->frames, handle->urid.device_name, &handle->ref);
		}

		// process this bundle
		handle->tuio2.ignore = false;
	}
	else // fid <= handle->tuio2.fid
	{
		// we have found a previously missed bundle
		handle->tuio2.missed -= 1;

		if(handle->log)
			lv2_log_trace(&handle->logger, "found event: %u (missing: %i)",
				fid, handle->tuio2.missed);

		if(handle->tuio2.missed < 0)
		{
			// we must assume that the peripheral has been reset
			_tuio2_reset(handle);

			if(handle->log)
				lv2_log_trace(&handle->logger, "reset");
		}
		else
		{
			// ignore this bundle
			handle->tuio2.ignore = true;
		}
	}

	XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
	{
		targetO_t *dst = voice->target;

		dst->active = false; // reset active flag
	}

	return 1;
}

// rt
static int
_tuio2_tok(const char *path, const LV2_Atom_Tuple *args,
	void *data)
{
	plughandle_t *handle = data;
	LV2_OSC_URID *osc_urid= &handle->osc_urid;
	LV2_Atom_Forge *forge = &handle->forge;

	pos_t pos;
	_pos_init(&pos, handle->tuio2.last);

	unsigned n = 0;
	LV2_ATOM_TUPLE_FOREACH(args, atom)
		n++;
	const bool has_derivatives = n == 1;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);

	if(handle->tuio2.ignore)
		return 1;

	uint32_t sid;
	ptr = lv2_osc_int32_get(osc_urid, ptr, (int32_t *)&sid);
	if(!ptr)
		return 1;

	xpress_uuid_t uuid;
	targetO_t *dst = _tuio2_get(handle, sid, &uuid);
	if(!dst)
	{
		if((dst = xpress_create(&handle->xpressO, &uuid)))
		{
			*dst = targetO_vanilla;
			dst->sid = sid;
		}
	}
	if(!dst)
		return 1; // failed to register

	ptr = lv2_osc_int32_get(osc_urid, ptr, (int32_t *)&dst->tuid);
	ptr = lv2_osc_int32_get(osc_urid, ptr, (int32_t *)&dst->gid);
	ptr = lv2_osc_float_get(osc_urid, ptr, &pos.x);
	ptr = lv2_osc_float_get(osc_urid, ptr, &pos.z);
	ptr = lv2_osc_float_get(osc_urid, ptr, &pos.a);

	if(has_derivatives)
	{
		ptr = lv2_osc_float_get(osc_urid, ptr, &pos.vx.f11);
		ptr = lv2_osc_float_get(osc_urid, ptr, &pos.vz.f11);
		ptr = lv2_osc_float_get(osc_urid, ptr, &pos.A);
		ptr = lv2_osc_float_get(osc_urid, ptr, &pos.m);
		ptr = lv2_osc_float_get(osc_urid, ptr, &pos.R);
		(void)ptr;
	}
	else // !has_derivatives
	{
		_pos_deriv(handle, &pos, &dst->pos);
	}

	_pos_clone(&dst->pos, &pos);

	const xpress_state_t state = {
		.zone = dst->gid,
		.pitch = dst->pos.x * handle->ran + handle->bot,
		.pressure = dst->pos.z,
		.timbre = 0.f, //TODO compare with dst->tuid
		.dPitch = dst->pos.vx.f11,
		.dPressure = dst->pos.vz.f11,
		.dTimbre = 0.f
	};

	if(handle->ref)
		handle->ref = xpress_token(&handle->xpressO, forge, handle->frames, uuid, &state);

	return 1;
}

// rt
static int
_tuio2_alv(const char *path, const LV2_Atom_Tuple *args,
	void *data)
{
	plughandle_t *handle = data;
	LV2_OSC_URID *osc_urid= &handle->osc_urid;
	LV2_Atom_Forge *forge = &handle->forge;

	if(handle->tuio2.ignore)
		return 1;

	unsigned n = 0;
	LV2_ATOM_TUPLE_FOREACH(args, atom)
		n++;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);
	for(unsigned i=0; (i<n) && ptr; i++)
	{
		uint32_t sid;

		ptr = lv2_osc_int32_get(osc_urid, ptr, (int32_t *)&sid);

		// already registered in this step?
		xpress_uuid_t uuid;
		targetO_t *dst = _tuio2_get(handle, sid, &uuid);
		if(!dst)
		{
			if((dst = xpress_create(&handle->xpressO, &uuid)))
			{
				*dst = targetO_vanilla;
				dst->sid = sid;
			}
		}
		if(!dst)
			continue; // failed to register
		
		dst->active = true; // set active state
	}

	// iterate over inactive blobs
	unsigned freed = 0;

	XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
	{
		targetO_t *dst = voice->target;

		// is is active?
		if(dst->active)
			continue;

		// has it disappeared?
		voice->uuid = 0; // mark for removal
		freed += 1;
	}

	if(freed > 0)
	{
		_xpress_sort(&handle->xpressO);
		handle->xpressO.nvoices -= freed;

		if(handle->ref)
			handle->ref = xpress_alive(&handle->xpressO, forge, handle->frames);
	}

	return 1;
}

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

	handle->rate = rate;
	_stiffness_set(handle, 32);

	xpress_map_t *voice_map = NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
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

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);

	if(  !xpress_init(&handle->xpressO, MAX_NVOICES, handle->map, voice_map,
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

	handle->urid.device_width = props_map(&handle->props, ESPRESSIVO_URI"#tuio2_deviceWidth");
	handle->urid.device_height = props_map(&handle->props, ESPRESSIVO_URI"#tuio2_deviceHeight");
	handle->urid.device_name = props_map(&handle->props, ESPRESSIVO_URI"#tuio2_deviceName");

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->osc_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	handle->state.device_width = 1;
	handle->state.device_height = 1;
	handle->state.device_name[0] = '\0';
}

typedef int (*osc_method_func_t)(const char *path,
	const LV2_Atom_Tuple *arguments, void *data);
typedef struct _method_t method_t;

struct _method_t {
	const char *path;
	osc_method_func_t cb;
};

static const method_t methods [] = {
	{"/tuio2/frm", _tuio2_frm},
	{"/tuio2/tok", _tuio2_tok},
	{"/tuio2/alv", _tuio2_alv},

	{NULL, NULL}
};

static void
_message_cb(const char *path, const LV2_Atom_Tuple *args, void *data)
{
	for(const method_t *meth = methods; meth->cb; meth++)
	{
		if(path && (!meth->path || !strcmp(meth->path, path)) )
		{
			if(meth->cb(path, args, data))
				break; // event handled, break
		}
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;
	
	LV2_Atom_Forge *forge = &handle->forge;
	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, forge, 0, &handle->ref);
	xpress_rst(&handle->xpressO);

	// read incoming OSC
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		handle->frames = ev->time.frames;

		if(!props_advance(&handle->props, forge, handle->frames, obj, &handle->ref))
			lv2_osc_unroll(&handle->osc_urid, obj, _message_cb, handle);
	}

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
	plughandle_t *handle = (plughandle_t *)instance;

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

const LV2_Descriptor tuio2_in = {
	.URI						= ESPRESSIVO_TUIO2_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
