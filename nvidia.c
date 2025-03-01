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
#include <dlfcn.h>

#define GK_PLUGIN_NAME "nvidia"
#define GK_MAX_GPUS 4
#define GK_CONFIG_KEYWORD "nvidia"
#define GK_MAX_TEXT 64
#define GK_MAX_PATH CFG_BUFSIZE
#define UNUSED(x) (void)(x)

/* interface with NVML - begin */
typedef enum { NVML_SUCCESS, NVML_ERROR_UNKNOWN = 999 } nvmlReturn_t;
typedef enum { NVML_CLOCK_GRAPHICS } nvmlClockType_t;
typedef enum { NVML_TEMPERATURE_GPU } nvmlTemperatureSensors_t;

typedef void* nvmlDevice_t;

typedef struct nvmlMemory_st {
	unsigned long long free;
	unsigned long long total;
	unsigned long long used;
} nvmlMemory_t;

typedef struct nvmlUtilization_st {
	guint gpu;
	guint memory;
} nvmlUtilization_t;

typedef nvmlReturn_t (*nvmlInit_fn)(void);
typedef nvmlReturn_t (*nvmlShutdown_fn)(void);
typedef nvmlReturn_t (*nvmlDeviceGetCount_fn)(guint*);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_fn)(guint, nvmlDevice_t*);
typedef nvmlReturn_t (*nvmlDeviceGetName_fn)(nvmlDevice_t, char*, guint);
typedef nvmlReturn_t (*nvmlDeviceGetClockInfo_fn)(nvmlDevice_t, nvmlClockType_t, guint*);
typedef nvmlReturn_t (*nvmlDeviceGetTemperature_fn)(nvmlDevice_t, nvmlTemperatureSensors_t, guint*);
typedef nvmlReturn_t (*nvmlDeviceGetFanSpeed_fn)(nvmlDevice_t, guint*);
typedef nvmlReturn_t (*nvmlDeviceGetPowerUsage_fn)(nvmlDevice_t, guint*);
typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_fn)(nvmlDevice_t, nvmlUtilization_t*);
typedef nvmlReturn_t (*nvmlDeviceGetMemoryInfo_fn)(nvmlDevice_t, nvmlMemory_t*);

nvmlInit_fn nvmlInit = NULL;
nvmlShutdown_fn nvmlShutdown = NULL;
nvmlDeviceGetCount_fn nvmlDeviceGetCount = NULL;
nvmlDeviceGetHandleByIndex_fn nvmlDeviceGetHandleByIndex = NULL;
nvmlDeviceGetName_fn nvmlDeviceGetName = NULL;
nvmlDeviceGetClockInfo_fn nvmlDeviceGetClockInfo = NULL;
nvmlDeviceGetTemperature_fn nvmlDeviceGetTemperature = NULL;
nvmlDeviceGetFanSpeed_fn nvmlDeviceGetFanSpeed = NULL;
nvmlDeviceGetPowerUsage_fn nvmlDeviceGetPowerUsage = NULL;
nvmlDeviceGetUtilizationRates_fn nvmlDeviceGetUtilizationRates = NULL;
nvmlDeviceGetMemoryInfo_fn nvmlDeviceGetMemoryInfo = NULL;
/* interface with NVML - end */

#ifndef GKFREQ_NVML_SONAME
 #define GKFREQ_NVML_SONAME "libnvidia-ml.so"
#endif

static char nvml_path[GK_MAX_PATH] = GKFREQ_NVML_SONAME;

static void *nvml_handle = NULL;

#define BIND_FUNCTION(handle, fun) fun = (fun##_fn)dlsym(handle, #fun)

static GtkWidget *plugin_vbox = NULL;
static GkrellmMonitor* monitor = NULL;
static GkrellmPanel* panel = NULL;
static int style_id = 0;
static int system_gpu_count = 0;
static gboolean needs_reinitialization = FALSE;

typedef enum _GPUProperty {
	GPU_NAME,
	GPU_CLOCK,
	GPU_TEMP,
	GPU_FAN,
	GPU_POWER,
	GPU_USAGE,
	GPU_MEMUSAGE,
	GPU_USEDMEM,
	GPU_TOTALMEM,
	GPU_PROPS_NUM
} GPUProperty_t;

