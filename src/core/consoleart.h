/* Console illustrations for the stock menu's icons (Sistema oficial).
 *
 * Each platform gets a small drawing of its console (or handheld, arcade
 * cabinet, PC) in a three-quarter view: bodies with a visible front face,
 * light from above, a soft floor shadow and a transparent background. The
 * drawings are generic shapes and colours made here, with no logos, names or
 * artwork taken from anyone. */
#ifndef TRIMUX_CONSOLEART_H
#define TRIMUX_CONSOLEART_H

/* Draws the console of platform sys_id ("FC", "GBA", ...) into an RGBA
 * buffer of size x size (straight alpha, cleared by the caller). accent is
 * the platform's colour, used by drawings without a colour of their own
 * (an unknown platform gets a gamepad in it). */
void tm_console_art(unsigned char *rgba, int size, const char *sys_id, unsigned accent);

#endif
