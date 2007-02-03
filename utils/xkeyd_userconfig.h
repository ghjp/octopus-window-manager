#ifndef _USERCONFIG_H
#define _USERCONFIG_H

typedef struct {
  GHashTable *keysym_command_hash;
  GHashTable *keycode_command_hash;
} user_cfg_t;

void init_xml_config(user_cfg_t *ucfg);
void read_xml_config(user_cfg_t *ucfg);
void finalize_xml_config(user_cfg_t *ucfg_p);

#endif
