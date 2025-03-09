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
#include <gkrellm2/gkrellm.h>
#include "nvml-lib.h"

#define GK_PLUGIN_NAME "nvidia"
#define GK_CONFIG_KEYWORD "nvidia"
#define GK_MAX_TEXT 64
#define GK_MAX_PATH CFG_BUFSIZE

#ifndef GK_MAX_GPUS
 #define GK_MAX_GPUS 4
#endif
#define GK_MAX_GPU_FANS 1

static GKNVMLLib nvml;
static gboolean reset_lib = FALSE;

#ifndef GKFREQ_NVML_SONAME
 #define GKFREQ_NVML_SONAME "libnvidia-ml.so"
#endif

/* convert nvml return to boolean */
#define _NV(fn) (nvml.fn == NVML_SUCCESS)

/* convert bytes to mbytes */
#define B2MB(b) (b / 0x100000)

/* mark unused variables to avoid compile warnings */
#define UNUSED(x) (void)(x)

/* mark unused variables to avoid compile warnings */
#define ASSERT_SIZE(st, size) \
	typedef char st ## _size[(sizeof(st) / sizeof(st[0]) == size) - 1];


typedef struct _GKNvidia {
	GtkWidget *main_vbox;
	GkrellmMonitor *monitor;
	GkrellmPanel *panel;
	int style_id;
} GKNvidia;

static GKNvidia plugin;

typedef enum _TextAlignment {
	RIGHT,
	CENTER,
	LEFT
} TextAlignment_t;

typedef enum _GPUProperty {
	GPU_NAME,
	GPU_USAGE,
	GPU_CLOCK,
	GPU_MEMCLOCK,
	GPU_TEMP,
	GPU_FAN,
	GPU_FANUSAGE,
	GPU_POWER,
	GPU_MEMUSAGE,
	GPU_USEDMEM,
	GPU_TOTALMEM,
	GPU_PROPS_NUM
} GPUProperty_t;

typedef struct _GkrellmDecalRowInfo {
	gboolean enable;
	guint order;
	TextAlignment_t alignment;
	char *label;
	char *optionlabel;
} GkrellmDecalRowInfo_t;

static GkrellmDecalRowInfo_t decal_info[] = {
	{ TRUE,  0, CENTER, "",                ""                                },
	{ TRUE,  1, RIGHT,  _("Load"),         _("GPU Load")                     },
	{ TRUE,  2, RIGHT,  _("Clock"),        _("GPU Clock")                    },
	{ TRUE,  3, RIGHT,  _("Memory Clock"), _("GPU Memory Clock")             },
	{ TRUE,  4, RIGHT,  _("Temp"),         _("GPU Temperature")              },
	{ TRUE,  5, RIGHT,  _("Fan"),          _("GPU Fan Speed")                },
	{ TRUE,  6, RIGHT,  _("Fan"),          _("GPU Fan Speed (percentage)")   },
	{ TRUE,  7, RIGHT,  _("Power"),        _("GPU Power Draw")               },
	{ TRUE,  8, RIGHT,  _("Used Memory"),  _("GPU Used Memory (percentage)") },
	{ TRUE,  9, RIGHT,  _("Used Memory"),  _("GPU Used Memory")              },
	{ TRUE, 10, RIGHT,  _("Total Memory"), _("GPU Total Memory")             } 
};

/* make sure this stays consistent with gpu properties */
ASSERT_SIZE(decal_info, GPU_PROPS_NUM);

typedef struct _GkrellmDecalRow {
	GkrellmDecal *label;
	GkrellmDecal *data;
} GkrellmDecalRow_t;

static GkrellmDecalRow_t decal_text[GK_MAX_GPUS * GPU_PROPS_NUM];

#define INVALID_PROP -1u

typedef struct _NVGpuInfo {
	gboolean good;
	char name[GK_MAX_TEXT];
	nvmlDevice_t h;
	nvmlPciInfo_t pci;
	guint clock;
	guint memclock;
	guint temp;
	guint fan;
	guint pwr;
	nvmlUsage_t usage;
	nvmlMemory_t memory;
	guint fan_count;
	nvmlFan_t fan_data[GK_MAX_GPU_FANS];
} NVGpuInfo;

static NVGpuInfo gpu_info[GK_MAX_GPUS];

