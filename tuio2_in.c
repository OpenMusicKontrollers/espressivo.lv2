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
#include <string.h>

#include <espressivo.h>
#include <lv2_osc.h>
#include <props.h>

#define MAX_NPROPS 6
#define MAX_NVOICES 64

typedef struct _pos_t pos_t;
typedef struct _target_t target_t;
typedef struct _handle_t handle_t;

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

struct _target_t {
	xpress_uuid_t uuid;

	bool active;

	uint32_t gid;
	uint32_t tuid;

	pos_t pos;
};

struct _handle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	osc_forge_t oforge;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;
	
	XPRESS_T(xpress, MAX_NVOICES);
	target_t target [MAX_NVOICES];
	
	float rate;
	float s;
	float sm1;
	uint64_t stamp;

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
		int n;
	} tuio2;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *event_out;

	LV2_Atom_Forge_Ref ref;
	
	struct {
		int32_t device_width;
		int32_t device_height;
		char device_name [128];
		int32_t octave;
		int32_t sensors_per_semitone;
		int32_t filter_stiffness;
	} stat;

	struct {
		LV2_URID device_width;
		LV2_URID device_height;
		LV2_URID device_name;
	} urid;

	PROPS_T(props, MAX_NPROPS);
};

static const props_def_t stat_tuio2_deviceWidth = {
	.property = ESPRESSIVO_URI"#tuio2_deviceWidth",
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat_tuio2_deviceHeight = {
	.property = ESPRESSIVO_URI"#tuio2_deviceHeight",
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat_tuio2_deviceName = {
	.property = ESPRESSIVO_URI"#tuio2_deviceName",
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat_tuio2_octave = {
	.property = ESPRESSIVO_URI"#tuio2_octave",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat_tuio2_sensorsPerSemitone = {
	.property = ESPRESSIVO_URI"#tuio2_sensorsPerSemitone",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat_tuio2_filterStiffness = {
	.property = ESPRESSIVO_URI"#tuio2_filterStiffness",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static inline void
_pos_init(pos_t *dst, uint64_t stamp)
{
	dst->stamp = stamp;
	dst->x = 0.f;
	dst->z = 0.f;
	dst->a = 0.f;
	dst->vx.f1 = 0.f;
	dst->vx.f11 = 0.f;
	dst->vz.f1 = 0.f;
	dst->vz.f11 = 0.f;
	dst->v = 0.f;
	dst->A = 0.f;
	dst->m = 0.f;
	dst->R = 0.f;
	
	//memset(dst, 0x0, sizeof(pos_t));
}

static inline void
_pos_clone(pos_t *dst, pos_t *src)
{
	dst->stamp = src->stamp;
	dst->x = src->x;
	dst->z = src->z;
	dst->a = src->a;
	dst->vx.f1 = src->vx.f1;
	dst->vx.f11 = src->vx.f11;
	dst->vz.f1 = src->vz.f1;
	dst->vz.f11 = src->vz.f11;
	dst->v = src->v;
	dst->A = src->A;
	dst->m = src->m;
	dst->R = src->R;
	
	//memcpy(dst, src, sizeof(pos_t));
}

static inline void
_pos_deriv(handle_t *handle, pos_t *neu, pos_t *old)
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

static void
_tuio2_reset(handle_t *handle)
{
	XPRESS_VOICE_FREE(&handle->xpress, voice)
	{}

	handle->tuio2.fid = 0;
	handle->tuio2.last = 0;
	handle->tuio2.missed = 0;
	handle->tuio2.width = 0;
	handle->tuio2.height = 0;
	handle->tuio2.ignore = false;
	handle->tuio2.n = 0;
}

uint64_t last_last = 0ULL;

// rt
static int
_tuio2_frm(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	handle_t *handle = data;
	osc_forge_t *oforge = &handle->oforge;
	LV2_Atom_Forge *forge = &handle->forge;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);
	uint32_t fid;
	uint64_t last;

	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&fid);
	ptr = osc_deforge_timestamp(oforge, forge, ptr, &handle->stamp);

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

		ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&dim);
		if(ptr)
		{
			handle->tuio2.width = dim >> 16;
			handle->tuio2.height = dim & 0xffff;

			if(handle->stat.device_width != handle->tuio2.width)
			{
				handle->stat.device_width = handle->tuio2.width;
				props_set(&handle->props, forge, handle->frames, handle->urid.device_width);
			}

			if(handle->stat.device_height != handle->tuio2.height)
			{
				handle->stat.device_height = handle->tuio2.height;
				props_set(&handle->props, forge, handle->frames, handle->urid.device_height);
			}
			
			const int n = handle->tuio2.width;
			const float oct = handle->stat.octave;
			const int sps = handle->stat.sensors_per_semitone; 

			handle->ran = (float)n / sps;
			handle->bot = oct*12.f - 0.5 - (n % (6*sps) / (2.f*sps));
		}
		
		ptr = osc_deforge_string(oforge, forge, ptr, &source);
		if(ptr && strcmp(handle->stat.device_name, source))
		{
			strcpy(handle->stat.device_name, source);
			props_set(&handle->props, forge, handle->frames, handle->urid.device_name);
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

	XPRESS_VOICE_FOREACH(&handle->xpress, voice)
	{
		target_t *src = voice->target;

		src->active = false; // reset active flag
	}

	return 1;
}

// rt
static int
_tuio2_tok(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	handle_t *handle = data;
	osc_forge_t *oforge = &handle->oforge;
	LV2_Atom_Forge *forge = &handle->forge;

	pos_t pos;
	_pos_init(&pos, handle->stamp);

	int has_derivatives = strlen(fmt) == 11;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);

	if(handle->tuio2.ignore)
		return 1;

	uint32_t sid;
	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&sid);
	if(!ptr)
		return 1;

	target_t *src = xpress_get(&handle->xpress, sid);
	if(!src)
	{
		if(!(src = xpress_add(&handle->xpress, sid)))
			return 1; // failed to register

		src->uuid = xpress_map(&handle->xpress);
	}

	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&src->tuid);
	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&src->gid);
	ptr = osc_deforge_float(oforge, forge, ptr, &pos.x);
	ptr = osc_deforge_float(oforge, forge, ptr, &pos.z);
	ptr = osc_deforge_float(oforge, forge, ptr, &pos.a);

	if(has_derivatives)
	{
		ptr = osc_deforge_float(oforge, forge, ptr, &pos.vx.f11);
		ptr = osc_deforge_float(oforge, forge, ptr, &pos.vz.f11);
		ptr = osc_deforge_float(oforge, forge, ptr, &pos.A);
		ptr = osc_deforge_float(oforge, forge, ptr, &pos.m);
		ptr = osc_deforge_float(oforge, forge, ptr, &pos.R);
		(void)ptr;
	}
	else // !has_derivatives
	{
		_pos_deriv(handle, &pos, &src->pos);
	}

	_pos_clone(&src->pos, &pos);

	return 1;
}

