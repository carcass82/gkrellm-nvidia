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
#include <gkrellm2/gkrellm.h>

#include <X11/Xlib.h>
#include <NVCtrl/NVCtrl.h>
#include <NVCtrl/NVCtrlLib.h>

#define GKFREQ_MAX_GPUS 4

static Display *display;

static GkrellmMonitor *monitor;
static GkrellmPanel *panel;
static GkrellmDecal *decal_text[GKFREQ_MAX_GPUS * 6];
static int style_id;
static int system_gpu_count;

#define GPU_CLOCK 1
#define GPU_TEMP  2
#define GPU_FAN   3

static int get_gpu_count()
{
	int event_basep, error_basep;
	Bool res;
	int gpu_count = 0;

	display = XOpenDisplay(NULL);

	if (!display || XNVCTRLQueryExtension(display, &event_basep, &error_basep) != True) {

		if (display)
			XCloseDisplay(display);

		return 0;
	}

	res = XNVCTRLQueryTargetCount(display, NV_CTRL_TARGET_TYPE_GPU, &gpu_count);
	return (res == True)? gpu_count : 0;
}

static int get_gpu_data(int gpu_id, int info, char *buf, int buf_size)
{
	int int_attribute;
	
	switch (info)
	{
	case GPU_CLOCK:
		/*
		 * NV_CTRL_GPU_CURRENT_CLOCK_FREQS - query the current GPU and memory
		 * clocks of the graphics device driving the X screen.
		 *
		 * NV_CTRL_GPU_CURRENT_CLOCK_FREQS is a "packed" integer attribute;
		 * the GPU clock is stored in the upper 16 bits of the integer, and
		 * the memory clock is stored in the lower 16 bits of the integer.
		 * All clock values are in MHz.  All clock values are in MHz.
		 */
		XNVCTRLQueryTargetAttribute(display, NV_CTRL_TARGET_TYPE_GPU, gpu_id, 0, NV_CTRL_GPU_CURRENT_CLOCK_FREQS, &int_attribute);
		snprintf(buf, buf_size, "%dMHz", int_attribute >> 16);
		break;

	case GPU_TEMP:
		/*
		 * NV_CTRL_GPU_CORE_TEMPERATURE reports the current core temperature
		 * of the GPU driving the X screen.
		 */
		XNVCTRLQueryTargetAttribute(display, NV_CTRL_TARGET_TYPE_GPU, gpu_id, 0, NV_CTRL_GPU_CORE_TEMPERATURE, &int_attribute);
		snprintf(buf, buf_size, "%.1fC", (float)int_attribute);
		break;

	case GPU_FAN:
		/*
		 * NV_CTRL_THERMAL_COOLER_SPEED - Returns cooler's current operating speed in
		 * rotations per minute (RPM).
		 */
		XNVCTRLQueryTargetAttribute(display, NV_CTRL_TARGET_TYPE_COOLER, gpu_id, 0, NV_CTRL_THERMAL_COOLER_SPEED, &int_attribute);
		snprintf(buf, buf_size, "%dRPM", int_attribute);
		break;

	default:
		return -1;
	};

	return 0;
}


#if 0 /* fallback to nvidia-settings, deprecated */
static int get_gpu_count()
{
	int gpu_count = 0;

	FILE *cmd;
	char dummy_buffer[256];
	static char nv_query[64];

	for (int i = 0; i < GKFREQ_MAX_GPUS; ++i)
	{
		snprintf(nv_query, 64, "nvidia-settings -q=[gpu:%d]/GPUCoreTemp", i);
		nv_query[63] = '\0';

		cmd = popen(nv_query, "r");
		fgets(dummy_buffer, 256, cmd);
		gpu_count += (WEXITSTATUS(pclose(cmd)) == 0)? 1 : 0;
	}

	return gpu_count;
}

static int get_gpu_data_internal(char* query)
{
	static char res[64];

	FILE *fp = popen(query, "r");
	fgets(res, 64, fp);
	res[63] = '\0';
	
	return (WEXITSTATUS(pclose(fp)) == 0)? atoi(res) : -1;
}

