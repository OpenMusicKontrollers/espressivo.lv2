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

#define MAX_NPROPS 4
#define MAX_CHORDS 4

typedef struct _targetI_t targetI_t;
typedef struct _targetO_t targetO_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _targetI_t {
	xpress_uuid_t uuid [4];
};

struct _targetO_t {
	// empty
};

struct _plugstate_t {
	float offset [MAX_CHORDS];
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
};

#define CHORD_OFFSET(NUM) \
{ \
	.property = ESPRESSIVO_URI"#chord_offset_"#NUM, \
	.offset = offsetof(plugstate_t, offset) + (NUM-1)*sizeof(float), \
	.type = LV2_ATOM__Int, \
}

static const props_def_t defs [MAX_NPROPS] = {
	CHORD_OFFSET(1),
	CHORD_OFFSET(2),
	CHORD_OFFSET(3),
	CHORD_OFFSET(4)
};

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;
	LV2_Atom_Forge *forge = &handle->forge;

	for(unsigned i=0; i<MAX_CHORDS; i++)
	{
		targetO_t *dst = xpress_create(&handle->xpressO, &src->uuid[i]);
		(void)dst;

		xpress_state_t new_state = *state;
		new_state.pitch += handle->state.offset[i];

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, src->uuid[i], &new_state);
	}
}

static void
_set(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;
	LV2_Atom_Forge *forge = &handle->forge;

	for(unsigned i=0; i<MAX_CHORDS; i++)
	{
		xpress_state_t new_state = *state;
		new_state.pitch += handle->state.offset[i];

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, src->uuid[i], &new_state);
	}
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;
	LV2_Atom_Forge *forge = &handle->forge;

	for(unsigned i=0; i<MAX_CHORDS; i++)
	{
		xpress_free(&handle->xpressO, src->uuid[i]);
	}

	if(handle->ref)
		handle->ref = xpress_alive(&handle->xpressO, forge, frames);
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

	if(!voice_map)
		voice_map = &voice_map_fallback;

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

const LV2_Descriptor chord = {
	.URI						= ESPRESSIVO_CHORD_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
