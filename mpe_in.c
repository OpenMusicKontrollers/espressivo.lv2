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
#include <inttypes.h>
#include <math.h>

#include <espressivo.h>
#include <props.h>

#define MAX_NPROPS 1
#define MAX_NVOICES 64
#define MAX_ZONES 8
#define MAX_CHANNELS 16

typedef struct _zone_t zone_t;
typedef struct _target_t target_t;
typedef struct _handle_t handle_t;

struct _zone_t {
	uint8_t index;
	uint8_t offset;
	uint8_t width;
	uint8_t master_bend_range;
	uint8_t voice_bend_range;
	int16_t master_bender;
	int16_t voice_bender;
};

struct _target_t {
	uint8_t key;
	xpress_uuid_t uuid;

	/*FIXME
	xpress_state_t state;

	uint8_t pressure_lsb;
	uint8_t timbre_lsb;
	*/
};

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *midi_out;

	PROPS_T(props, MAX_NPROPS);
	int32_t num_zones;

	XPRESS_T(xpress, MAX_NVOICES);
	target_t target [MAX_NVOICES];

	uint16_t data;
	uint16_t rpn;

	zone_t zones [MAX_ZONES];
	zone_t *slots [MAX_CHANNELS];
};

static const props_def_t stat_mpe_zones = {
	.property = ESPRESSIVO_URI"#mpe_range",
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static inline void
_zone_init(zone_t *zone, uint8_t index, uint8_t offset, uint8_t width)
{
	zone->index = index;
	zone->offset = offset;
	zone->width = width;
	zone->master_bend_range = 2;
	zone->voice_bend_range = 48;
	zone->master_bender = 0;
	zone->voice_bender = 0;
}

static inline bool
_zone_is_master(zone_t *zone, uint8_t chan)
{
	return zone->offset == chan;
}

static inline bool
_zone_is_first_voice(zone_t *zone, uint8_t chan)
{
	return zone->offset + 1 == chan;
}

static inline bool
_zone_is_any_voice(zone_t *zone, uint8_t chan)
{
	return (zone->offset + 1 <= chan) && (zone->offset + zone->width >= chan);
}

static inline void
_slots_init(handle_t *handle)
{
	for(unsigned i=0; i<MAX_ZONES; i++)
	{
		zone_t *zone = &handle->zones[i];

		_zone_init(zone, i, i*2 + 1, i == 0 ? 7 : 0);
	}

	for(unsigned j=0; j<MAX_CHANNELS; j++)
		handle->slots[j] = &handle->zones[0];
}

static inline void
_slots_update(handle_t *handle)
{
	memset(handle->slots, 0x0, MAX_CHANNELS * sizeof(zone_t *));

	unsigned offset = 0;
	for(unsigned i=0; i<MAX_ZONES; i++)
	{
		zone_t *zone = &handle->zones[i];

		if(zone->width)
		{
			zone->offset = offset;
			offset += 1 + zone->width;

			for(unsigned j=0; j<=zone->width; j++)
				handle->slots[zone->offset + j] = zone;
		}
	}
}

static inline void
_slots_zone_set(handle_t *handle, uint8_t offset, uint8_t width)
{
	zone_t *zone = handle->slots[offset];

	if(zone)
	{
		for(unsigned idx=0; idx<zone->index; idx++)
		{
			zone_t *src = &handle->zones[idx];

			//FIXME
		}

		zone->offset = offset;
		zone->width = width;
	}
	else // !zone
	{
		for(unsigned i=0; i<MAX_ZONES; i++)
		{
			zone = &handle->zones[i];
			if(!zone->width)
			{
				zone->width = width;
				break;
			}
		}
	}

	_slots_update(handle);
}

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
		else if(!strcmp(features[i]->URI, ESPRESSIVO_URI"#voiceMap"))
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

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);

	lv2_atom_forge_init(&handle->forge, handle->map);
	
	if(!xpress_init(&handle->xpress, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_NONE, NULL, NULL, NULL) )
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

	if(props_register(&handle->props, &stat_mpe_zones, PROP_EVENT_NONE, NULL, &handle->num_zones))
	{
		props_sort(&handle->props);
	}
	else
	{
		free(handle);
		return NULL;
	}

	_slots_init(handle);

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
			handle->midi_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static inline LV2_Atom_Forge_Ref
_midi_event(handle_t *handle, int64_t frames, const uint8_t *m, size_t len)
{
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Ref ref;
		
	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = lv2_atom_forge_atom(forge, len, handle->uris.midi_MidiEvent);
	if(ref)
		ref = lv2_atom_forge_raw(forge, m, len);
	if(ref)
		lv2_atom_forge_pad(forge, len);

	return ref;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->midi_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->midi_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(obj->atom.type == handle->uris.midi_MidiEvent)
		{
			const uint8_t *m = LV2_ATOM_BODY_CONST(&obj->atom);
			const uint8_t comm = m[0] & 0xf0;
			const uint8_t chan = m[0] & 0x0f;

			if(comm == LV2_MIDI_MSG_NOTE_ON)
			{
				//FIXME
			}
			else if(comm == LV2_MIDI_MSG_NOTE_OFF)
			{
				//FIXME
			}
			else if(comm == LV2_MIDI_MSG_CHANNEL_PRESSURE)
			{
				//FIXME
			}
			else if(comm == LV2_MIDI_MSG_BENDER)
			{
				//FIXME
			}
			else if(comm == LV2_MIDI_MSG_CONTROLLER)
			{
				const uint8_t controller = m[1];
				const uint8_t value = m[2];

				switch(controller)
				{
					case LV2_MIDI_CTL_LSB_DATA_ENTRY:
						handle->data = (handle->data & 0x3f80) | value;
						break;
					case LV2_MIDI_CTL_MSB_DATA_ENTRY:
						handle->data = (handle->data & 0x7f) | (value << 7);

						if(handle->rpn == 6)
						{
							printf("setting zone width: %"PRIu8" %"PRIu16"\n", chan, handle->data >> 7);
							//TODO zone pitch bend range
						}
						else if(handle->rpn == 0)
						{
							printf("setting pitch bend range: %"PRIu8" %"PRIu16"\n", chan, handle->data >> 7);
							//TODO voice pitch bend range
						}
						break;
					case LV2_MIDI_CTL_RPN_LSB:
						handle->rpn = (handle->rpn & 0x3f80) | value;
						break;
					case LV2_MIDI_CTL_RPN_MSB:
						handle->rpn = (handle->rpn & 0x7f) | ((uint16_t)value << 7);
						break;
				}
			}
		}
		else
		{
			props_advance(&handle->props, forge, frames, obj, &handle->ref);
		}
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->midi_out);
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

const LV2_Descriptor mpe_in = {
	.URI						= ESPRESSIVO_MPE_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