typedef enum _TextAlignment {
	RIGHT,
	CENTER,
	LEFT
} TextAlignment_t;

typedef struct _GkrellmDecalRowInfo {
	GPUProperty_t prop;
	gboolean enable;
	TextAlignment_t alignment;
	char* label;
	char* optionlabel;
} GkrellmDecalRowInfo;

static GkrellmDecalRowInfo decal_info[] = {
	{ GPU_NAME,     TRUE, CENTER, "",                ""                                         },
	{ GPU_CLOCK,    TRUE, RIGHT,  _("Clock"),        _("Show GPU Clock")                        },
	{ GPU_TEMP,     TRUE, RIGHT,  _("Temp"),         _("Show GPU Temperature")                  },
	{ GPU_FAN,      TRUE, RIGHT,  _("Fan"),          _("Show GPU Fan Speed")                    },
	{ GPU_POWER,    TRUE, RIGHT,  _("Power"),        _("Show GPU Power Usage")                  },
	{ GPU_USAGE,    TRUE, RIGHT,  _("Usage"),        _("Show GPU Usage")                        },
	{ GPU_MEMUSAGE, TRUE, RIGHT,  _("Memory Usage"), _("Show GPU Memory Usage (as percentage)") },
	{ GPU_USEDMEM,  TRUE, RIGHT,  _("Memory Usage"), _("Show GPU Memory In Use")                },
	{ GPU_TOTALMEM, TRUE, RIGHT,  _("Total Memory"), _("Show GPU Total Memory")                 }
};

typedef struct _GkrellmDecalRow {
	GkrellmDecal* label;
	GkrellmDecal* data;
} GkrellmDecalRow;

static GkrellmDecalRow decal_text[GK_MAX_GPUS * GPU_PROPS_NUM];

static void shutdown_gpulib(void)
{
	if (nvml_handle)
	{
		if (nvmlShutdown)
			nvmlShutdown();
		
		dlclose(nvml_handle);
		nvml_handle = NULL;
	}
}

static gboolean is_valid_gpulib(char* path)
{
	void* temp_handle = NULL;
	nvmlInit_fn temp_fn = NULL;

	temp_handle = (strlen(path) > 0)? dlopen(path, RTLD_LAZY) : NULL;
	temp_fn = (temp_handle != NULL)? (nvmlInit_fn)dlsym(temp_handle, "nvmlInit") : NULL;

	return (temp_fn != NULL);
}

static gboolean initialize_gpulib(void)
{
	static gboolean res = FALSE;

	nvml_handle = dlopen(nvml_path, RTLD_LAZY);
	if (nvml_handle)
	{
		BIND_FUNCTION(nvml_handle, nvmlInit);
		BIND_FUNCTION(nvml_handle, nvmlShutdown);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetCount);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetHandleByIndex);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetName);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetClockInfo);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetTemperature);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetFanSpeed);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetPowerUsage);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetUtilizationRates);
		BIND_FUNCTION(nvml_handle, nvmlDeviceGetMemoryInfo);

		res = (dlerror() == NULL && nvmlInit() == NVML_SUCCESS);
	}

	if (!res)
		gkrellm_debug(G_LOG_LEVEL_ERROR, "could not load '%s'", nvml_path);

	return res;
}

static gboolean reinitialize_gpulib(void)
{
	shutdown_gpulib();
	return initialize_gpulib();
}

static void update_gpu_count(void)
{
	nvmlReturn_t res;
	guint gpu_count;

	res = nvmlDeviceGetCount &&
	      (nvmlDeviceGetCount(&gpu_count) == NVML_SUCCESS);

	system_gpu_count = res? MIN(GK_MAX_GPUS, gpu_count) : 0;
}

