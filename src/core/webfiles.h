/* "Arquivos pelo navegador": the card in any browser on the same Wi-Fi, to
 * list, download, upload, create folders and delete files.
 *
 * The server is the firmware's own BusyBox httpd, started only while the
 * window is open on the device (like FTP). Its document root is a folder in
 * RAM (/tmp/trimux/www) with the page (TriMux/share/web/index.html) and one
 * CGI program, trimuxctl webcgi, which does every file operation:
 *
 *   GET  ?op=list&path=Roms/GBA          JSON listing
 *   GET  ?op=get&path=Roms/GBA/x.gba     download
 *   POST ?op=put&path=Roms/GBA&name=x.gba[&overwrite=1]   body = the file
 *   POST ?op=mkdir&path=Roms&name=NEW
 *   POST ?op=del&path=Roms/GBA/x.gba     a file or an empty folder
 *
 * Paths are relative to the card root; "..", absolute paths and control
 * characters are refused, and TriMux's own folders and the official
 * system's (tm_path_protected) are read-only. Uploads are written to a
 * hidden temporary file and renamed only when complete. There is no
 * password (BusyBox limits, as with FTP): the page warns about it. */
#ifndef TRIMUX_WEBFILES_H
#define TRIMUX_WEBFILES_H

#include "paths.h"

#include <stdio.h>

#define TM_WEB_PORT 8080

int tm_web_available(const TmPaths *p);
/* Starts the server on ip:port (the device's Wi-Fi address). */
int tm_web_start(const TmPaths *p, const char *ip, int port);
void tm_web_stop(const TmPaths *p);
int tm_web_running(const TmPaths *p);

/* The CGI program: reads REQUEST_METHOD, QUERY_STRING and CONTENT_LENGTH
 * from the environment, the request body from in, writes the full HTTP
 * response (status line included) to out. Returns 0 when the request
 * succeeded. */
int tm_web_cgi(const char *root, FILE *in, FILE *out);

#endif
