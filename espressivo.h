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

#include <xpress.lv2/xpress.h>

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

#ifdef LV2_ATOM_TUPLE_FOREACH
#	undef LV2_ATOM_TUPLE_FOREACH
#	define LV2_ATOM_TUPLE_FOREACH(tuple, iter) \
	for (LV2_Atom* (iter) = lv2_atom_tuple_begin(tuple); \
	     !lv2_atom_tuple_is_end(LV2_ATOM_BODY(tuple), (tuple)->atom.size, (iter)); \
	     (iter) = lv2_atom_tuple_next(iter))
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
#define ESPRESSIVO_TUIO2_OUT_URI		ESPRESSIVO_URI"#tuio2_out"
#define ESPRESSIVO_MIDI_IN_URI			ESPRESSIVO_URI"#midi_in"
#define ESPRESSIVO_MIDI_OUT_URI			ESPRESSIVO_URI"#midi_out"
#define ESPRESSIVO_MPE_IN_URI				ESPRESSIVO_URI"#mpe_in"
#define ESPRESSIVO_MPE_OUT_URI			ESPRESSIVO_URI"#mpe_out"
#define ESPRESSIVO_SNH_URI					ESPRESSIVO_URI"#snh"
#define ESPRESSIVO_THROUGH_URI			ESPRESSIVO_URI"#through"
#define ESPRESSIVO_MODULATOR_URI		ESPRESSIVO_URI"#modulator"
#define ESPRESSIVO_REDIRECTOR_URI		ESPRESSIVO_URI"#redirector"
#define ESPRESSIVO_CHORD_URI				ESPRESSIVO_URI"#chord"
#define ESPRESSIVO_REDUCTO_URI			ESPRESSIVO_URI"#reducto"
#define ESPRESSIVO_DISCRETO_URI			ESPRESSIVO_URI"#discreto"
#define ESPRESSIVO_SC_OUT_URI				ESPRESSIVO_URI"#sc_out"
#define ESPRESSIVO_SQEW_URI					ESPRESSIVO_URI"#sqew"
#define ESPRESSIVO_MONITOR_OUT_URI	ESPRESSIVO_URI"#monitor_out"

#define MAX_NVOICES 64

extern const LV2_Descriptor tuio2_in;
extern const LV2_Descriptor tuio2_out;
extern const LV2_Descriptor midi_in;
extern const LV2_Descriptor midi_out;
extern const LV2_Descriptor mpe_in;
extern const LV2_Descriptor mpe_out;
extern const LV2_Descriptor snh;
extern const LV2_Descriptor through;
extern const LV2_Descriptor modulator;
extern const LV2_Descriptor redirector;
extern const LV2_Descriptor chord;
extern const LV2_Descriptor reducto;
extern const LV2_Descriptor discreto;
extern const LV2_Descriptor sc_out;
extern const LV2_Descriptor sqew;
extern const LV2_Descriptor monitor_out;

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

#endif // _ESPRESSIVO_LV2_H
