#define G_LOG_DOMAIN "userconfig"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Include headers {{{1 */
#include "octopus.h"
#include "userconfig.h"
#include "action_system.h"

#include <stdlib.h> /* strtol */

#define DEFAULT_VDESK_NUM 4

/* Defines {{{1 */
#define CONFIG_FILE_BASE_NAME "config.xml"

#define DEFAULT_VDESK_NUM 4

/* XML parser callback routines {{{1 */
/* Called for open tags <foo bar="baz"> */

static void _read_color_intensity(const gchar *element_name,
    const gchar **attribute_names, const gchar **attribute_values,
    color_intensity_t *ci, GError **error)
{
  gint i;

  for(i = 0; attribute_names[i]; i++) {
    if(g_str_equal(attribute_names[i], "red"))
      ci->r = g_ascii_strtod(attribute_values[i], NULL);
    else if(g_str_equal(attribute_names[i], "green"))
      ci->g = g_ascii_strtod(attribute_values[i], NULL);
    else if(g_str_equal(attribute_names[i], "blue"))
      ci->b = g_ascii_strtod(attribute_values[i], NULL);
    else {
      *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
          "%s Detected unknown attribute `%s' from element %s",
          __func__, attribute_names[i], element_name);
      break;
    }
  }
}

static void _xml_config_start_elem(GMarkupParseContext *context,
    const gchar         *element_name,
    const gchar        **attribute_names,
    const gchar        **attribute_values,
    gpointer             user_data,
    GError             **error)
{
  gint i;
  gswm_t *gsw = (gswm_t*)user_data;

  if(g_str_equal(element_name, "layout")) {
    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "border_width"))
        gsw->ucfg.border_width = strtol(attribute_values[i], NULL, 0);
      else if(g_str_equal(attribute_names[i], "titlebar_height"))
        gsw->ucfg.titlebar_height = strtol(attribute_values[i], NULL, 0);
      else if(g_str_equal(attribute_names[i], "titlebar_font_family")) {
        g_free(gsw->ucfg.titlebar_font_family);
        gsw->ucfg.titlebar_font_family = g_strdup(attribute_values[i]);
      }
      else if(g_str_equal(attribute_names[i], "osd_font_family")) {
        g_free(gsw->ucfg.osd_font);
        gsw->ucfg.osd_font = g_strdup(attribute_values[i]);
      }
      else if(g_str_equal(attribute_names[i], "osd_height"))
        gsw->ucfg.osd_height = strtol(attribute_values[i], NULL, 0);
      else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
            "%s Detected unknown attribute `%s' from element %s",
            __func__, attribute_names[i], element_name);
        break;
      }
    }
  }
  else if(g_str_equal(element_name, "new_action")) {
    const gchar *name, *action;

    name = action = NULL;
    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "name"))
        name = attribute_values[i];
      else if(g_str_equal(attribute_names[i], "action"))
        action = attribute_values[i];
      else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
            "%s Detected unknown attribute `%s' from element %s",
            __func__, attribute_names[i], element_name);
        break;
      }
    }
    if(name && action)
      action_system_add(gsw, name, action);
    else
      *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
          "%s Element %s must contain attributes `name' and `action'",
          __func__, element_name);
  }
  else if(g_str_equal(element_name, "osd_border_color"))
    _read_color_intensity(element_name, attribute_names, attribute_values,
        &gsw->ucfg.osd_bgc, error);
  else if(g_str_equal(element_name, "osd_text_color"))
    _read_color_intensity(element_name, attribute_names, attribute_values,
        &gsw->ucfg.osd_fgc, error);
  else if(g_str_equal(element_name, "focused_color"))
    _read_color_intensity(element_name, attribute_names, attribute_values,
        &gsw->ucfg.focused_color, error);
  else if(g_str_equal(element_name, "unfocused_color"))
    _read_color_intensity(element_name, attribute_names, attribute_values,
        &gsw->ucfg.unfocused_color, error);
  else if(g_str_equal(element_name, "focused_text_color"))
    _read_color_intensity(element_name, attribute_names, attribute_values,
        &gsw->ucfg.focused_text_color, error);
  else if(g_str_equal(element_name, "unfocused_text_color"))
    _read_color_intensity(element_name, attribute_names, attribute_values,
        &gsw->ucfg.unfocused_text_color, error);
  else if(g_str_equal(element_name, "alpha")) {
    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "east"))
        gsw->ucfg.alpha_east = g_strtod(attribute_values[i], NULL);
      else if(g_str_equal(attribute_names[i], "west"))
        gsw->ucfg.alpha_west = g_strtod(attribute_values[i], NULL);
      else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
            "%s Detected unknown attribute `%s' from element %s",
            __func__, attribute_names[i], element_name);
        break;
      }
    }
  }
  else if(g_str_equal(element_name, "options")) {
    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "border_snap"))
        gsw->ucfg.border_snap = strtol(attribute_values[i], NULL, 0);
      else if(g_str_equal(attribute_names[i], "client_snap"))
        gsw->ucfg.client_snap = strtol(attribute_values[i], NULL, 0);
      else if(g_str_equal(attribute_names[i], "resize_inc_fraction"))
        gsw->ucfg.resize_inc_fraction = strtol(attribute_values[i], NULL, 0);
      else if(g_str_equal(attribute_names[i], "xterm_cmd")) {
        g_free(gsw->ucfg.xterm_cmd);
        gsw->ucfg.xterm_cmd = g_strdup(attribute_values[i]);
      }
      else if(g_str_equal(attribute_names[i], "xterm_terminal_cmd")) {
        g_free(gsw->ucfg.xterm_terminal_cmd);
        gsw->ucfg.xterm_terminal_cmd = g_strdup(attribute_values[i]);
      }
      else if(g_str_equal(attribute_names[i], "modifier")) {
        if(!g_ascii_strcasecmp(attribute_values[i], "mod1"))
          gsw->ucfg.modifier = Mod1Mask;
        else if(!g_ascii_strcasecmp(attribute_values[i], "mod2"))
          gsw->ucfg.modifier = Mod2Mask;
        else if(!g_ascii_strcasecmp(attribute_values[i], "mod3"))
          gsw->ucfg.modifier = Mod3Mask;
        else if(!g_ascii_strcasecmp(attribute_values[i], "mod4"))
          gsw->ucfg.modifier = Mod4Mask;
        else if(!g_ascii_strcasecmp(attribute_values[i], "mod5"))
          gsw->ucfg.modifier = Mod5Mask;
        else {
          *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
              "%s Detected unknown attribute value `%s' for attribute %s",
              __func__, attribute_values[i], attribute_names[i]);
          break;
        }
      }
      else if(g_str_equal(attribute_names[i], "move_opaque"))
        gsw->ucfg.move_opaque = strtol(attribute_values[i], NULL, 0) ? TRUE: FALSE;
      else if(g_str_equal(attribute_names[i], "resize_opaque"))
        gsw->ucfg.resize_opaque = strtol(attribute_values[i], NULL, 0) ? TRUE: FALSE;
      else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
            "%s Detected unknown attribute `%s' from element %s",
            __func__, attribute_names[i], element_name);
        break;
      }
    }
  }
  else if(g_str_equal(element_name, "vdesk")) {
    /* TODO Parse the workspace names */
    gchar *lastname = NULL;

    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "name")) {
        g_free(lastname);
        lastname = g_strdup(attribute_values[i]);
      }
    }
    if(!lastname)
      lastname = g_strdup_printf("vdesk%02d", gsw->ucfg.vdesk_names->len);
    g_ptr_array_add(gsw->ucfg.vdesk_names, lastname);
    TRACE(("%s Found vdeskname=%s", __func__, lastname));
  }
  else if(g_str_equal(element_name, PACKAGE_NAME)) {
    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "version")) {
        if(!g_str_equal(attribute_values[i], PACKAGE_VERSION))
          g_warning("Version mismatch between config file and application: %s != %s",
              attribute_values[i], PACKAGE_VERSION);
      }
      else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
            "%s Detected unknown attribute `%s' from element %s",
            __func__, attribute_names[i], element_name);
        break;
      }
    }
  }
}