// rt
static int
_tuio2_alv(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	handle_t *handle = data;
	osc_forge_t *oforge = &handle->oforge;
	LV2_Atom_Forge *forge = &handle->forge;

	if(handle->tuio2.ignore)
		return 1;

	const unsigned n = strlen(fmt);

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);
	for(unsigned i=0; (i<n) && ptr; i++)
	{
		uint32_t sid;

		ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&sid);

		// already registered in this step?
		target_t *src = xpress_get(&handle->xpress, sid);
		if(!src)
		{
			if(!(src = xpress_add(&handle->xpress, sid)))
				continue; // failed to register

			src->uuid = xpress_map(&handle->xpress);
		}
			
		src->active = true; // set active state
	}

	// iterate over inactive blobs
	unsigned removed = 0;
	XPRESS_VOICE_FOREACH(&handle->xpress, voice)
	{
		target_t *src = voice->target;

		// has it disappeared?
		// is is active?
		if(src->active)
			continue;

		if(handle->ref)
			handle->ref = xpress_del(&handle->xpress, forge, handle->frames, src->uuid);

		voice->uuid = 0; // mark for removal
		removed++;
	}
	_xpress_sort(&handle->xpress);
	handle->xpress.nvoices -= removed;

	// iterate over active blobs
	XPRESS_VOICE_FOREACH(&handle->xpress, voice)
	{
		target_t *src = voice->target;

		const xpress_state_t state = {
			.zone = src->gid,
			.position[0] = src->pos.x * handle->ran + handle->bot,
			.position[1] = src->pos.z,
			.velocity[0] = src->pos.vx.f11,
			.velocity[1] = src->pos.vz.f11
		};

		if(handle->ref)
			handle->ref = xpress_put(&handle->xpress, forge, handle->frames, src->uuid, &state);
	}

	handle->tuio2.n = n;

	return 1;
}

