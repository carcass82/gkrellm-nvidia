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
#include <dlfcn.h>

#ifndef	FALSE
 #define FALSE (0)
#endif
#ifndef	TRUE
 #define TRUE (!FALSE)
#endif
#ifndef	NULL
 #define NULL (0)
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

boolean is_valid_gpulib_path(char* path)
{
	boolean res = FALSE;
	void* tmp_handle = NULL;
	nvmlInit_fn tmp_fn = NULL;
	static const char INIT_FN_NAME[] = "nvmlInit";

	tmp_handle = (path && path[0] != '\0')? dlopen(path, RTLD_LAZY) : NULL;
	tmp_fn = (tmp_handle)? (nvmlInit_fn)dlsym(tmp_handle, INIT_FN_NAME) : NULL;
	res = (tmp_fn != NULL && dlerror() == NULL);
	
	if (tmp_handle)
		dlclose(tmp_handle);

	return res;
}

boolean is_valid_gpulib(GKNVMLLib* lib)
{
	return lib && lib->handle;
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

		res = (dlerror() == NULL && NVCHECK(lib->nvmlInit()));
	}

	return res;
}

boolean reinitialize_gpulib(GKNVMLLib* lib)
{
	shutdown_gpulib(lib);
	return initialize_gpulib(lib);
}
