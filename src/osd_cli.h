#ifndef _OSD_CLI_H
#define _OSD_CLI_H

osd_cli_t *osd_cli_create(gswm_t *gsw, gchar *font, gdouble red, gdouble green, gdouble blue);
gint osd_cli_destroy(osd_cli_t *obj);
void osd_cli_show(osd_cli_t *obj);
void osd_cli_hide(osd_cli_t *obj);
void osd_cli_set_text(osd_cli_t *obj, gchar *text);

#endif