/* Called for close tags </foo> */
static void _xml_config_end_elem(GMarkupParseContext *context,
    const gchar         *element_name,
    gpointer             user_data,
    GError             **error)
{
  /* TODO Fill in useful actions */
  /*
  g_message("%s Found closing element `%s'", __func__, element_name);
  */
}

/* Called for character data */
/* text is not nul-terminated */
static void _xml_config_text(GMarkupParseContext *context,
    const gchar         *text,
    gsize                text_len,
    gpointer             user_data,
    GError             **error)
{
  /* TODO Fill in useful actions */
  /*
  gchar *str;
  str = g_strndup(text, text_len);
  g_message("%s called with %d bytes data: %s", __func__, text_len, str);
  g_free(str);
  */
}

/* Called for strings that should be re-saved verbatim in this same
 * position, but are not otherwise interpretable.  At the moment
 * this includes comments and processing instructions.
 */
/* text is not nul-terminated. */
static void _xml_config_passthrough(GMarkupParseContext *context,
    const gchar         *passthrough_text,
    gsize                text_len,  
    gpointer             user_data,
    GError             **error)
{
  /* TODO Fill in useful actions */
  /*
  g_message("%s called with %d bytes data", __func__, text_len);
  */
}

/* Called on error, including one set by other
 * methods in the vtable. The GError should not be freed.
 */
static void _xml_config_error(GMarkupParseContext *context,
    GError              *error,
    gpointer             user_data)
{
  g_critical("%s: %s", __func__, error->message);
}

/* XML config read function {{{1 */

