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

#ifndef _ESPRESSIVO_LV2_H
#define _ESPRESSIVO_LV2_H

#include <math.h>
#include <stdlib.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>

#define _ATOM_ALIGNED __attribute__((aligned(8)))

#if defined(HAS_BUILTIN_ASSUME_ALIGNED)
#	define ASSUME_ALIGNED(PTR) __builtin_assume_aligned((PTR), 8)
#else
#	define ASSUME_ALIGNED(PTR) (PTR)
#endif


// bundle uri
#define ESPRESSIVO_URI							"http://open-music-kontrollers.ch/lv2/espressivo"

// event uri
#define ESPRESSIVO_EVENT_URI				ESPRESSIVO_URI"#Event"

// state uris
#define ESPRESSIVO_STATE_ON_URI			ESPRESSIVO_URI"#on"
#define ESPRESSIVO_STATE_SET_URI		ESPRESSIVO_URI"#set"
#define ESPRESSIVO_STATE_OFF_URI		ESPRESSIVO_URI"#off"
#define ESPRESSIVO_STATE_IDLE_URI		ESPRESSIVO_URI"#idle"

// plugin uris
#define ESPRESSIVO_TUIO2_IN_URI			ESPRESSIVO_URI"#tuio2_in"
#define ESPRESSIVO_MIDI_IN_URI			ESPRESSIVO_URI"#midi_in"
#define ESPRESSIVO_MPE_OUT_URI			ESPRESSIVO_URI"#mpe_out"
#define ESPRESSIVO_SNH_URI					ESPRESSIVO_URI"#snh"
#define ESPRESSIVO_SC_OUT_URI				ESPRESSIVO_URI"#sc_out"

extern const LV2_Descriptor tuio2_in;
extern const LV2_Descriptor midi_in;
extern const LV2_Descriptor mpe_out;
extern const LV2_Descriptor snh;
extern const LV2_Descriptor sc_out;

// bundle enums and structs
typedef enum _espressivo_state_t		espressivo_state_t;
typedef struct _espressivo_event_t	espressivo_event_t;
typedef struct _espressivo_obj_t		espressivo_obj_t;
typedef struct _espressivo_pack_t		espressivo_pack_t;
typedef struct _espressivo_forge_t	espressivo_forge_t;
typedef struct _espressivo_dict_t		espressivo_dict_t;

enum _espressivo_state_t {
	ESPRESSIVO_STATE_ON		= (1 << 1),
	ESPRESSIVO_STATE_SET	= (1 << 2),
	ESPRESSIVO_STATE_OFF	= (1 << 3),
	ESPRESSIVO_STATE_IDLE	= (1 << 4),
};

struct _espressivo_event_t {
	espressivo_state_t state;
	uint32_t sid;
	uint32_t gid;
	float dim [4];
};

struct _espressivo_obj_t {
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body prop _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _espressivo_pack_t {
	espressivo_obj_t cobj _ATOM_ALIGNED;

	LV2_Atom_Int sid _ATOM_ALIGNED;
	LV2_Atom_Int gid _ATOM_ALIGNED;
	LV2_Atom_Float dim0 _ATOM_ALIGNED;
	LV2_Atom_Float dim1 _ATOM_ALIGNED;
	LV2_Atom_Float dim2 _ATOM_ALIGNED;
	LV2_Atom_Float dim3 _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _espressivo_forge_t {
	LV2_Atom_Forge forge;

	struct {
		LV2_URID event;

		LV2_URID on;
		LV2_URID set;
		LV2_URID off;
		LV2_URID idle;
	} uris;
};

struct _espressivo_dict_t {
	uint32_t sid;
	void *ref;
};

static inline float
_midi2cps(float pitch)
{
	return exp2f( (pitch - 69.f) / 12.f) * 440.f;
}

static inline float
_cps2midi(float cps)
{
	return log2f(cps / 440.f) * 12.f + 69.f;
}

static inline void
espressivo_forge_init(espressivo_forge_t *cforge, LV2_URID_Map *map)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	cforge->uris.event = map->map(map->handle, ESPRESSIVO_EVENT_URI);

	cforge->uris.on = map->map(map->handle, ESPRESSIVO_STATE_ON_URI);
	cforge->uris.set = map->map(map->handle, ESPRESSIVO_STATE_SET_URI);
	cforge->uris.off = map->map(map->handle, ESPRESSIVO_STATE_OFF_URI);
	cforge->uris.idle = map->map(map->handle, ESPRESSIVO_STATE_IDLE_URI);

	lv2_atom_forge_init(forge, map);
}

// event handle 
static inline LV2_Atom_Forge_Ref
espressivo_event_forge(espressivo_forge_t *cforge, const espressivo_event_t *ev)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	uint32_t otype = 0;
	switch(ev->state)
	{
		case ESPRESSIVO_STATE_ON:
			otype = cforge->uris.on;
			break;
		case ESPRESSIVO_STATE_SET:
			otype = cforge->uris.set;
			break;
		case ESPRESSIVO_STATE_OFF:
			otype = cforge->uris.off;
			break;
		case ESPRESSIVO_STATE_IDLE:
			otype = cforge->uris.idle;
			break;
	}

