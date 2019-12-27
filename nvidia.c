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

static int max(int a, int b)
{
	return (a > b)? a : b;
}

static int get_gpu_count()
{
	int event, error;
	Bool res;
	int gpu_count = 0;

	display = XOpenDisplay(NULL);

	if (!display || XNVCTRLQueryExtension(display, &event, &error) != True) {

		if (display)
			XCloseDisplay(display);

		return 0;
	}

	res = XNVCTRLQueryTargetCount(display, NV_CTRL_TARGET_TYPE_GPU, &gpu_count);

	return (res == True)? gpu_count : 0;
}

static int get_gpu_data(int gpu_id, int info, char *buf, int buf_size)
{
	Bool res = False;
	int int_attribute = -1;
	int target_count = 0;
	
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
		res = XNVCTRLQueryTargetAttribute(display,
		                                  NV_CTRL_TARGET_TYPE_GPU,
		                                  gpu_id,
		                                  0,
		                                  NV_CTRL_GPU_CURRENT_CLOCK_FREQS,
		                                  &int_attribute);

		if (res == True) {
			snprintf(buf, buf_size, "%dMHz", int_attribute >> 16);
		}
		break;

	case GPU_TEMP:
		/*
		 * NV_CTRL_GPU_CORE_TEMPERATURE reports the current core temperature
		 * of the GPU driving the X screen.
		 */
		res = XNVCTRLQueryTargetAttribute(display,
		                                  NV_CTRL_TARGET_TYPE_GPU,
		                                  gpu_id,
		                                  0,
		                                  NV_CTRL_GPU_CORE_TEMPERATURE,
		                                  &int_attribute);

		if (res == True) {
			snprintf(buf, buf_size, "%.1fC", (float)int_attribute);
		}
		break;

	case GPU_FAN:
		/*
		 * NV_CTRL_THERMAL_COOLER_SPEED - Returns cooler's current operating
		 * speed in rotations per minute (RPM).
		 */
		if (XNVCTRLQueryTargetCount(display, NV_CTRL_TARGET_TYPE_COOLER, &target_count) == True &&
		    target_count > 0) {

			res = XNVCTRLQueryTargetAttribute(display,
			                                  NV_CTRL_TARGET_TYPE_COOLER,
			                                  gpu_id,
			                                  0,
			                                  NV_CTRL_THERMAL_COOLER_SPEED,
			                                  &int_attribute);
		}

		if (res == True) {
			snprintf(buf, buf_size, "%dRPM", int_attribute);
		}
		break;

	default:
		return -1;
	};

	return (res == True)? 0 : -1;
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

	static char clock_label[] = "GPUN Clock:";
	static char clock_string[64] = "N/A";
	static char temp_label[] = "GPUN Temp:";
	static char temp_string[64] = "N/A";
	static char fan_label[] = "GPUN Fan:";
	static char fan_string[64] = "N/A";

	for (int i = 0; i < system_gpu_count; ++i) {

		get_gpu_data(i, GPU_CLOCK, clock_string, 64);
		w_text = gdk_string_width(
			gdk_font_from_description(decal_text[i * 6 + 1]->text_style.font),
			clock_string);
		decal_text[i * 6 + 1]->x = w - m->left - m->right - w_text - 1;

		get_gpu_data(i, GPU_TEMP, temp_string, 64);
		w_text = gdk_string_width(
			gdk_font_from_description(decal_text[i * 6 + 3]->text_style.font),
			temp_string);
		decal_text[i * 6 + 3]->x = w - m->left - m->right - w_text - 1;

		get_gpu_data(i, GPU_FAN, fan_string, 64);
		w_text = gdk_string_width(
			gdk_font_from_description(decal_text[i * 6 + 5]->text_style.font),
			fan_string);
		decal_text[i * 6 + 5]->x = w - m->left - m->right - w_text - 1;

		/* replace the N in "GPUN <attribute>" string */
		clock_label[3] = temp_label[3] = fan_label[3] = i + '0';

		gkrellm_draw_decal_text(panel, decal_text[i * 6 + 0], clock_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[i * 6 + 1], clock_string, 0);

		gkrellm_draw_decal_text(panel, decal_text[i * 6 + 2], temp_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[i * 6 + 3], temp_string, 0);
		
		gkrellm_draw_decal_text(panel, decal_text[i * 6 + 4], fan_label, 0);
		gkrellm_draw_decal_text(panel, decal_text[i * 6 + 5], fan_string, 0);
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

		decal_text[idx * 6 + 0] = gkrellm_create_decal_text(panel,
		                                                    "GPU8 Clock:",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		decal_text[idx * 6 + 1] = gkrellm_create_decal_text(panel,
		                                                    "8888MHz",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);
		
		y = max(decal_text[idx * 6 + 0]->y, decal_text[idx * 6 + 1]->y) +
		    max(decal_text[idx * 6 + 0]->h, decal_text[idx * 6 + 1]->h) + 1;
		
		decal_text[idx * 6 + 2] = gkrellm_create_decal_text(panel,
		                                                    "GPU8 Temp:",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		decal_text[idx * 6 + 3] = gkrellm_create_decal_text(panel,
		                                                    "88.8C",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);
		
		y = max(decal_text[idx * 6 + 2]->y, decal_text[idx * 6 + 3]->y) +
		    max(decal_text[idx * 6 + 2]->h, decal_text[idx * 6 + 3]->h) + 1;
		
		decal_text[idx * 6 + 4] = gkrellm_create_decal_text(panel,
		                                                    "GPU8 Fan:",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		decal_text[idx * 6 + 5] = gkrellm_create_decal_text(panel,
		                                                    "8888RPM",
		                                                    ts,
		                                                    style,
		                                                    -1,
		                                                    y,
		                                                    -1);

		y = max(decal_text[idx * 6 + 4]->y, decal_text[idx * 6 + 5]->y) +
		    max(decal_text[idx * 6 + 4]->h, decal_text[idx * 6 + 5]->h) + 1;

		/* next GPU infos */
		y += ((idx == system_gpu_count - 1)? 1 : 10);
	}
	gkrellm_panel_configure(panel, NULL, style);
	gkrellm_panel_create(vbox, monitor, panel);

	if (first_create)
		g_signal_connect(G_OBJECT(panel->drawing_area),
		                 "expose_event",
		                 G_CALLBACK(panel_expose_event),
		                 NULL);

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