void read_xml_config(gswm_t *gsw)
{
  GMarkupParser gmp;
  GMarkupParseContext *gmpc;
  const gchar *xdg_config_home;
  gchar *cfg_fname;
  gchar *xml_doc = NULL;
  gsize xmllen;
  GError *error = NULL;

  /* This window manager is conform to the
     XDG Base Directory Specification */
  xdg_config_home = g_getenv("XDG_CONFIG_HOME");
  if(xdg_config_home)
    cfg_fname = g_build_filename(xdg_config_home,
        PACKAGE_NAME, CONFIG_FILE_BASE_NAME, NULL);
  else
    cfg_fname = g_build_filename(g_get_home_dir(), ".config",
        PACKAGE_NAME, CONFIG_FILE_BASE_NAME, NULL);

  if(!g_file_get_contents(cfg_fname, &xml_doc, &xmllen, &error)) {
    g_warning("%s: %s", __func__, error->message);
    g_error_free(error);
    goto out1;
  }

  gmp.start_element = _xml_config_start_elem;
  gmp.end_element   = _xml_config_end_elem;
  gmp.text          = _xml_config_text;
  gmp.passthrough   = _xml_config_passthrough;
  gmp.error         = _xml_config_error;

  gmpc = g_markup_parse_context_new(&gmp, 0, gsw, NULL);
  error = NULL;
  if(!g_markup_parse_context_parse(gmpc, xml_doc, xmllen, &error) ||
      !g_markup_parse_context_end_parse(gmpc, &error)) {
    g_critical("%s `%s' caused parse error: %s", __func__, cfg_fname, error->message);
    g_error_free(error);
  }

  g_markup_parse_context_free(gmpc);
  g_free(xml_doc);
out1:
  g_free(cfg_fname);

  /* Check consistency of configuration again.
     User configuration files are a pain */
  if(!gsw->ucfg.vdesk_names->len) {
    gint i;

    TRACE(("%s constructing default %d vdesks", __func__, DEFAULT_VDESK_NUM));
    for(i = 0; i < DEFAULT_VDESK_NUM; i++) {
      gchar *lastname = g_strdup_printf("vdesk%02d", i);
      g_ptr_array_add(gsw->ucfg.vdesk_names, lastname);
    }
  }
  if(0 > gsw->ucfg.titlebar_height)
    gsw->ucfg.titlebar_height = 0;
  else if(4 > gsw->ucfg.titlebar_height)
    gsw->ucfg.titlebar_height = 4;
  if(gsw->ucfg.titlebar_height)
    gsw->ucfg.border_width = CLAMP(gsw->ucfg.border_width, 0, gsw->ucfg.titlebar_height-1);
  else if(0 > gsw->ucfg.border_width)
    gsw->ucfg.border_width = 0;
  gsw->ucfg.focused_color.r = CLAMP(gsw->ucfg.focused_color.r, 0.0, 1.0);
  gsw->ucfg.focused_color.g = CLAMP(gsw->ucfg.focused_color.g, 0.0, 1.0);
  gsw->ucfg.focused_color.b = CLAMP(gsw->ucfg.focused_color.b, 0.0, 1.0);
  gsw->ucfg.resize_inc_fraction = CLAMP(gsw->ucfg.resize_inc_fraction, 1, 100);
  gsw->ucfg.alpha_east = CLAMP(gsw->ucfg.alpha_east, 0.0, 1.0);
  gsw->ucfg.alpha_west = CLAMP(gsw->ucfg.alpha_west, 0.0, 1.0);
}

void init_xml_config(gswm_t *gsw)
{
  TRACE((__func__));
  gsw->ucfg.border_width = 2;
  gsw->ucfg.titlebar_height = 16;
  gsw->ucfg.titlebar_font_family = g_strdup("Sans");
  gsw->ucfg.border_snap = 10;
  gsw->ucfg.client_snap = 10;
  gsw->ucfg.resize_inc_fraction = 10;
  gsw->ucfg.resize_opaque = gsw->ucfg.move_opaque = FALSE;
  gsw->ucfg.xterm_cmd = g_strdup("xterm");
  gsw->ucfg.xterm_terminal_cmd = g_strdup("xterm -e");
  gsw->ucfg.modifier = Mod1Mask;
  /* denim theme is the default */
  gsw->ucfg.alpha_east = 0.9;
  gsw->ucfg.alpha_west = 0.1;
  gsw->ucfg.focused_color.r = 0.12;
  gsw->ucfg.focused_color.g = 0.39;
  gsw->ucfg.focused_color.b = 1.00;
  gsw->ucfg.unfocused_color.r = 0.36;
  gsw->ucfg.unfocused_color.g = 0.56;
  gsw->ucfg.unfocused_color.b = 1.00;
  gsw->ucfg.focused_text_color.r = 0.90;
  gsw->ucfg.focused_text_color.g = 0.90;
  gsw->ucfg.focused_text_color.b = 0.98;
  gsw->ucfg.unfocused_text_color.r = 0.2;
  gsw->ucfg.unfocused_text_color.g = 0.2;
  gsw->ucfg.unfocused_text_color.b = 0.2;
  gsw->ucfg.vdesk_names = g_ptr_array_new();
  gsw->ucfg.osd_bgc = gsw->ucfg.focused_color;
  gsw->ucfg.osd_fgc = gsw->ucfg.focused_text_color;
  gsw->ucfg.osd_font = g_strdup(gsw->ucfg.titlebar_font_family);
  gsw->ucfg.osd_height = (3*gsw->ucfg.titlebar_height) / 2;
}