	const espressivo_pack_t pack = {
		.cobj = {
			.obj = {
				.atom.type = forge->Object,
				.atom.size = sizeof(espressivo_pack_t) - sizeof(LV2_Atom),
				.body.id = 0,
				.body.otype = otype
			},
			.prop = {
				.key = otype,
				.context = 0,
				.value.type = forge->Tuple,
				.value.size = sizeof(espressivo_pack_t) - sizeof(LV2_Atom_Object) - sizeof(LV2_Atom_Property_Body)
			}
		},
		.sid = {
			.atom.size = sizeof(int32_t),
			.atom.type = forge->Int,
			.body = ev->sid
		},
		.gid = {
			.atom.size = sizeof(int32_t),
			.atom.type = forge->Int,
			.body = ev->gid
		},
		.dim0 = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->dim[0]
		},
		.dim1 = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->dim[1]
		},
		.dim2 = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->dim[2]
		},
		.dim3 = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->dim[3]
		}
	};

	return lv2_atom_forge_raw(forge, &pack, sizeof(espressivo_pack_t));
}

static inline int
espressivo_event_check_type(const espressivo_forge_t *cforge, const LV2_Atom *atom)
{
	const LV2_Atom_Forge *forge = &cforge->forge;
	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);

	if(lv2_atom_forge_is_object_type(forge, obj->atom.type)
			&& ( (obj->body.otype == cforge->uris.on)
				|| (obj->body.otype == cforge->uris.off)
				|| (obj->body.otype == cforge->uris.set)
				|| (obj->body.otype == cforge->uris.idle) ) )
	{
		return 1;
	}
	
	return 0;
}

static inline void
espressivo_event_deforge(const espressivo_forge_t *cforge, const LV2_Atom *atom,
	espressivo_event_t *ev)
{
	const espressivo_pack_t *pack = ASSUME_ALIGNED(atom);

	uint32_t otype = pack->cobj.obj.body.otype;

	if(otype == cforge->uris.on)
		ev->state = ESPRESSIVO_STATE_ON;
	else if(otype == cforge->uris.set)
		ev->state = ESPRESSIVO_STATE_SET;
	else if(otype == cforge->uris.off)
		ev->state = ESPRESSIVO_STATE_OFF;
	else
		ev->state = ESPRESSIVO_STATE_IDLE;

	ev->sid = pack->sid.body;
	ev->gid = pack->gid.body;
	ev->dim[0] = pack->dim0.body;
	ev->dim[1] = pack->dim1.body;
	ev->dim[2] = pack->dim2.body;
	ev->dim[3] = pack->dim3.body;
}

#if !defined(ESPRESSIVO_DICT_SIZE)
#	define ESPRESSIVO_DICT_SIZE 16
#endif

#define ESPRESSIVO_DICT_INIT(DICT, REF) \
({ \
	for(unsigned _i=0; _i<ESPRESSIVO_DICT_SIZE; _i++) \
	{ \
		(DICT)[_i].sid = 0; \
		(DICT)[_i].ref = (REF) + _i; \
	} \
})

#define ESPRESSIVO_DICT_FOREACH(DICT, SID, REF) \
	for(unsigned _i=0; _i<ESPRESSIVO_DICT_SIZE; _i++) \
		if( (((SID) = (DICT)[_i].sid) != 0) && ((REF) = (DICT)[_i].ref) )

static inline void
espressivo_dict_clear(espressivo_dict_t *dict)
{
	for(unsigned i=0; i<ESPRESSIVO_DICT_SIZE; i++)
		dict[i].sid = 0;
}

static inline void *
espressivo_dict_add(espressivo_dict_t *dict, uint32_t sid)
{
	for(unsigned i=0; i<ESPRESSIVO_DICT_SIZE; i++)
		if(dict[i].sid == 0)
		{
			dict[i].sid = sid;
			return dict[i].ref;
		}

	return NULL;
}