static int get_gpu_data(int gpu_id, int info, char *buf, int buf_size)
{
	nvmlReturn_t res = NVML_ERROR_UNKNOWN;
	guint attr = -1;
	nvmlDevice_t device;
	nvmlUtilization_t utilization;
	nvmlMemory_t memInfo;

	if (nvmlDeviceGetHandleByIndex(gpu_id, &device) == NVML_SUCCESS) {

		switch (info)
		{
		case GPU_NAME:
			res = nvmlDeviceGetName(device, buf, buf_size);
			break;

		case GPU_CLOCK:
			res = nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &attr);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%uMHz", attr);
			break;

		case GPU_TEMP:
			res = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &attr);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%.1fC", (float)attr);
			break;

		case GPU_FAN:
			res = nvmlDeviceGetFanSpeed(device, &attr);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%d%%", CLAMP(attr, 0, 100));
			break;

		case GPU_POWER:
			res = nvmlDeviceGetPowerUsage(device, &attr);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%uW", attr / 1000);
			break;

		case GPU_USAGE:
		case GPU_MEMUSAGE:
			res = nvmlDeviceGetUtilizationRates(device, &utilization);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%u%%", (info == GPU_USAGE)? utilization.gpu : utilization.memory);
			break;

		case GPU_USEDMEM:
		case GPU_TOTALMEM:
			res = nvmlDeviceGetMemoryInfo(device, &memInfo);
			if (res == NVML_SUCCESS)
				snprintf(buf, buf_size, "%lluMB", ((info == GPU_USEDMEM)? memInfo.used : memInfo.total) / 1024 / 1024);
			break;
		}

	}

	if (res != NVML_SUCCESS)
		strcpy(buf, "N/A");

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

static void panel_click_event(GtkWidget *w, GdkEventButton *event, gpointer p)
{
	UNUSED(w);
	UNUSED(p);

    if (event->button == 3)
        gkrellm_open_config_window(monitor);
}

static void update_plugin(void)
{
	GkrellmStyle *style = gkrellm_panel_style(style_id);
	GkrellmMargin *m = gkrellm_get_style_margins(style);
	GkrellmDecal *d;
	int w = gkrellm_chart_width();
	int w_text, i, p, idx;
	static char temp_string[GK_MAX_TEXT] = "N/A";

	for (i = 0; i < system_gpu_count; ++i) {

		idx = i * GPU_PROPS_NUM;

		for (p = 0; p < GPU_PROPS_NUM; ++p) {

			d = decal_text[idx + p].label;

			if (decal_info[p].enable && d != NULL) {

				gkrellm_draw_decal_text(panel, d, decal_info[p].label, 0);

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

				gkrellm_draw_decal_text(panel,
				                        decal_text[idx + p].data,
				                        temp_string,
				                        0);
			}

		}
	}

	gkrellm_draw_panel_layers(panel);
}

static int
create_decal_row(int i, GPUProperty_t offset, gchar* label, gchar* text, int y)
{
	GkrellmStyle *style = gkrellm_meter_style(style_id);
	GkrellmTextstyle *ts = gkrellm_meter_textstyle(style_id);
	int idx = i * GPU_PROPS_NUM + offset;

	decal_text[idx].label = gkrellm_create_decal_text(panel,
	                                                  label,
	                                                  ts,
	                                                  style,
	                                                  -1,
	                                                  y,
	                                                  -1);
	
	decal_text[idx].data = gkrellm_create_decal_text(panel,
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
	
	for (y = -1, i = 0; i < system_gpu_count; ++i) {

		for (j = GPU_NAME; j < GPU_PROPS_NUM; ++j) {

			if (decal_info[j].enable) {
				y = create_decal_row(i, j, decal_info[j].label, "8888888", y);
				y += ((j == GPU_NAME)? 5 : 1);
			}

		}
		
		y += ((i == system_gpu_count - 1)? 1 : 10);
	
	}
}

static void destroy_nv_panel()
{
	gkrellm_panel_destroy(panel);
	panel = NULL;
}

static void create_nv_panel(gint first_create)
{
	if (!panel)
		panel = gkrellm_panel_new0();

	populate_panel();

	gkrellm_panel_configure(panel, NULL, gkrellm_meter_style(style_id));
	
	gkrellm_panel_create(plugin_vbox, monitor, panel);

	if (first_create) {
		g_signal_connect(G_OBJECT(panel->drawing_area),
		                 "expose_event",
		                 G_CALLBACK(panel_expose_event),
		                 NULL);
		
		g_signal_connect(G_OBJECT(panel->drawing_area),
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

static void create_plugin(GtkWidget* vbox, gint first_create)
{
	if (first_create) {
		plugin_vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), plugin_vbox, FALSE, FALSE, 0);
		gtk_widget_show(plugin_vbox);
	}

	if (initialize_gpulib())
			update_gpu_count();

	gkrellm_disable_plugin_connect(monitor, shutdown_gpulib);

	create_nv_panel(first_create);
}

static void cb_toggle(GtkWidget* button, gpointer data)
{
	gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));

	if (data != NULL)
		*(gboolean*)data = active;

	rebuild_nv_panel();
}

