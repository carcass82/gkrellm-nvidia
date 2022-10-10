/*****************************************************************************
 * GKrellM nVidia                                                            *
 * A plugin for GKrellM showing nVidia GPU info using libxnvctrl             *
 * Copyright (C) 2019 Carlo Casta <carlo.casta@gmail.com>                    *
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

#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <gkrellm2/gkrellm.h>

/* minimum definitions required to link with NVML - begin */
#define NVML_SUCCESS 0
#define NVML_ERROR_UNKNOWN 999

#define NVML_DEVICE_NAME_BUFFER_SIZE 64

#define NVML_CLOCK_GRAPHICS 0
#define NVML_TEMPERATURE_GPU 0

typedef struct nvmlDevice* nvmlDevice_t;
typedef int nvmlReturn_t;
typedef int nvmlClockType_t;
typedef int nvmlTemperatureSensors_t;

nvmlReturn_t nvmlInit();
nvmlReturn_t nvmlShutdown();
nvmlReturn_t nvmlDeviceGetCount(unsigned int* deviceCount);
nvmlReturn_t nvmlDeviceGetHandleByIndex(unsigned int index, nvmlDevice_t* device);
nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t device, char* name, unsigned int length);
nvmlReturn_t nvmlDeviceGetClockInfo(nvmlDevice_t device, nvmlClockType_t type, unsigned int* clock);
nvmlReturn_t nvmlDeviceGetTemperature(nvmlDevice_t device, nvmlTemperatureSensors_t sensorType, unsigned int* temp);
nvmlReturn_t nvmlDeviceGetFanSpeed(nvmlDevice_t device, unsigned int* speed);
/* minimum definitions required to link with NVML - end */


#define GKFREQ_MAX_GPUS 4

static GkrellmMonitor *monitor;
static GkrellmPanel *panel;
static GkrellmDecal *decal_text[GKFREQ_MAX_GPUS * 8];
static int style_id;
static int system_gpu_count;

#define GPU_CLOCK 1
#define GPU_TEMP  2
#define GPU_FAN   3

static int max(int a, int b)
{
	return (a > b)? a : b;
}

static int min(int a, int b)
{
	return (a > b)? b : a;
}

static int clamp(int v, int a, int b)
{
	return min(max(v, a), b);
}

static int get_gpu_count()
{
	nvmlReturn_t res = NVML_ERROR_UNKNOWN;
	unsigned int gpu_count = 0;

	if (nvmlInit() != NVML_SUCCESS) {

		gkrellm_debug(G_LOG_LEVEL_ERROR, "NVML failed to load");

		nvmlShutdown();

		return 0;
	}

	res = nvmlDeviceGetCount(&gpu_count);

	return (res == NVML_SUCCESS)? min(GKFREQ_MAX_GPUS, gpu_count) : 0;
}

static void get_gpu_name(int i, char** gpu_name)
{
	static char gpu_string[GKFREQ_MAX_GPUS][NVML_DEVICE_NAME_BUFFER_SIZE] = {'\0'};
	nvmlReturn_t res = NVML_ERROR_UNKNOWN;
	nvmlDevice_t device;

	if (gpu_string[i][0] == '\0') {

		res = nvmlDeviceGetHandleByIndex(i, &device);

		if (res == NVML_SUCCESS)
			res = nvmlDeviceGetName(device, gpu_string[i], NVML_DEVICE_NAME_BUFFER_SIZE);

		if (res != NVML_SUCCESS)
			strcpy(gpu_string[i], "N/A");

	}

	*gpu_name = gpu_string[i];
}

static int get_gpu_data(int gpu_id, int info, char *buf, int buf_size)
{
	nvmlReturn_t res = NVML_ERROR_UNKNOWN;
	unsigned uint_attribute = -1;
	nvmlDevice_t device;

	if (nvmlDeviceGetHandleByIndex(gpu_id, &device) == NVML_SUCCESS) {

		switch (info)
		{
		case GPU_CLOCK:
			res = nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &uint_attribute);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%uMHz", uint_attribute);
			break;

		case GPU_TEMP:
			res = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &uint_attribute);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%.1fC", (float)uint_attribute);
			break;

		case GPU_FAN:
			res = nvmlDeviceGetFanSpeed(device, &uint_attribute);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%d%%", clamp(uint_attribute, 0, 100));
			break;
		}

	}

	return (res == NVML_SUCCESS)? 0 : -1;
}

static gint panel_expose_event(GtkWidget* widget, GdkEventExpose* ev)
{
	gdk_draw_pixmap(widget->window,
	                widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
	                panel->pixmap,
	                ev->area.x,
	                ev->area.y,
	                ev->area.x,
	                ev->area.y,
	                ev->area.width,
	                ev->area.height);

	return 0;
}

