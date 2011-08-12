/*
 * e2k-enums.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E2K_ENUMS_H
#define E2K_ENUMS_H

/**
 * E2kAutoconfigGalAuthPref:
 * E2K_AUTOCONFIG_USE_GAL_DEFAULT:
 *   Try NTLM first if available, then fall back to Basic.
 * E2K_AUTOCONFIG_USE_GAL_BASIC:
 *   Plaintext password authentication.
 * E2K_AUTOCONFIG_USE_GAL_NTLM:
 *   NTLM (NT LAN Manager) authentication.
 *
 * Authentication method for the Global Address List server.
 **/
typedef enum {
	E2K_AUTOCONFIG_USE_GAL_DEFAULT,
	E2K_AUTOCONFIG_USE_GAL_BASIC,
	E2K_AUTOCONFIG_USE_GAL_NTLM
} E2kAutoconfigGalAuthPref;

#endif /* E2K_ENUMS_H */
