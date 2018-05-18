/*
 * Copyright (c) 2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <osc.lv2/osc.h>
#include <osc.lv2/util.h>
#include <osc.lv2/endian.h>

#include <canvas.lv2/forge.h>
#include <canvas.lv2/forge.h>

#define MAX_NPROPS 0

typedef struct _targetI_t targetI_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _targetI_t {
	xpress_state_t state;
};

struct _plugstate_t {
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	LV2_OSC_URID osc_urid;
	LV2_Canvas_URID canvas_urid;

	unsigned n;
	bool needs_sync;

	plugstate_t state;
	plugstate_t stash;

	uint32_t overflow;
	uint32_t overflowsec;
	uint32_t counter;

	PROPS_T(props, MAX_NPROPS);
	XPRESS_T(xpressI, MAX_NVOICES);
	targetI_t targetI [MAX_NVOICES];
};

static const props_def_t defs [MAX_NPROPS] = {
	// empty
};

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	src->state = *state;

	handle->needs_sync = true;
}

static void
_set(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	src->state = *state;

	handle->needs_sync = true;
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;

	handle->needs_sync = true;
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
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);

	const float update_rate = 120.f; //FIXME read from options
	handle->overflow = rate / update_rate;
	handle->overflowsec = rate;

	lv2_atom_forge_init(&handle->forge, handle->map);
	lv2_osc_urid_init(&handle->osc_urid, handle->map);
	lv2_canvas_urid_init(&handle->canvas_urid, handle->map);

	if(  !xpress_init(&handle->xpressI, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_ALL, &ifaceI, handle->targetI, handle) )
	{
		free(handle);
		return NULL;
	}

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		fprintf(stderr, "failed to initialize property structure\n");
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
			handle->event_in = (const void *)data;
			break;
		case 1:
			handle->event_out = data;
			break;
		default:
			break;
	}
}

#define NUM_COLS 6

static const uint32_t cols [NUM_COLS] = {
	[0] = 0xff0000ff,
	[1] = 0x00ff00ff,
	[2] = 0x0000ffff,
	[3] = 0xffff00ff,
	[4] = 0xff00ffff,
	[5] = 0x00ffffff
};

static LV2_Atom_Forge_Ref
_render(plughandle_t *handle, uint32_t frames)
{
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Canvas_URID *canvas_urid = &handle->canvas_urid;
	LV2_Atom_Forge_Frame frame [2];

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);;
	if(ref)
		ref = lv2_atom_forge_object(forge, &frame[0], 0, handle->props.urid.patch_set);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, handle->props.urid.patch_property);
		if(ref)
			ref = lv2_atom_forge_urid(forge, canvas_urid->Canvas_graph);

		if(ref)
			ref = lv2_atom_forge_key(forge, handle->props.urid.patch_value);
		if(ref)
			ref = lv2_atom_forge_tuple(forge, &frame[1]);
		{
			// draw background
			if(ref)
				ref = lv2_canvas_forge_style(forge, canvas_urid, 0x222222ff);
			if(ref)
				ref = lv2_canvas_forge_rectangle(forge, canvas_urid, 0.f, 0.f, 1.f, 1.f);
			if(ref)
				ref = lv2_canvas_forge_fill(forge, canvas_urid);

			if(ref)
				ref = lv2_canvas_forge_lineWidth(forge, canvas_urid, 0.002f);

			XPRESS_VOICE_FOREACH(&handle->xpressI, voice)
			{
				targetI_t *src = voice->target;

				//FIXME
				const float x = src->state.pitch;
				const float vx = src->state.dPitch * 0.5f;
				const float y = 0.5f;
				const float r = src->state.pressure * 0.1f;
				const float vr = -src->state.dPressure * 0.08f;
				const float a1 = 0.f;
				const float a2 = 2.f * M_PI;
				const uint32_t col = cols[voice->uuid % NUM_COLS];

				if(ref)
					ref = lv2_canvas_forge_style(forge, canvas_urid, col);

				if(ref)
					ref = lv2_canvas_forge_arc(forge, canvas_urid, x, y, r, a1, a2);
				if(ref)
					ref = lv2_canvas_forge_stroke(forge, canvas_urid);

				if(ref)
					ref = lv2_canvas_forge_moveTo(forge, canvas_urid, x, y);
				if(ref)
					ref = lv2_canvas_forge_lineTo(forge, canvas_urid, x + vx, y);
				if(ref)
					ref = lv2_canvas_forge_stroke(forge, canvas_urid);

				if(ref)
					ref = lv2_canvas_forge_moveTo(forge, canvas_urid, x, y);
				if(ref)
					ref = lv2_canvas_forge_lineTo(forge, canvas_urid, x, y + vr);
				if(ref)
					ref = lv2_canvas_forge_stroke(forge, canvas_urid);
			}

		}
		if(ref)
			lv2_atom_forge_pop(forge, &frame[1]);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &frame[0]);

	return ref;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->event_out->atom.size;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	props_idle(&handle->props, &handle->forge, 0, &handle->ref);
	xpress_pre(&handle->xpressI);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(!props_advance(&handle->props, &handle->forge, frames, obj, &handle->ref))
		{
			xpress_advance(&handle->xpressI, &handle->forge, frames, obj, &handle->ref);
		}
	}

	xpress_post(&handle->xpressI, nsamples-1);

	handle->counter += nsamples;

	if(handle->counter >= handle->overflowsec) // update every sec
	{
		if(handle->ref)
			handle->ref = _render(handle, nsamples-1);

		handle->counter -= handle->overflowsec;
	}
	else if(handle->needs_sync && (handle->counter >= handle->overflow) )
	{
		if(handle->ref)
			handle->ref = _render(handle, nsamples-1);

		handle->counter -= handle->overflow;
		handle->needs_sync = false;
	}

	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
	{
		if(handle->log)
			lv2_log_trace(&handle->logger, "%s: output buffer overflow\n", __func__);
		lv2_atom_sequence_clear(handle->event_out);
	}
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle)
	{
		xpress_deinit(&handle->xpressI);
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

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;

	return NULL;
}

const LV2_Descriptor monitor_out = {
	.URI						= ESPRESSIVO_MONITOR_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
