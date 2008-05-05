#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#define G_LOG_DOMAIN "octopus_xkeyd-userconfig"
#include <glib.h>

/* Include headers {{{1 */
#include "xkeyd_userconfig.h"

#include <stdlib.h> /* strtol */

/* Defines {{{1 */
#define CONFIG_FILE_BASE_NAME "xkeyb.xml"

/* XML parser callback routines {{{1 */
/* Called for open tags <foo bar="baz"> */

static void _xml_config_start_elem(GMarkupParseContext *context,
    const gchar         *element_name,
    const gchar        **attribute_names,
    const gchar        **attribute_values,
    gpointer             user_data,
    GError             **error)
{
  gint i;
  user_cfg_t *ucfg_p = (user_cfg_t*)user_data;

  if(g_str_equal(element_name, "config")) {
    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "model")) {
        g_message("Configuration model is a '%s'", attribute_values[i]);
      }
      else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
            "%s Detected unknown attribute `%s' from element %s",
            __func__, attribute_names[i], element_name);
        break;
      }
    }
  }
  else if(g_str_equal(element_name, "userdef")) {
    guint keycode = 0;
    const gchar *keysym = NULL, *command = NULL;

    for(i = 0; attribute_names[i]; i++) {
      if(g_str_equal(attribute_names[i], "keysym"))
        keysym = attribute_values[i];
      else if(g_str_equal(attribute_names[i], "keycode"))
        keycode = strtoul(attribute_values[i], NULL, 0);
      else if(g_str_equal(attribute_names[i], "command"))
        command = attribute_values[i];
      else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
            "%s Detected unknown attribute `%s' from element %s",
            __func__, attribute_names[i], element_name);
        break;
      }
    }
    if(command) {
      gchar *cmd  = g_strdup(command);
      if(keycode)
        g_hash_table_replace(ucfg_p->keycode_command_hash, GUINT_TO_POINTER(keycode), cmd);
      else if(keysym) {
        gchar *ksym = g_strdup(keysym);
        g_hash_table_replace(ucfg_p->keysym_command_hash, ksym, cmd);
      }
    }
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

void read_xml_config(user_cfg_t *ucfg_p)
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

  gmpc = g_markup_parse_context_new(&gmp, 0, ucfg_p, NULL);
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
}

void init_xml_config(user_cfg_t *ucfg_p)
{
  ucfg_p->keysym_command_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  ucfg_p->keycode_command_hash = g_hash_table_new_full(NULL, g_direct_equal, NULL, g_free);
}

void finalize_xml_config(user_cfg_t *ucfg_p)
{
  /* FIXME: The follwing two calls are only available in glib2 version 2.12 */
  g_hash_table_remove_all(ucfg_p->keysym_command_hash);
  g_hash_table_remove_all(ucfg_p->keycode_command_hash);
  ucfg_p->keysym_command_hash = ucfg_p->keycode_command_hash = NULL;
}
