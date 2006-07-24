#ifndef _GLIB_LOG_EXT_H
#define _GLIB_LOG_EXT_H

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#ifndef g_critical
#define	g_critical(...)	g_log (G_LOG_DOMAIN,         \
			       G_LOG_LEVEL_CRITICAL,    \
			       __VA_ARGS__)
#endif
#define	g_info(...)	g_log (G_LOG_DOMAIN,         \
			       G_LOG_LEVEL_INFO,    \
			       __VA_ARGS__)
#ifndef g_debug
#define	g_debug(...)	g_log (G_LOG_DOMAIN,         \
			       G_LOG_LEVEL_DEBUG,  \
			       __VA_ARGS__)
#endif
#elif defined (__GNUC__)
#ifndef g_critical
#define	g_critical(format...)	g_log (G_LOG_DOMAIN,         \
				       G_LOG_LEVEL_CRITICAL,    \
				       format)
#endif
#define	g_info(format...)	g_log (G_LOG_DOMAIN,         \
				       G_LOG_LEVEL_INFO,    \
				       format)
#ifndef g_debug
#define	g_debug(format...)	g_log (G_LOG_DOMAIN,         \
				       G_LOG_LEVEL_DEBUG,  \
				       format)
#endif
#else	/* !__GNUC__ */
static void
g_critical (const gchar *format,
	 ...)
{
  va_list args;
  va_start (args, format);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, format, args);
  va_end (args);
}
static void
g_info (const gchar *format,
	 ...)
{
  va_list args;
  va_start (args, format);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, format, args);
  va_end (args);
}
static void
g_debug (const gchar *format,
	   ...)
{
  va_list args;
  va_start (args, format);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
  va_end (args);
}
#endif	/* !__GNUC__ */

#endif /* _GLIB_LOG_EXT_H */