static int get_gpu_data(int gpu_id, int info, char *buf, int buf_size)
{
	static char query[128];

	switch (info)
	{
	case GPU_CLOCK:
		snprintf(query, 128, "nvidia-settings -q=[gpu:%d]/GPUCurrentClockFreqsString | grep Attribute | cut -d '=' -f 2 | cut -d ',' -f 1", gpu_id);
		query[127] = '\0';
		snprintf(buf, buf_size, "%dMHz", gpu_id, get_gpu_data_internal(query));
		break;

	case GPU_TEMP:
		snprintf(query, 128, "nvidia-settings -q=[gpu:%d]/GPUCoreTemp | grep Attribute | cut -d ' ' -f 6 | cut -d '.' -f 1", gpu_id);
		query[127] = '\0';
		snprintf(buf, buf_size, "%.1fC", gpu_id, (float)get_gpu_data_internal(query));
		break;

	case GPU_FAN:
		snprintf(query, 128, "nvidia-settings -q=[gpu:%d]/GPUCurrentFanSpeedRPM | grep Attribute | cut -d ' ' -f 6 | cut -d '.' -f 1", gpu_id);
		query[127] = '\0';
		snprintf(buf, buf_size, "%dRPM", gpu_id, get_gpu_data_internal(query));
		break;
	
	default:
		return -1;
	}

	return 0;
}
#endif

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
	int decal_base_idx;

	static char gpu_clock_label[] = "GPUN Clock:";
	static char gpu_clock_string[64];
	static char gpu_temp_label[] = "GPUN Temp:";
	static char gpu_temp_string[64];
	static char gpu_fan_label[] = "GPUN Fan:";
	static char gpu_fan_string[64];

	for (int i = 0; i < system_gpu_count; ++i) {

		decal_base_idx = i * 6;

		get_gpu_data(i, GPU_CLOCK, gpu_clock_string, 64);
		w_text = gdk_string_width(gdk_font_from_description(decal_text[decal_base_idx + 1]->text_style.font), gpu_clock_string);
		decal_text[decal_base_idx + 1]->x = w - m->left - m->right - w_text - 1;

		get_gpu_data(i, GPU_TEMP, gpu_temp_string, 64);
		w_text = gdk_string_width(gdk_font_from_description(decal_text[decal_base_idx + 3]->text_style.font), gpu_temp_string);
		decal_text[decal_base_idx + 3]->x = w - m->left - m->right - w_text - 1;

		get_gpu_data(i, GPU_FAN, gpu_fan_string, 64);
		w_text = gdk_string_width(gdk_font_from_description(decal_text[decal_base_idx + 5]->text_style.font), gpu_fan_string);
		decal_text[decal_base_idx + 5]->x = w - m->left - m->right - w_text - 1;

		gpu_clock_label[3] = gpu_temp_label[3] = gpu_fan_label[3] = i + '0';

		gkrellm_draw_decal_text(panel, decal_text[decal_base_idx + 0], gpu_clock_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[decal_base_idx + 1], gpu_clock_string, 0);

		gkrellm_draw_decal_text(panel, decal_text[decal_base_idx + 2], gpu_temp_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[decal_base_idx + 3], gpu_temp_string, 0);
		
		gkrellm_draw_decal_text(panel, decal_text[decal_base_idx + 4], gpu_fan_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[decal_base_idx + 5], gpu_fan_string, 0);
	}

	gkrellm_draw_panel_layers(panel);
}

static void create_plugin(GtkWidget* vbox, gint first_create)
{
	GkrellmStyle *style;
	GkrellmTextstyle *ts;
	int decal_base_idx;

	if (first_create)
		panel = gkrellm_panel_new0();
	style = gkrellm_meter_style(style_id);
	ts = gkrellm_meter_textstyle(style_id);

	system_gpu_count = get_gpu_count();

	for (int y = -1, idx = 0; idx < system_gpu_count; ++idx) {

		decal_base_idx = idx * 6;

		decal_text[decal_base_idx + 0] = gkrellm_create_decal_text(panel, "GPU8 Clock:", ts, style, -1, y, -1);
		decal_text[decal_base_idx + 1] = gkrellm_create_decal_text(panel, "8888MHz", ts, style, -1, y, -1);
		
		y = decal_text[decal_base_idx + 0]->y + decal_text[decal_base_idx + 0]->h + 1;
		
		decal_text[decal_base_idx + 2] = gkrellm_create_decal_text(panel, "GPU8 Temp:", ts, style, -1, y, -1);
		decal_text[decal_base_idx + 3] = gkrellm_create_decal_text(panel, "88.8C", ts, style, -1, y, -1);
		
		y = decal_text[decal_base_idx + 2]->y + decal_text[decal_base_idx + 2]->h + 1;
		
		decal_text[decal_base_idx + 4] = gkrellm_create_decal_text(panel, "GPU8 Fan:", ts, style, -1, y, -1);
		decal_text[decal_base_idx + 5] = gkrellm_create_decal_text(panel, "8888RPM", ts, style, -1, y, -1);

		/* next GPU */
		y = decal_text[decal_base_idx + 4]->y + decal_text[decal_base_idx + 4]->h + ((idx == system_gpu_count - 1)? 1 : 10);
	}
	gkrellm_panel_configure(panel, NULL, style);
	gkrellm_panel_create(vbox, monitor, panel);

	if (first_create)
		g_signal_connect(G_OBJECT(panel->drawing_area), "expose_event", G_CALLBACK(panel_expose_event), NULL);

}

static GkrellmMonitor plugin_mon =
{
	"nvidia",                    /* Name, for config tab.        */
	0,                           /* Id,  0 if a plugin           */
	create_plugin,               /* The create_plugin() function */
	update_plugin,               /* The update_plugin() function */
	NULL,                        /* The create_plugin_tab() config function */
	NULL,                        /* The apply_plugin_config() function      */

	NULL,                        /* The save_plugin_config() function  */
	NULL,                        /* The load_plugin_config() function  */
	NULL,                        /* config keyword                     */

	NULL,                        /* Undefined 2  */
	NULL,                        /* Undefined 1  */
	NULL,                        /* Undefined 0  */

	MON_CPU | MON_INSERT_AFTER,  /* Insert plugin before this monitor.       */
	NULL,                        /* Handle if a plugin, filled in by GKrellM */
	NULL                         /* path if a plugin, filled in by GKrellM   */
};

GkrellmMonitor* gkrellm_init_plugin(void)
{
	style_id = gkrellm_add_meter_style(&plugin_mon, "nvidia");
	monitor = &plugin_mon;

	return monitor;
}