static void update_gpu_info(void)
{
	guint i, gpu_count, f;
	NVGpuInfo *g;

	memset(gpu_info, 0, sizeof(NVGpuInfo) * GK_MAX_GPUS);

	if (_NV(nvmlDeviceGetCount(&gpu_count)))
		if (CLAMP(gpu_count, 0, GK_MAX_GPUS) > 0)
			for (i = 0; i < gpu_count; ++i) {
				g = &gpu_info[i];
				g->good = _NV(nvmlDeviceGetHandleByIndex(i, &(g->h)))        &&
				          _NV(nvmlDeviceGetName(g->h, g->name, GK_MAX_TEXT)) &&
				          _NV(nvmlDeviceGetPciInfo(g->h, &(g->pci)));

				if (_NV(nvmlDeviceGetNumFans(g->h, &(g->fan_count)))) 
					g->fan_count = CLAMP(g->fan_count, 0, GK_MAX_GPU_FANS);
				else
					g->fan_count = 0;

				for (f = 0; f < g->fan_count; ++f) {
					g->fan_data[f].version = nvmlFan_ver;
					g->fan_data[f].fanidx = f;
				}
			}
}

static void update_gpu_data(void)
{
	int i;
	NVGpuInfo *g;

	for (i = 0; i < GK_MAX_GPUS; ++i) {
		
		g = &gpu_info[i];
		
		if (!g->good)
			continue;

		if (!decal_info[GPU_CLOCK].enable ||
			!_NV(nvmlDeviceGetClockInfo(g->h, NVML_CLOCK_GFX, &(g->clock))))
			g->clock = INVALID_PROP;

		if (!decal_info[GPU_CLOCK].enable ||
			!_NV(nvmlDeviceGetClockInfo(g->h, NVML_CLOCK_MEM, &(g->memclock))))
			g->memclock = INVALID_PROP;

		if (!decal_info[GPU_TEMP].enable ||
			!_NV(nvmlDeviceGetTemperature(g->h, NVML_TEMP_GPU, &(g->temp))))
			g->temp = INVALID_PROP;

		if (!decal_info[GPU_FANUSAGE].enable ||
			!_NV(nvmlDeviceGetFanSpeed(g->h, &(g->fan))))
			g->fan = INVALID_PROP;

		if (!decal_info[GPU_FAN].enable ||
			!_NV(nvmlDeviceGetFanSpeedRPM(g->h, &(g->fan_data[0]))))
			g->fan_data[0].speed = INVALID_PROP;

		if (!decal_info[GPU_POWER].enable ||
			!_NV(nvmlDeviceGetPowerUsage(g->h, &(g->pwr))))
			g->pwr = INVALID_PROP;

		if ((!decal_info[GPU_USAGE].enable &&
			 !decal_info[GPU_MEMUSAGE].enable) ||
			!_NV(nvmlDeviceGetUtilizationRates(g->h, &(g->usage))))
			g->usage.gpu = g->usage.memory = INVALID_PROP;

		if ((!decal_info[GPU_USEDMEM].enable &&
			 !decal_info[GPU_TOTALMEM].enable) ||
			!_NV(nvmlDeviceGetMemoryInfo(g->h, &(g->memory))))
			g->memory.free = g->memory.total = g->memory.used = INVALID_PROP;
	}
}

static gboolean get_gpu_data(int gpu_id, int info, char *buf, int buf_size)
{
	gboolean res = FALSE;
	NVGpuInfo *g;

	if (gpu_info[gpu_id].good) {

		g = &gpu_info[gpu_id];

		switch (info) {
		case GPU_NAME:
			strcpy(buf, g->name);
			res = TRUE;
			break;

		case GPU_CLOCK:
			snprintf(buf, buf_size, "%uMHz", g->clock);
			res = g->clock != INVALID_PROP;
			break;

		case GPU_MEMCLOCK:
			snprintf(buf, buf_size, "%uMHz", g->memclock);
			res = g->clock != INVALID_PROP;
			break;

		case GPU_TEMP:
			snprintf(buf, buf_size, "%.01fC", (float)(g->temp));
			res = g->temp != INVALID_PROP;
			break;

		case GPU_FANUSAGE:
			snprintf(buf, buf_size, "%u%%", CLAMP(g->fan, 0u, 100u));
			res = g->fan != INVALID_PROP;
			break;

		case GPU_FAN:
			snprintf(buf, buf_size, "%uRPM", g->fan_data[0].speed);
			res = g->fan_count > 0 && g->fan_data[0].speed != INVALID_PROP;
			break;

		case GPU_POWER:
			snprintf(buf, buf_size, "%uW", g->pwr / 1000);
			res = g->pwr != INVALID_PROP;
			break;

		case GPU_USAGE:
			snprintf(buf, buf_size, "%u%%", g->usage.gpu);
			res = g->usage.gpu != INVALID_PROP;
			break;

		case GPU_MEMUSAGE:
			snprintf(buf, buf_size, "%u%%", g->usage.memory);
			res = g->usage.memory != INVALID_PROP;
			break;

		case GPU_USEDMEM:
			snprintf(buf, buf_size, "%lluMB", B2MB(g->memory.used));
			res = g->memory.used != INVALID_PROP;
			break;

		case GPU_TOTALMEM:
			snprintf(buf, buf_size, "%lluMB", B2MB(g->memory.total));
			res = g->memory.total != INVALID_PROP;
			break;

		default:
			res = FALSE;
			break;
		}

	}

	if (!res)
		strcpy(buf, "N/A");

	return res;
}

