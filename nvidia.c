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

/* default libNVML file name */
#ifndef GKFREQ_NVML_SONAME
 #define GKFREQ_NVML_SONAME "libnvidia-ml.so"
#endif

/* interface with NVML - begin */
#define NVML_SUCCESS 0
#define NVML_ERROR_UNKNOWN 999

#define NVML_CLOCK_GRAPHICS 0
#define NVML_TEMPERATURE_GPU 0

typedef void* nvmlDevice_t;
typedef int nvmlReturn_t;
typedef int nvmlClockType_t;
typedef int nvmlTemperatureSensors_t;

typedef nvmlReturn_t (*nvmlInit_fn)(void);
typedef nvmlReturn_t (*nvmlShutdown_fn)(void);
typedef nvmlReturn_t (*nvmlDeviceGetCount_fn)(guint*);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_fn)(guint, nvmlDevice_t*);
typedef nvmlReturn_t (*nvmlDeviceGetName_fn)(nvmlDevice_t, char*, guint);
typedef nvmlReturn_t (*nvmlDeviceGetClockInfo_fn)(nvmlDevice_t, nvmlClockType_t, guint*);
typedef nvmlReturn_t (*nvmlDeviceGetTemperature_fn)(nvmlDevice_t, nvmlTemperatureSensors_t, guint*);
typedef nvmlReturn_t (*nvmlDeviceGetFanSpeed_fn)(nvmlDevice_t, guint*);

nvmlInit_fn nvmlInit = NULL;
nvmlShutdown_fn nvmlShutdown = NULL;
nvmlDeviceGetCount_fn nvmlDeviceGetCount = NULL;
nvmlDeviceGetHandleByIndex_fn nvmlDeviceGetHandleByIndex = NULL;
nvmlDeviceGetName_fn nvmlDeviceGetName = NULL;
nvmlDeviceGetClockInfo_fn nvmlDeviceGetClockInfo = NULL;
nvmlDeviceGetTemperature_fn nvmlDeviceGetTemperature = NULL;
nvmlDeviceGetFanSpeed_fn nvmlDeviceGetFanSpeed = NULL;
/* interface with NVML - end */

#define GK_PLUGIN_NAME "nvidia"
#define GK_MAX_GPUS 4
#define GK_CONFIG_KEYWORD "nvidia"
#define GK_MAX_TEXT 64

static GkrellmMonitor* monitor = NULL;
static GkrellmPanel* panel = NULL;
static int style_id = 0;
static int system_gpu_count = 0;

#define UNUSED(x) (void)(x)

typedef enum GPUProperties_t {
	GPU_NAME,
	GPU_CLOCK,
	GPU_TEMP,
	GPU_FAN,
	GPU_PROPS_NUM
} GPUProperties;

typedef enum TextAlignment_t {
	RIGHT,
	CENTER,
	LEFT
} TAlignment;

typedef struct _GkrellmDecalRow {
	GkrellmDecal* label;
	GkrellmDecal* data;
} GkrellmDecalRow;

static GkrellmDecalRow decal_text[GK_MAX_GPUS * GPU_PROPS_NUM];

static gboolean decal_enabled[GPU_PROPS_NUM] = {TRUE, TRUE, TRUE, TRUE};

static TAlignment decal_align[GPU_PROPS_NUM] = {CENTER, RIGHT, RIGHT, RIGHT};

static char decal_labels[GPU_PROPS_NUM][GK_MAX_TEXT] = {
	"",
	"GPUX Clock",
	"GPUX Temp",
	"GPUX Fan"
};

static char nvml_path[GK_MAX_TEXT] = GKFREQ_NVML_SONAME;

static void *nvml_handle = NULL;

#define BIND_FUNCTION(fun, handle) fun = (fun##_fn)dlsym(handle, #fun)

static void shutdown_gpulib(void)
{
	if (nvml_handle)
	{
		if (nvmlShutdown)
			nvmlShutdown();
		
		dlclose(nvml_handle);
	}
}

static gboolean initialize_gpulib(void)
{
	shutdown_gpulib();

	nvml_handle = dlopen(nvml_path, RTLD_LAZY);
	if (nvml_handle)
	{
		BIND_FUNCTION(nvmlInit, nvml_handle);
		BIND_FUNCTION(nvmlShutdown, nvml_handle);
		BIND_FUNCTION(nvmlDeviceGetCount, nvml_handle);
		BIND_FUNCTION(nvmlDeviceGetHandleByIndex, nvml_handle);
		BIND_FUNCTION(nvmlDeviceGetName, nvml_handle);
		BIND_FUNCTION(nvmlDeviceGetClockInfo, nvml_handle);
		BIND_FUNCTION(nvmlDeviceGetTemperature, nvml_handle);
		BIND_FUNCTION(nvmlDeviceGetFanSpeed, nvml_handle);
	}

	return (nvml_handle != NULL &&
		    dlerror() == NULL &&
			nvmlInit() == NVML_SUCCESS);
}

static int get_gpu_count(void)
{
	nvmlReturn_t res;
	guint gpu_count;

	res = nvmlDeviceGetCount(&gpu_count);

	return (res == NVML_SUCCESS)? MIN(GK_MAX_GPUS, gpu_count) : 0;
}

