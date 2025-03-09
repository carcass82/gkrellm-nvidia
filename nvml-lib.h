/*****************************************************************************
 * GKrellM nVidia                                                            *
 * A plugin for GKrellM showing nVidia GPU info using libNVML                *
 * Copyright (C) 2025 Carlo Casta <carlo.casta@gmail.com>                    *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *  
 * it under the terms of the GNU General Public License as published by      *
 * the Free Software Foundation; either version 2 of the License, or         *
 * (at your option) any later version.                                       *
 *                                                                           *
 * This program is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU General Public License for more details.                              *
 *                                                                           *
 * You should have received a copy of the GNU General Public License         *
 * along with this program; if not, write to the Free Software               *
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA *
 *                                                                           *
 *****************************************************************************/
typedef int boolean;
typedef unsigned int uint;
typedef unsigned long long uint64;

typedef enum { NVML_SUCCESS, NVML_ERROR_UNKNOWN = 999 } nvmlReturn_t;
typedef enum { NVML_CLOCK_GFX, NVML_CLOCK_MEM = 2 } nvmlClockType_t;
typedef enum { NVML_TEMP_GPU } nvmlSensors_t;

#define NVML_API_VERSION(name, ver) (uint)(sizeof(name) | (ver << 24u))

typedef void* nvmlDevice_t;

typedef struct {
	uint64 total;
	uint64 free;
	uint64 used;
} nvmlMemory_t;

typedef struct {
	uint gpu;
	uint memory;
} nvmlUsage_t;

typedef struct {
    uint version;
	uint fanidx;
    uint speed;
} nvmlFan_t;
#define nvmlFan_ver NVML_API_VERSION(nvmlFan_t, 1)

typedef struct {
	char busId[16];
	uint unused[9];
} nvmlPciInfo_t;

#define DECLARE_FUNCTION(f, ...) typedef nvmlReturn_t (*f ## _fn)(__VA_ARGS__)
DECLARE_FUNCTION(nvmlInit, void);
DECLARE_FUNCTION(nvmlShutdown, void);
DECLARE_FUNCTION(nvmlDeviceGetCount, uint*);
DECLARE_FUNCTION(nvmlDeviceGetHandleByIndex, uint, nvmlDevice_t*);
DECLARE_FUNCTION(nvmlDeviceGetName, nvmlDevice_t, char*, uint);
DECLARE_FUNCTION(nvmlDeviceGetClockInfo, nvmlDevice_t, nvmlClockType_t, uint*);
DECLARE_FUNCTION(nvmlDeviceGetTemperature, nvmlDevice_t, nvmlSensors_t, uint*);
DECLARE_FUNCTION(nvmlDeviceGetFanSpeed, nvmlDevice_t, uint*);
DECLARE_FUNCTION(nvmlDeviceGetPowerUsage, nvmlDevice_t, uint*);
DECLARE_FUNCTION(nvmlDeviceGetUtilizationRates, nvmlDevice_t, nvmlUsage_t*);
DECLARE_FUNCTION(nvmlDeviceGetMemoryInfo, nvmlDevice_t, nvmlMemory_t*);
DECLARE_FUNCTION(nvmlDeviceGetPciInfo, nvmlDevice_t, nvmlPciInfo_t*);
DECLARE_FUNCTION(nvmlDeviceGetNumFans, nvmlDevice_t, uint*);
DECLARE_FUNCTION(nvmlDeviceGetFanSpeedRPM, nvmlDevice_t, nvmlFan_t*);
#undef DECLARE_FUNCTION

typedef struct {
	char path[512];
	void *handle;
	boolean valid;

	nvmlInit_fn nvmlInit;
	nvmlShutdown_fn nvmlShutdown;
	nvmlDeviceGetCount_fn nvmlDeviceGetCount;
	nvmlDeviceGetHandleByIndex_fn nvmlDeviceGetHandleByIndex;
	nvmlDeviceGetName_fn nvmlDeviceGetName;
	nvmlDeviceGetClockInfo_fn nvmlDeviceGetClockInfo;
	nvmlDeviceGetTemperature_fn nvmlDeviceGetTemperature;
	nvmlDeviceGetFanSpeed_fn nvmlDeviceGetFanSpeed;
	nvmlDeviceGetPowerUsage_fn nvmlDeviceGetPowerUsage;
	nvmlDeviceGetUtilizationRates_fn nvmlDeviceGetUtilizationRates;
	nvmlDeviceGetMemoryInfo_fn nvmlDeviceGetMemoryInfo;
	nvmlDeviceGetPciInfo_fn nvmlDeviceGetPciInfo;
	nvmlDeviceGetNumFans_fn nvmlDeviceGetNumFans;
	nvmlDeviceGetFanSpeedRPM_fn nvmlDeviceGetFanSpeedRPM;
} GKNVMLLib;

boolean initialize_gpulib(GKNVMLLib *lib);
boolean reinitialize_gpulib(GKNVMLLib *lib);
void shutdown_gpulib(GKNVMLLib *lib);
boolean is_valid_gpulib(GKNVMLLib *lib);
boolean is_valid_gpulib_path(char *path);
