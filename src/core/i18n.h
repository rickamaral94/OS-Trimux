/* Translations: share/i18n/<code>.lang files with "key = text" lines.
 * Missing keys fall back to the default language (pt_BR), then to the key. */
#ifndef TRIMUX_I18N_H
#define TRIMUX_I18N_H

#include <stddef.h>

#define TM_DEFAULT_LANG "pt_BR"

int tm_i18n_load(const char *i18n_dir, const char *code);
const char *tm_tr(const char *key);
const char *tm_i18n_current(void);
/* Lists available language codes (files *.lang). Returns count. */
size_t tm_i18n_available(const char *i18n_dir, char codes[][16], size_t max);
/* Human name of a language, read from its own file (key "lang.name"). */
int tm_i18n_language_name(const char *i18n_dir, const char *code, char *out, size_t size);
void tm_i18n_free(void);

#endif
