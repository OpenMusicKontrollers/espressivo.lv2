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

typedef struct _pos_t pos_t;
typedef struct _tuio2_ref_t tuio2_ref_t;
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

struct _tuio2_ref_t {
	uint32_t gid;
	uint32_t tuid;

	pos_t pos;
};

struct _handle_t {
	LV2_URID_Map *map;
	espressivo_forge_t cforge;
	osc_forge_t oforge;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;
	
	float rate;
	float s;
	float sm1;
	uint64_t stamp;
	
	int64_t rel;

	float bot;
	float ran;

	struct {
		espressivo_dict_t dict [2][ESPRESSIVO_DICT_SIZE];
		tuio2_ref_t ref [2][ESPRESSIVO_DICT_SIZE];
		int pos;

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
	} stat;
	props_t *props;
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

// rt
static inline LV2_Atom_Forge_Ref
_chim_event(handle_t *handle, int64_t frames, espressivo_event_t *cev)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Ref ref;

	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = espressivo_event_forge(&handle->cforge, cev);

	return ref;
}

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
		float rate = handle->rate / (neu->stamp - old->stamp);

		float dx = neu->x - old->x;
		neu->vx.f1 = dx * rate;

		float dz = neu->z - old->z;
		neu->vz.f1 = dz * rate;

		// first-order IIR filter
		neu->vx.f11 = handle->s*(neu->vx.f1 + old->vx.f1) + old->vx.f11*handle->sm1;
		neu->vz.f11 = handle->s*(neu->vz.f1 + old->vz.f1) + old->vz.f11*handle->sm1;

		neu->v = sqrtf(neu->vx.f11 * neu->vx.f11 + neu->vz.f11 * neu->vz.f11);

		float dv =  neu->v - old->v;
		neu->A = 0.f;
		neu->m = dv * rate;
		neu->R = 0.f;
	}
}

static void
_tuio2_reset(handle_t *handle)
{
	espressivo_dict_clear(handle->tuio2.dict[0]);
	espressivo_dict_clear(handle->tuio2.dict[1]);
	handle->tuio2.pos = 0;

	handle->tuio2.fid = 0;
	handle->tuio2.last = 0;
	handle->tuio2.missed = 0;
	handle->tuio2.width = 0;
	handle->tuio2.height = 0;
	handle->tuio2.ignore = false;
	handle->tuio2.n = 0;
}

// rt
static int
_tuio2_frm(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	handle_t *handle = data;
	osc_forge_t *oforge = &handle->oforge;
	LV2_Atom_Forge *forge = &handle->cforge.forge;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);
	uint32_t fid;
	uint64_t last;

	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&fid);
	ptr = osc_deforge_timestamp(oforge, forge, ptr, &last);

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
				handle->stat.device_width = handle->tuio2.width; //TODO update
			if(handle->stat.device_height != handle->tuio2.height)
				handle->stat.device_height = handle->tuio2.height; //TODO update
			
			const int n = handle->tuio2.width;
			const float oct = handle->stat.octave;
			const int sps = handle->stat.sensors_per_semitone; 

			handle->ran = (float)n / sps;
			handle->bot = oct*12.f - 0.5 - (n % (6*sps) / (2.f*sps));
		}
		
		ptr = osc_deforge_string(oforge, forge, ptr, &source);
		if(ptr && strcmp(handle->stat.device_name, source))
		{
			strcpy(handle->stat.device_name, source); //TODO update
		}

		handle->tuio2.pos ^= 1; // toggle pos
		espressivo_dict_clear(handle->tuio2.dict[handle->tuio2.pos]);

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

	return 1;
}

// rt
static int
_tuio2_tok(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	handle_t *handle = data;
	osc_forge_t *oforge = &handle->oforge;
	LV2_Atom_Forge *forge = &handle->cforge.forge;

	pos_t pos;
	_pos_init(&pos, handle->stamp);

	int has_derivatives = strlen(fmt) == 11;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);
	tuio2_ref_t *ref = NULL;

	if(handle->tuio2.ignore)
		return 1;

	uint32_t sid;
	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&sid);
	if(ptr)
		ref = espressivo_dict_add(handle->tuio2.dict[handle->tuio2.pos], sid); // get new blob ref
	if(!ref)
		return 1;

	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&ref->tuid);
	ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&ref->gid);
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
		_pos_deriv(handle, &pos, &ref->pos);
	}

	_pos_clone(&ref->pos, &pos);

	return 1;
}