static gint panel_expose_event(GtkWidget *widget, GdkEventExpose *ev)
{
	gdk_draw_pixmap(widget->window,
	                widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
	                plugin.panel->pixmap,
	                ev->area.x,
	                ev->area.y,
	                ev->area.x,
	                ev->area.y,
	                ev->area.width,
	                ev->area.height);

	return 0;
}

static void panel_click_event(GtkWidget *w, GdkEventButton *event, gpointer p)
{
	UNUSED(w);
	UNUSED(p);

	if (event->button == 3)
		gkrellm_open_config_window(plugin.monitor);
}

static void update_plugin(void)
{
	GkrellmStyle *style = gkrellm_panel_style(plugin.style_id);
	GkrellmMargin *m = gkrellm_get_style_margins(style);
	GkrellmDecal *d;
	int w = gkrellm_chart_width();
	int w_text, i, p, idx;
	static char temp_string[GK_MAX_TEXT] = "N/A";

	update_gpu_data();

	for (i = 0; i < GK_MAX_GPUS; ++i) {

		if (!gpu_info[i].good)
			continue;

		idx = i * GPU_PROPS_NUM;

		for (p = 0; p < GPU_PROPS_NUM; ++p) {

			d = decal_text[idx + p].label;

			if (decal_info[p].enable && d != NULL) {

				gkrellm_draw_decal_text(plugin.panel,
				                        d,
				                        decal_info[p].label,
				                        0);

				get_gpu_data(i, p, temp_string, GK_MAX_TEXT);
				
				w_text = gkrellm_gdk_string_width(d->text_style.font,
				                                  temp_string);

				switch (decal_info[p].alignment) {
				case LEFT:
					decal_text[idx + p].data->x = m->left;
					break;
				case CENTER:
					decal_text[idx + p].data->x = (w - w_text) / 2 - 1;
					break;
				case RIGHT:
					decal_text[idx + p].data->x = w -
					                              m->left -
					                              m->right -
					                              w_text -
					                              1;
					break;
				}

				gkrellm_draw_decal_text(plugin.panel,
				                        decal_text[idx + p].data,
				                        temp_string,
				                        0);
			}

		}
	}

	gkrellm_draw_panel_layers(plugin.panel);
}

static int
create_decal_row(int i, GPUProperty_t offset, gchar *label, gchar *text, int y)
{
	GkrellmStyle *style = gkrellm_meter_style(plugin.style_id);
	GkrellmTextstyle *ts = gkrellm_meter_textstyle(plugin.style_id);
	int idx = i * GPU_PROPS_NUM + offset;

	decal_text[idx].label = gkrellm_create_decal_text(plugin.panel,
	                                                  label,
	                                                  ts,
	                                                  style,
	                                                  -1,
	                                                  y,
	                                                  -1);
	
	decal_text[idx].data = gkrellm_create_decal_text(plugin.panel,
	                                                 text,
	                                                 ts,
	                                                 style,
	                                                 -1,
	                                                 y,
	                                                 -1);

	return MAX(decal_text[idx].label->y, decal_text[idx].data->y) +
	       MAX(decal_text[idx].label->h, decal_text[idx].data->h);
}