static void cb_pathchanged(GtkWidget* widget, gpointer data)
{
	UNUSED(data);

	static const char *ICON_OK = "gtk-yes";
	static const char *ICON_KO = "gtk-no";

	gchar *text = NULL;
	gboolean valid_path = FALSE;
	GIcon* valid_icon = NULL;

	gkrellm_dup_string(&text, gkrellm_gtk_entry_get_text(&widget));

	valid_path = is_valid_gpulib(text);
	valid_icon = g_themed_icon_new(valid_path? ICON_OK : ICON_KO);

	gtk_entry_set_icon_from_gicon(GTK_ENTRY(widget),
	                              GTK_ENTRY_ICON_SECONDARY,
	                              valid_icon);

	g_object_unref(valid_icon);

	needs_reinitialization = valid_path;
	if (valid_path)
		strcpy(nvml_path, text);
}

/*
 * wrapper for gtk entry with label following the style of
 * gkrellm_gtk_check_button_connected or gkrellm_gtk_button_connected
 * found in gkrellm src/gui.c
 */
static void
gkrellm_gtk_entry_connected(GtkWidget *box, GtkWidget **entry, gchar* text,
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

static void create_plugin_tab(GtkWidget* tab_vbox)
{
	int i;
	GtkWidget *tabs, *vbox;
	
	tabs = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

	vbox = gkrellm_gtk_framed_notebook_page(tabs, _("Options"));
	
	gkrellm_gtk_entry_connected(vbox,
	                            NULL,
	                            nvml_path,
	                            FALSE,
	                            FALSE,
	                            0,
	                            cb_pathchanged,
	                            NULL,
	                            _("libNVML path"));

	for (i = GPU_CLOCK; i < GPU_PROPS_NUM; ++i)
		gkrellm_gtk_check_button_connected(vbox,
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
	if (needs_reinitialization) {
		if (reinitialize_gpulib())
			update_gpu_count();
		rebuild_nv_panel();
		needs_reinitialization = FALSE;
	}
}

static void save_plugin_config(FILE *f)
{
	fprintf(f, "%s NVML %d %d %d %d %d %d %d %d %s\n", GK_CONFIG_KEYWORD,
	                                                   decal_info[GPU_CLOCK].enable,
	                                                   decal_info[GPU_TEMP].enable,
	                                                   decal_info[GPU_FAN].enable,
	                                                   decal_info[GPU_POWER].enable,
	                                                   decal_info[GPU_USAGE].enable,
												       decal_info[GPU_MEMUSAGE].enable,
	                                                   decal_info[GPU_USEDMEM].enable,
	                                                   decal_info[GPU_TOTALMEM].enable,
	                                                   nvml_path);
}


static void load_plugin_config(gchar *arg)
{
	gchar config_key[16];
	gchar config_line[512];
	gboolean read_config_ok = FALSE;
	int i;
    
	if (sscanf(arg, "%15s %511[^\n]", config_key, config_line) == 2) {
	
		if (!strcmp(config_key, "NVML"))
			if (sscanf(config_line, "%d %d %d %d %d %d %d %d %511s", &decal_info[GPU_CLOCK].enable,
			                                                         &decal_info[GPU_TEMP].enable,
			                                                         &decal_info[GPU_FAN].enable,
			                                                         &decal_info[GPU_POWER].enable,
			                                                         &decal_info[GPU_USAGE].enable,
															         &decal_info[GPU_MEMUSAGE].enable,
			                                                         &decal_info[GPU_USEDMEM].enable,
																	 &decal_info[GPU_TOTALMEM].enable,
			                                                         nvml_path) == 9)
				read_config_ok = TRUE;

	}

	if (!read_config_ok) {
		strcpy(nvml_path, GKFREQ_NVML_SONAME);

		for (i = GPU_CLOCK; i < GPU_PROPS_NUM; ++i)
			decal_info[i].enable = TRUE;
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
	style_id = gkrellm_add_meter_style(&plugin_mon, GK_PLUGIN_NAME);
	monitor = &plugin_mon;

	return monitor;
}
