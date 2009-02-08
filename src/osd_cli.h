#ifndef _OSD_CLI_H
#define _OSD_CLI_H

osd_cli_t *osd_cli_create(gswm_t *gsw);
void osd_cli_destroy(osd_cli_t *obj);
void osd_cli_show(osd_cli_t *obj);
void osd_cli_hide(osd_cli_t *obj);
void osd_cli_set_text(osd_cli_t *obj, gchar *text);
void osd_cli_printf(osd_cli_t *obj, const gchar *fmt, ...);
void osd_cli_set_horizontal_offset(osd_cli_t *obj, gint offset);
void osd_cli_set_vertical_offset(osd_cli_t *obj, gint offset);

#endif
