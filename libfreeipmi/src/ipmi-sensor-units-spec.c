/* 
   ipmi-sensor-units-spec.c - IPMI Sensor Units

   Copyright (C) 2003, 2004, 2005 FreeIPMI Core Team

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "freeipmi/ipmi-sensor-units-spec.h"

const char *const ipmi_sensor_units[] = 
  {
    "unspecified", 
    "degrees C", 
    "degrees F", 
    "degrees K", 
    "Volts", 
    "Amps", 
    "Watts", 
    "Joules", 
    "Coulombs", 
    "VA", 
    "Nits", 
    "Lumen", 
    "Lux", 
    "Candela", 
    "kPA", 
    "PSI", 
    "Newton", 
    "CFM", 
    "RPM", 
    "Hz", 
    "microsecond", 
    "millisecond", 
    "second", 
    "minute", 
    "hour", 
    "day", 
    "week", 
    "mil", 
    "inches", 
    "feet", 
    "cu in", 
    "cu feet", 
    "mm", 
    "cm", 
    "m", 
    "cu cm", 
    "cu m", 
    "liters", 
    "fluid Ounce", 
    "radians", 
    "steradians", 
    "revolutions", 
    "cycles", 
    "gravities", 
    "ounce", 
    "pound", 
    "ft-lb", 
    "oz-in", 
    "gauss", 
    "gilberts", 
    "henry", 
    "millihenry", 
    "farad", 
    "microfarad", 
    "ohms", 
    "siemens", 
    "mole", 
    "becquerel", 
    "PPM", 
    "reserved", 
    "Decibels", 
    "DbA", 
    "DbC", 
    "gray", 
    "sievert", 
    "color temp deg K", 
    "bit", 
    "kilobit", 
    "megabit", 
    "gigabit", 
    "byte", 
    "kilobyte", 
    "megabyte", 
    "gigabyte", 
    "word", 
    "dword", 
    "qword",
    "line", 
    "hit", 
    "miss", 
    "retry", 
    "reset", 
    "overrun / overflow", 
    "underrun", 
    "collision", 
    "packets", 
    "messages", 
    "characters", 
    "error", 
    "correctable Error", 
    "uncorrectable Error",
    "fatal Error",
    "grams",
    NULL
  };

const char *const ipmi_sensor_units_abbreviated[] = 
  {
    "unspecified",
    "C",
    "F",
    "K",
    "V",
    "A",
    "W",
    "J",
    "C",
    "VA",
    "nits",			/* ? */
    "lm",
    "lux",
    "cd",
    "KPA",
    "PSI",
    "N",
    "CFM",
    "RPM",
    "Hz",
    "us",
    "ms",
    "s",
    "min",
    "hr",
    "day",
    "wk",
    "mil",
    "in",
    "ft",
    "cu in",
    "cu feet",
    "mm",
    "cm",
    "m",
    "cu cm",
    "cu m",
    "L",
    "fl oz",
    "rad",
    "sr",
    "rev",
    "c",
    "gravities",		/* ? */
    "oz",
    "lb",
    "ft lB",
    "oz in",
    "G",
    "Gi",
    "H",
    "mH",
    "F",
    "uF",
    "ohms",			/* no abbreviation? */
    "S",
    "mol",
    "BecBquerel",
    "PPM",
    "Reserved",
    "dB",
    "DbA",
    "DbC",
    "Gy",
    "Sv",
    "color temp deg K",		/* ??? */
    "b",
    "Kb",
    "Mb",
    "Gb",
    "B",
    "KB",
    "MB",
    "GB",
    "word",
    "dword",
    "qword",
    "line",
    "hit",
    "miss",
    "retry",
    "reset",
    "overrun /overflow",
    "underrun",
    "collision",
    "pkts",
    "msgs",
    "chars",
    "err",
    "correctable err",
    "uncorrectable err",
    "fatal err",
    "g",
    NULL
  };