static int get_gpu_data(int gpu_id, int info, char *buf, int buf_size)
{
	nvmlReturn_t res = NVML_ERROR_UNKNOWN;
	guint attr = -1;
	nvmlDevice_t device;

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

			if (decal_enabled[p] && d != NULL) {

				decal_labels[p][3] = i + '0';
				gkrellm_draw_decal_text(panel, d, decal_labels[p], 0);

				get_gpu_data(i, p, temp_string, GK_MAX_TEXT);
				
				w_text = gkrellm_gdk_string_width(d->text_style.font,
				                                  temp_string);

				switch (decal_align[p]) {
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
create_decal_row(int i, GPUProperties off, gchar* label, gchar* text, int y)
{
	GkrellmStyle *style = gkrellm_meter_style(style_id);
	GkrellmTextstyle *ts = gkrellm_meter_textstyle(style_id);
	int idx = i * GPU_PROPS_NUM + off;

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

static void empty_panel(void)
{
	gkrellm_destroy_decal_list(panel);
}

static void populate_panel(void)
{
	int i, y;
	GkrellmStyle *style = gkrellm_meter_style(style_id);

	for (y = -1, i = 0; i < system_gpu_count; ++i) {

		y = create_decal_row(i, GPU_NAME, "", "GPU_NAME", y);
		y += 5;

		if (decal_enabled[GPU_CLOCK]) {
			y = create_decal_row(i, GPU_CLOCK, "GPU8 Clock:", "8888MHz", y);
			y += 1;
		}

		if (decal_enabled[GPU_TEMP]) {
			y = create_decal_row(i, GPU_TEMP, "GPU8 Temp:", "88.8C", y);
			y += 1;
		}

		if (decal_enabled[GPU_FAN]) {
			y = create_decal_row(i, GPU_FAN, "GPU8 Fan:", "888%", y);
			y += 1;
		}

		y += ((i == system_gpu_count - 1)? 1 : 10);
	}

	gkrellm_panel_configure(panel, NULL, style);
}

static void create_plugin(GtkWidget* vbox, gint first_create)
{
	if (first_create) {
		panel = gkrellm_panel_new0();

		gkrellm_disable_plugin_connect(monitor, shutdown_gpulib);

		if (!initialize_gpulib()) {
			gkrellm_debug(G_LOG_LEVEL_ERROR, "NVML failed to load");
			shutdown_gpulib();	
			return;
		}

		system_gpu_count = get_gpu_count();
	
	} else {

		empty_panel();
	
	}

	populate_panel();
	
	gkrellm_panel_create(vbox, monitor, panel);

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

static void cb_toggle(GtkWidget* button, gpointer data)
{
	gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));

	if (data != NULL)
		*(gboolean*)data = active;

	empty_panel();
	populate_panel();
}

static void cb_pathchanged(GtkWidget* widget, gpointer data)
{
	UNUSED(widget);
	UNUSED(data);
}

static void create_plugin_tab(GtkWidget* tab_vbox)
{
	GtkWidget *tabs, *vbox;
	
	tabs = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

	vbox = gkrellm_gtk_framed_notebook_page(tabs, _("Options"));
	
	gkrellm_gtk_check_button_connected(vbox,
	                                   NULL,
	                                   decal_enabled[GPU_CLOCK],
	                                   FALSE,
	                                   FALSE,
	                                   0,
	                                   cb_toggle,
	                                   &decal_enabled[GPU_CLOCK],
	                                   _("Show GPU Clock"));
	
	gkrellm_gtk_check_button_connected(vbox,
	                                   NULL,
	                                   decal_enabled[GPU_TEMP],
	                                   FALSE,
	                                   FALSE,
	                                   0,
	                                   cb_toggle,
	                                   &decal_enabled[GPU_TEMP],
	                                   _("Show GPU Temperature"));
	
	gkrellm_gtk_check_button_connected(vbox,
	                                   NULL,
	                                   decal_enabled[GPU_FAN],
	                                   FALSE,
	                                   FALSE,
	                                   0,
	                                   cb_toggle,
	                                   &decal_enabled[GPU_FAN],
	                                   _("Show GPU Fan Speed"));
}

static void save_plugin_config(FILE *f)
{
    fprintf(f, "%s NVML: %s Decals: %d %d %d\n", GK_CONFIG_KEYWORD,
	                                             nvml_path,
	                                             decal_enabled[GPU_CLOCK],
	                                             decal_enabled[GPU_TEMP],
	                                             decal_enabled[GPU_FAN]);
}


static void load_plugin_config(gchar *arg)
{
	UNUSED(arg);

	strcpy(nvml_path, GKFREQ_NVML_SONAME);
	decal_enabled[GPU_CLOCK] = TRUE;
	decal_enabled[GPU_TEMP] = TRUE;
	decal_enabled[GPU_FAN] = TRUE;
}

static GkrellmMonitor plugin_mon =
{
	GK_PLUGIN_NAME,              /* Name, for config tab.                    */
	0,                           /* Id, 0 if a plugin                        */
	create_plugin,               /* The create_plugin() function             */
	update_plugin,               /* The update_plugin() function             */
	create_plugin_tab,           /* The create_plugin_tab() config function  */
	NULL,                        /* The apply_plugin_config() function       */

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