static void populate_panel(void)
{
	int i, j, y;
	
	for (y = -1, i = 0; i < GK_MAX_GPUS; ++i) {

		if (!gpu_info[i].good)
			continue;

		for (j = GPU_NAME; j < GPU_PROPS_NUM; ++j) {

			if (decal_info[j].enable) {
				y = create_decal_row(i, j, decal_info[j].label, "8888888", y);
				y += ((j == GPU_NAME)? 5 : 1);
			}

		}
	}
}

static void destroy_nv_panel()
{
	gkrellm_panel_destroy(plugin.panel);
	plugin.panel = NULL;
}

static void create_nv_panel(gint first_create)
{
	if (!plugin.panel)
		plugin.panel = gkrellm_panel_new0();

	populate_panel();

	gkrellm_panel_configure(plugin.panel,
	                        NULL,
	                        gkrellm_meter_style(plugin.style_id));
	
	gkrellm_panel_create(plugin.main_vbox, plugin.monitor, plugin.panel);

	if (first_create) {
		g_signal_connect(G_OBJECT(plugin.panel->drawing_area),
		                 "expose_event",
		                 G_CALLBACK(panel_expose_event),
		                 NULL);
		
		g_signal_connect(G_OBJECT(plugin.panel->drawing_area),
		                 "button_press_event",
		                 G_CALLBACK(panel_click_event),
		                 NULL);
	}
}

static void rebuild_nv_panel()
{
	destroy_nv_panel();
	create_nv_panel(TRUE);
}

static void shutdown_plugin()
{
	int i;

	for (i = 0; i < GK_MAX_GPUS; ++i)
		gpu_info[i].good = FALSE;

	shutdown_gpulib(&nvml);
}

static void create_plugin(GtkWidget* vbox, gint first_create)
{
	if (first_create) {
		plugin.main_vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), plugin.main_vbox, FALSE, FALSE, 0);
		gtk_widget_show(plugin.main_vbox);
	}

	if (initialize_gpulib(&nvml))
			update_gpu_info();

	gkrellm_disable_plugin_connect(plugin.monitor, shutdown_plugin);

	create_nv_panel(first_create);
}

static void cb_toggle(GtkWidget *button, gpointer data)
{
	gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));

	if (data != NULL)
		*(gboolean*)data = active;

	rebuild_nv_panel();
}

static void cb_pathchanged(GtkWidget *widget, gpointer data)
{
	UNUSED(data);

	static const char *ICON_OK = "gtk-yes";
	static const char *ICON_KO = "gtk-no";

	gchar *text = NULL;
	gboolean valid_path = FALSE;
	GIcon* valid_icon = NULL;

	gkrellm_dup_string(&text, gkrellm_gtk_entry_get_text(&widget));

	valid_path = is_valid_gpulib_path(text);
	valid_icon = g_themed_icon_new(valid_path? ICON_OK : ICON_KO);

	gtk_entry_set_icon_from_gicon(GTK_ENTRY(widget),
	                              GTK_ENTRY_ICON_SECONDARY,
	                              valid_icon);

	g_object_unref(valid_icon);

	reset_lib = valid_path;
	if (valid_path)
		strcpy(nvml.path, text);
}

/*
 * wrapper for gtk entry with label following the style of
 * gkrellm_gtk_check_button_connected or gkrellm_gtk_button_connected
 * found in gkrellm src/gui.c
 */
static void
gkrellm_gtk_entry_connected(GtkWidget *box, GtkWidget **entry, gchar *text,
                            gboolean expand, gboolean fill, gint pad,
                            void (*cb_func)(), gpointer data, gchar *label)
{
	GtkWidget *l = gtk_label_new(label);
	GtkWidget *e = gtk_entry_new_with_max_length(GK_MAX_PATH);

	GtkWidget *h = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(h), l, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(h), e, TRUE, TRUE, 4);

	if (text)
		gtk_entry_set_text(GTK_ENTRY(e), text);
	
	if (box) {
		if (pad < 0)
			gtk_box_pack_end(GTK_BOX(box), h, expand, fill, -(pad + 1));
		else
			gtk_box_pack_start(GTK_BOX(box), h, expand, fill, pad);
	}
	
	if (cb_func)
		g_signal_connect(G_OBJECT(e), "changed", G_CALLBACK(cb_func), data);
	
	if (entry)
		*entry = h;
}

