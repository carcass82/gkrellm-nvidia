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
#include "nvml-lib.h"
#include <string.h>
#include <dlfcn.h>

#ifndef	FALSE
 #define FALSE (0)
#endif
#ifndef	TRUE
 #define TRUE (!FALSE)
#endif

#define NVCHECK(fn) (fn == NVML_SUCCESS)

void shutdown_gpulib(GKNVMLLib* lib)
{
	if (lib && lib->handle && lib->nvmlShutdown)
	{
		lib->nvmlShutdown();
		dlclose(lib->handle);
		
        lib->handle = NULL;
	}
}

boolean is_valid_gpulib(char* path)
{
	void* temp_handle = NULL;
	nvmlInit_fn temp_fn = NULL;

	temp_handle = (strlen(path) > 0)? dlopen(path, RTLD_LAZY) : NULL;
	temp_fn = (temp_handle != NULL)? (nvmlInit_fn)dlsym(temp_handle, "nvmlInit") : NULL;

	return (temp_fn != NULL && dlerror() == NULL);
}

boolean initialize_gpulib(GKNVMLLib* lib)
{
	boolean res = FALSE;

	lib->handle = dlopen(lib->path, RTLD_LAZY);
	if (lib->handle)
	{
        #define BIND_FUNCTION(fun) fun = (fun ## _fn)dlsym(lib->handle, #fun)
		lib->BIND_FUNCTION(nvmlInit);
		lib->BIND_FUNCTION(nvmlShutdown);
		lib->BIND_FUNCTION(nvmlDeviceGetCount);
		lib->BIND_FUNCTION(nvmlDeviceGetHandleByIndex);
		lib->BIND_FUNCTION(nvmlDeviceGetName);
		lib->BIND_FUNCTION(nvmlDeviceGetClockInfo);
		lib->BIND_FUNCTION(nvmlDeviceGetTemperature);
		lib->BIND_FUNCTION(nvmlDeviceGetFanSpeed);
		lib->BIND_FUNCTION(nvmlDeviceGetPowerUsage);
		lib->BIND_FUNCTION(nvmlDeviceGetUtilizationRates);
		lib->BIND_FUNCTION(nvmlDeviceGetMemoryInfo);
        #undef BIND_FUNCTION

		lib->initialized = res = (dlerror() == NULL && NVCHECK(lib->nvmlInit()));
	}

	return res;
}

boolean reinitialize_gpulib(GKNVMLLib* lib)
{
	shutdown_gpulib(lib);
	return initialize_gpulib(lib);
}