static const xpress_iface_t iface = {
	.size = sizeof(target_t)
};

static inline void
_stiffness_set(handle_t *handle, int32_t stiffness)
{
	handle->s = 1.f / 32.f; //FIXME make stiffness configurable
	handle->sm1 = 1.f - handle->s;
	handle->s *= 0.5;
}

static void
_intercept_filter_stiffness(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	handle_t *handle = data;

	_stiffness_set(handle, handle->stat.filter_stiffness);
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
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
	osc_forge_init(&handle->oforge, handle->map);

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);

	if(!xpress_init(&handle->xpress, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_NONE, &iface, handle->target, NULL))
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

	if(  (handle->urid.device_width = props_register(&handle->props, &stat_tuio2_deviceWidth, PROP_EVENT_NONE, NULL,
				&handle->stat.device_width))
		&& (handle->urid.device_height = props_register(&handle->props, &stat_tuio2_deviceHeight, PROP_EVENT_NONE, NULL,
				&handle->stat.device_height))
		&& (handle->urid.device_name = props_register(&handle->props, &stat_tuio2_deviceName, PROP_EVENT_NONE, NULL,
				&handle->stat.device_name))

		&& props_register(&handle->props, &stat_tuio2_octave, PROP_EVENT_NONE, NULL, &handle->stat.octave)
		&& props_register(&handle->props, &stat_tuio2_sensorsPerSemitone, PROP_EVENT_NONE, NULL, &handle->stat.sensors_per_semitone)
		&& props_register(&handle->props, &stat_tuio2_filterStiffness, PROP_EVENT_WRITE, _intercept_filter_stiffness, &handle->stat.filter_stiffness) )
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
	handle_t *handle = (handle_t *)instance;

	handle->stat.device_width = 1;
	handle->stat.device_height = 1;
	handle->stat.device_name[0] = '\0';
}

typedef int (*osc_method_func_t)(const char *path, const char *fmt,
	const LV2_Atom_Tuple *arguments, void *data);
typedef struct _method_t method_t;

struct _method_t {
	const char *path;
	const char *fmt;
	osc_method_func_t cb;
};

static const method_t methods [] = {
	{"/tuio2/frm", "itis", _tuio2_frm},
	{"/tuio2/tok", "iiifff", _tuio2_tok},
	{"/tuio2/tok", "iiiffffffff", _tuio2_tok},
	{"/tuio2/alv", NULL, _tuio2_alv},

	{NULL, NULL, NULL}
};

static void
_message_cb(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	for(const method_t *meth = methods; meth->cb; meth++)
	{
		if(path && (!meth->path || !strcmp(meth->path, path)) )
		{
			if(fmt && (!meth->fmt || !strcmp(meth->fmt, fmt)) )
			{
				if(meth->cb(path, fmt, args, data))
					break; // event handled, break
			}
		}
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;
	
	LV2_Atom_Forge *forge = &handle->forge;
	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	// read incoming OSC
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		handle->frames = ev->time.frames;

		if(!props_advance(&handle->props, forge, handle->frames, obj, &handle->ref))
			osc_atom_event_unroll(&handle->oforge, obj, NULL, NULL, _message_cb, handle);
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

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