static void create_plugin_tab(GtkWidget *tab_vbox)
{
	int i;
	GtkWidget *tabs, *vbox, *cntvbox;
	
	tabs = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
	gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

	vbox = gkrellm_gtk_framed_notebook_page(tabs, _(" Options "));
	
	gkrellm_gtk_entry_connected(vbox,
	                            NULL,
	                            nvml.path,
	                            FALSE,
	                            FALSE,
	                            0,
	                            cb_pathchanged,
	                            NULL,
	                            _("libNVML path"));

	cntvbox = gkrellm_gtk_framed_vbox(vbox, _(" Counters "), 2, TRUE, 4, 4);

	for (i = GPU_NAME + 1; i < GPU_PROPS_NUM; ++i)
		gkrellm_gtk_check_button_connected(cntvbox,
		                                   NULL,
		                                   decal_info[i].enable,
		                                   FALSE,
		                                   FALSE,
		                                   0,
		                                   cb_toggle,
		                                   &decal_info[i].enable,
		                                   decal_info[i].optionlabel);
}

static void apply_plugin_config(void)
{
	if (reset_lib) {
		if (reinitialize_gpulib(&nvml))
			update_gpu_info();
		rebuild_nv_panel();
		reset_lib = FALSE;
	}
}

static void save_plugin_config(FILE *f)
{
	fprintf(f, "%s NVML %d %d %d %d %d %d %d %d %s\n",
	                                           GK_CONFIG_KEYWORD,
	                                           decal_info[GPU_CLOCK].enable,
	                                           decal_info[GPU_TEMP].enable,
	                                           decal_info[GPU_FAN].enable,
	                                           decal_info[GPU_POWER].enable,
	                                           decal_info[GPU_USAGE].enable,
	                                           decal_info[GPU_MEMUSAGE].enable,
	                                           decal_info[GPU_USEDMEM].enable,
	                                           decal_info[GPU_TOTALMEM].enable,
	                                           nvml.path);
}


static void load_plugin_config(gchar *arg)
{
	gchar config_key[16];
	gchar config_line[GK_MAX_PATH];
	gboolean read_config_ok = FALSE;
	int i;

	if (sscanf(arg, "%15s %511[^\n]", config_key, config_line) == 2) {
	
		if (!strcmp(config_key, "NVML"))
			if (sscanf(config_line, "%d %d %d %d %d %d %d %d %511s",
			                                  &decal_info[GPU_CLOCK].enable,
			                                  &decal_info[GPU_TEMP].enable,
			                                  &decal_info[GPU_FAN].enable,
			                                  &decal_info[GPU_POWER].enable,
			                                  &decal_info[GPU_USAGE].enable,
			                                  &decal_info[GPU_MEMUSAGE].enable,
			                                  &decal_info[GPU_USEDMEM].enable,
			                                  &decal_info[GPU_TOTALMEM].enable,
			                                  nvml.path) == 9)

				read_config_ok = is_valid_gpulib_path(nvml.path);

	}

	if (!read_config_ok) {
		strcpy(nvml.path, GKFREQ_NVML_SONAME);

		for (i = GPU_NAME; i < GPU_PROPS_NUM; ++i)
			decal_info[i].enable = TRUE;

		for (i = 0; i < GK_MAX_GPUS; ++i)
			gpu_info[i].good = FALSE;
	}
}

static GkrellmMonitor plugin_mon =
{
	GK_PLUGIN_NAME,              /* Name, for config tab.                    */
	0,                           /* Id, 0 if a plugin                        */
	create_plugin,               /* The create_plugin() function             */
	update_plugin,               /* The update_plugin() function             */
	create_plugin_tab,           /* The create_plugin_tab() config function  */
	apply_plugin_config,         /* The apply_plugin_config() function       */

	save_plugin_config,          /* The save_plugin_config() function        */
	load_plugin_config,          /* The load_plugin_config() function        */
	GK_CONFIG_KEYWORD,           /* config keyword                           */

	NULL,                        /* Undefined 2                              */
	NULL,                        /* Undefined 1                              */
	NULL,                        /* Undefined 0                              */

	MON_CPU | MON_INSERT_AFTER,  /* Insert plugin before this monitor.       */
	NULL,                        /* Handle if a plugin, filled in by GKrellM */
	NULL                         /* path if a plugin, filled in by GKrellM   */
};

GkrellmMonitor* gkrellm_init_plugin(void)
{
	plugin.panel = NULL;
	plugin.main_vbox = NULL;
	plugin.style_id = gkrellm_add_meter_style(&plugin_mon, GK_PLUGIN_NAME);
	plugin.monitor = &plugin_mon;

	return plugin.monitor;
}
