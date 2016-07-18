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

#ifndef _MPE_LV2_H
#define _MPE_LV2_H

#define MPE_CHAN_MAX 16
#define MPE_ZONE_MAX (MPE_CHAN_MAX / 2)

typedef struct _zone_t zone_t;
typedef struct _mpe_t mpe_t;

struct _zone_t {
	uint8_t base;
	uint8_t span;
	uint8_t ref;
	uint8_t master_range;
	uint8_t voice_range;
};

struct _mpe_t {
	uint8_t n_zones;
	zone_t zones [MPE_ZONE_MAX];
	int8_t channels [MPE_CHAN_MAX];
};

#endif