static inline void *
espressivo_dict_del(espressivo_dict_t *dict, uint32_t sid)
{
	for(unsigned i=0; i<ESPRESSIVO_DICT_SIZE; i++)
		if(dict[i].sid == sid)
		{
			dict[i].sid = 0;
			return dict[i].ref;
		}

	return NULL;
}

static inline void *
espressivo_dict_ref(espressivo_dict_t *dict, uint32_t sid)
{
	for(unsigned i=0; i<ESPRESSIVO_DICT_SIZE; i++)
		if(dict[i].sid == sid)
			return dict[i].ref;

	return NULL;
}

typedef struct _espressivo_voice_t espressivo_voice_t;
typedef struct _espressivo_inst_t espressivo_inst_t;

struct _espressivo_voice_t {
	int32_t sid;
	void *data;
};

struct _espressivo_inst_t {
	espressivo_forge_t cforge; 
	unsigned max_voices;
	unsigned num_voices;
	espressivo_voice_t voices [0];
};

#define INST_T(INST, NVOICES) \
	espressivo_inst_t (INST); \
	espressivo_voice_t _voices [(NVOICES)];

// rt-safe
static inline void
espressivo_inst_init(espressivo_inst_t *inst, void *data, size_t data_size, unsigned max_voices)
{
	inst->max_voices = max_voices;

	for(unsigned i=0; i<max_voices; i++)
	{
		espressivo_voice_t *voice = &inst->voices[i];
		voice->sid = INT32_MAX; // inactive voice
		voice->data = data + i*data_size;
	}
}

static int
_voice_sort(const void *itm1, const void *itm2)
{
	const espressivo_voice_t *voice1 = itm1;
	const espressivo_voice_t *voice2 = itm2;

	if(voice1->sid < voice2->sid)
		return -1;
	else if(voice1->sid > voice2->sid)
		return 1;

	return 0;
}

static inline void *
espressivo_inst_voice_get(espressivo_inst_t *inst, uint32_t sid)
{
	const espressivo_voice_t tmp = {
		.sid = sid,
		.data = NULL
	};

	espressivo_voice_t *voice = bsearch(&tmp, inst->voices, inst->num_voices, sizeof(espressivo_voice_t), _voice_sort);

	if(voice)
		return voice->data;

	return NULL;
}

static inline void * 
espressivo_inst_voice_add(espressivo_inst_t *inst, uint32_t sid)
{
	if(inst->num_voices < inst->max_voices)
	{
		espressivo_voice_t *voice = &inst->voices[inst->num_voices];
		voice->sid = sid;

		inst->num_voices += 1;
		qsort(inst->voices, inst->num_voices, sizeof(espressivo_voice_t), _voice_sort);

		return voice->data;
	}

	return NULL;
}

static inline void * 
espressivo_inst_voice_del(espressivo_inst_t *inst, uint32_t sid)
{
	const espressivo_voice_t tmp = {
		.sid = sid,
		.data = NULL
	};

	espressivo_voice_t *voice = bsearch(&tmp, inst->voices, inst->num_voices, sizeof(espressivo_voice_t), _voice_sort);

	if(voice)
	{
		voice->sid = INT32_MAX; // inactive voice

		qsort(inst->voices, inst->num_voices, sizeof(espressivo_voice_t), _voice_sort);
		inst->num_voices -= 1;

		return voice->data;
	}

	return NULL;
}

// rt-safe
static inline int
espressivo_inst_advance(espressivo_inst_t *inst, LV2_Atom_Forge *forge, int64_t frames, const LV2_Atom_Object *obj)
{
	if(espressivo_event_check_type(&inst->cforge, &obj->atom))
	{
		espressivo_event_t cev;
		espressivo_event_deforge(&inst->cforge, &obj->atom, &cev);

		switch(cev.state)
		{
			case ESPRESSIVO_STATE_ON:
			{
				void *voc = espressivo_inst_voice_add(inst, cev.sid);
				break;
			}
			case ESPRESSIVO_STATE_SET:
			{
				void *voc = espressivo_inst_voice_get(inst, cev.sid);
				break;
			}
			case ESPRESSIVO_STATE_OFF:
			{
				void *voc = espressivo_inst_voice_del(inst, cev.sid);
				break;
			}
			case ESPRESSIVO_STATE_IDLE:
			{
				//TODO
				break;
			}
		}

		return 1; // handled
	}

	return 0; // not handled
}

#define ESPRESSIVO_INST_VOICES_FOREACH(INST, VOICE) \
	for(espressivo_voice_t *VOICE=(INST)->voices; VOICE - (INST)->voices < (INST)->num_voices; VOICE++)

#endif // _ESPRESSIVO_LV2_H