static void update_plugin()
{
	GkrellmStyle *style = gkrellm_panel_style(style_id);
	GkrellmMargin *m = gkrellm_get_style_margins(style);
	int w = gkrellm_chart_width();
	int w_text;

	static char* gpu_string;
	static char clock_label[] = "GPUN Clock:";
	static char clock_string[64] = "N/A";
	static char temp_label[] = "GPUN Temp:";
	static char temp_string[64] = "N/A";
	static char fan_label[] = "GPUN Fan:";
	static char fan_string[64] = "N/A";

	for (int i = 0; i < system_gpu_count; ++i) {

		get_gpu_name(i, &gpu_string);
		w_text = gdk_string_width(
			gdk_font_from_description(decal_text[i * 8 + 1]->text_style.font),
			gpu_string);
		decal_text[i * 8 + 1]->x = (w - w_text) / 2 - 1;

		get_gpu_data(i, GPU_CLOCK, clock_string, 64);
		w_text = gdk_string_width(
			gdk_font_from_description(decal_text[i * 8 + 3]->text_style.font),
			clock_string);
		decal_text[i * 8 + 3]->x = w - m->left - m->right - w_text - 1;

		get_gpu_data(i, GPU_TEMP, temp_string, 64);
		w_text = gdk_string_width(
			gdk_font_from_description(decal_text[i * 8 + 5]->text_style.font),
			temp_string);
		decal_text[i * 8 + 5]->x = w - m->left - m->right - w_text - 1;

		get_gpu_data(i, GPU_FAN, fan_string, 64);
		w_text = gdk_string_width(
			gdk_font_from_description(decal_text[i * 8 + 7]->text_style.font),
			fan_string);
		decal_text[i * 8 + 7]->x = w - m->left - m->right - w_text - 1;

		/* replace the N in "GPUN <attribute>" string */
		clock_label[3] = temp_label[3] = fan_label[3] = i + '0';

		gkrellm_draw_decal_text(panel, decal_text[i * 8 + 1], gpu_string, 0);

		gkrellm_draw_decal_text(panel, decal_text[i * 8 + 2], clock_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[i * 8 + 3], clock_string, 0);

		gkrellm_draw_decal_text(panel, decal_text[i * 8 + 4], temp_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[i * 8 + 5], temp_string, 0);
		
		gkrellm_draw_decal_text(panel, decal_text[i * 8 + 6], fan_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[i * 8 + 7], fan_string, 0);
	}

	gkrellm_draw_panel_layers(panel);
}

static void create_plugin(GtkWidget* vbox, gint first_create)
{
	GkrellmStyle *style;
	GkrellmTextstyle *ts;

	if (first_create)
		panel = gkrellm_panel_new0();
	style = gkrellm_meter_style(style_id);
	ts = gkrellm_meter_textstyle(style_id);

	system_gpu_count = get_gpu_count();

	for (int y = -1, idx = 0; idx < system_gpu_count; ++idx) {

		decal_text[idx * 8 + 0] = gkrellm_create_decal_text(panel,
		                                                    "",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		decal_text[idx * 8 + 1] = gkrellm_create_decal_text(panel,
		                                                    "GPU NAME",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		y = max(decal_text[idx * 8 + 0]->y, decal_text[idx * 8 + 1]->y) +
		    max(decal_text[idx * 8 + 0]->h, decal_text[idx * 8 + 1]->h) + 5;

		decal_text[idx * 8 + 2] = gkrellm_create_decal_text(panel,
		                                                    "GPU8 Clock:",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		decal_text[idx * 8 + 3] = gkrellm_create_decal_text(panel,
		                                                    "8888MHz",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);
		
		y = max(decal_text[idx * 8 + 2]->y, decal_text[idx * 8 + 3]->y) +
		    max(decal_text[idx * 8 + 2]->h, decal_text[idx * 8 + 3]->h) + 1;
		
		decal_text[idx * 8 + 4] = gkrellm_create_decal_text(panel,
		                                                    "GPU8 Temp:",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		decal_text[idx * 8 + 5] = gkrellm_create_decal_text(panel,
		                                                    "88.8C",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);
		
		y = max(decal_text[idx * 8 + 4]->y, decal_text[idx * 8 + 5]->y) +
		    max(decal_text[idx * 8 + 4]->h, decal_text[idx * 8 + 5]->h) + 1;
		
		decal_text[idx * 8 + 6] = gkrellm_create_decal_text(panel,
		                                                    "GPU8 Fan:",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		decal_text[idx * 8 + 7] = gkrellm_create_decal_text(panel,
		                                                    "8888RPM",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		y = max(decal_text[idx * 8 + 6]->y, decal_text[idx * 8 + 7]->y) +
		    max(decal_text[idx * 8 + 6]->h, decal_text[idx * 8 + 7]->h) + 1;

		/* next GPU infos */
		y += ((idx == system_gpu_count - 1)? 1 : 10);
	}
	gkrellm_panel_configure(panel, NULL, style);
	gkrellm_panel_create(vbox, monitor, panel);

	if (first_create) {
		g_signal_connect(G_OBJECT(panel->drawing_area),
		                 "expose_event",
		                 G_CALLBACK(panel_expose_event),
		                 NULL);
	}

}

static GkrellmMonitor plugin_mon =
{
	"nvidia",                    /* Name, for config tab.                    */
	0,                           /* Id,  0 if a plugin                       */
	create_plugin,               /* The create_plugin() function             */
	update_plugin,               /* The update_plugin() function             */
	NULL,                        /* The create_plugin_tab() config function  */
	NULL,                        /* The apply_plugin_config() function       */

	NULL,                        /* The save_plugin_config() function        */
	NULL,                        /* The load_plugin_config() function        */
	NULL,                        /* config keyword                           */

	NULL,                        /* Undefined 2                              */
	NULL,                        /* Undefined 1                              */
	NULL,                        /* Undefined 0                              */

	MON_CPU | MON_INSERT_AFTER,  /* Insert plugin before this monitor.       */
	NULL,                        /* Handle if a plugin, filled in by GKrellM */
	NULL                         /* path if a plugin, filled in by GKrellM   */
};

GkrellmMonitor* gkrellm_init_plugin()
{
	style_id = gkrellm_add_meter_style(&plugin_mon, "nvidia");
	monitor = &plugin_mon;

	return monitor;
}