// rt
static int
_tuio2_alv(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	handle_t *handle = data;
	osc_forge_t *oforge = &handle->oforge;
	LV2_Atom_Forge *forge = &handle->cforge.forge;

	const LV2_Atom *ptr = lv2_atom_tuple_begin(args);
	espressivo_event_t cev;
	int n;
	uint32_t sid;
	tuio2_ref_t *dst;
	tuio2_ref_t *src;

	if(handle->tuio2.ignore)
		return 1;

	n = strlen(fmt);

	for(int i=0; i<n; i++)
	{
		if((ptr = osc_deforge_int32(oforge, forge, ptr, (int32_t *)&sid)))
		{
			// already registered in this step?
			dst = espressivo_dict_ref(handle->tuio2.dict[handle->tuio2.pos], sid);
			if(!dst)
			{
				// register in this step
				dst = espressivo_dict_add(handle->tuio2.dict[handle->tuio2.pos], sid);
				// clone from previous step
				src = espressivo_dict_ref(handle->tuio2.dict[!handle->tuio2.pos], sid);
				if(dst && src)
					memcpy(dst, src, sizeof(tuio2_ref_t));
			}
		}
	}

	// iterate over last step's blobs
	ESPRESSIVO_DICT_FOREACH(handle->tuio2.dict[!handle->tuio2.pos], sid, src)
	{
		// is it registered in this step?
		if(!espressivo_dict_ref(handle->tuio2.dict[handle->tuio2.pos], sid))
		{
			// is disappeared blob
			cev.state = ESPRESSIVO_STATE_OFF;
			cev.sid = sid;
			cev.gid = src->gid;
			cev.dim[0] = _midi2cps(src->pos.x * handle->ran + handle->bot);
			cev.dim[1] = src->pos.z;
			cev.dim[2] = src->pos.vx.f11;
			cev.dim[3] = src->pos.vz.f11;

			if(handle->ref)
				handle->ref = _chim_event(handle, handle->rel, &cev);
		}
	}

	// iterate over this step's blobs
	ESPRESSIVO_DICT_FOREACH(handle->tuio2.dict[handle->tuio2.pos], sid, dst)
	{
		cev.sid = sid;
		cev.gid = dst->gid;
		cev.dim[0] = _midi2cps(dst->pos.x * handle->ran + handle->bot);
		cev.dim[1] = dst->pos.z;
		cev.dim[2] = dst->pos.vx.f11;
		cev.dim[3] = dst->pos.vz.f11;

		// was it registered in previous step?
		if(!espressivo_dict_ref(handle->tuio2.dict[!handle->tuio2.pos], sid))
			cev.state = ESPRESSIVO_STATE_ON;
		else
			cev.state = ESPRESSIVO_STATE_SET;

		if(handle->ref)
			handle->ref = _chim_event(handle, handle->rel, &cev);
	}

	if(!n && !handle->tuio2.n)
	{
		// is idling
		cev.state = ESPRESSIVO_STATE_IDLE;
		cev.sid = 0;
		cev.gid = 0;
		cev.dim[0] = 0.f;
		cev.dim[1] = 0.f;
		cev.dim[2] = 0.f;
		cev.dim[3] = 0.f;

		if(handle->ref)
			handle->ref = _chim_event(handle, handle->rel, &cev);
	}

	handle->tuio2.n = n;

	return 1;
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	handle->rate = rate;
	handle->s = 1.f / 32.f; //FIXME make stiffness configurable
	handle->sm1 = 1.f - handle->s;
	handle->s *= 0.5;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	espressivo_forge_init(&handle->cforge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);
	ESPRESSIVO_DICT_INIT(handle->tuio2.dict[0], handle->tuio2.ref[0]);
	ESPRESSIVO_DICT_INIT(handle->tuio2.dict[1], handle->tuio2.ref[1]);

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);

	handle->props = props_new(5, descriptor->URI, handle->map, handle);
	if(!handle->props)
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	props_register(handle->props, &stat_tuio2_deviceWidth, NULL, &handle->stat.device_width);
	props_register(handle->props, &stat_tuio2_deviceHeight, NULL, &handle->stat.device_height);
	props_register(handle->props, &stat_tuio2_deviceName, NULL, &handle->stat.device_name);

	props_register(handle->props, &stat_tuio2_octave, NULL, &handle->stat.octave);
	props_register(handle->props, &stat_tuio2_sensorsPerSemitone, NULL, &handle->stat.sensors_per_semitone);

	props_sort(handle->props);

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

	handle->stamp = 0;

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
		if(!meth->path || !strcmp(meth->path, path))
		{
			if(!meth->fmt || !strcmp(meth->fmt, fmt))
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
	
	handle->stamp += nsamples;

	LV2_Atom_Forge *forge = &handle->cforge.forge;
	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	// read incoming OSC
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		if(!props_advance(handle->props, forge, ev->time.frames, obj, &handle->ref))
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
