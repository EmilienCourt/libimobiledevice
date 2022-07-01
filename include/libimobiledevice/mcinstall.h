/**
 * @file libimobiledevice/mcinstall.h
 * @brief Manage configuration profiles.
 * \internal
 *
 * Copyright (c) 2013-2014 Martin Szulecki All Rights Reserved.
 * Copyright (c) 2012 Nikias Bassen All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef IMCINSTALL_H
#define IMCINSTALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

/** Service identifier passed to lockdownd_start_service() to start the mcinstall service */
#define MCINSTALL_SERVICE_NAME "com.apple.mobile.MCInstall"

/** Error Codes */
typedef enum {
	MCINSTALL_E_SUCCESS        =  0,
	MCINSTALL_E_INVALID_ARG    = -1,
	MCINSTALL_E_PLIST_ERROR    = -2,
	MCINSTALL_E_CONN_FAILED    = -3,
	MCINSTALL_E_REQUEST_FAILED = -4,
	MCINSTALL_E_UNKNOWN_ERROR  = -256
} mcinstall_error_t;

typedef struct mcinstall_client_private mcinstall_client_private; /**< \private */
typedef mcinstall_client_private *mcinstall_client_t; /**< The client handle. */

/* Interface */

/**
 * Connects to the mcinstall service on the specified device.
 *
 * @param device The device to connect to.
 * @param service The service descriptor returned by lockdownd_start_service.
 * @param client Pointer that will point to a newly allocated
 *     mcinstall_client_t upon successful return.
 *
 * @return MCINSTALL_E_SUCCESS on success, MCINSTALL_E_INVALID_ARG when
 *     client is NULL, or an MCINSTALL_E_* error code otherwise.
 */
mcinstall_error_t mcinstall_client_new(idevice_t device, lockdownd_service_descriptor_t service, mcinstall_client_t *client);

/**
 * Starts a new mcinstall service on the specified device and connects to it.
 *
 * @param device The device to connect to.
 * @param client Pointer that will point to a newly allocated
 *     mcinstall_client_t upon successful return. Must be freed using
 *     mcinstall_client_free() after use.
 * @param label The label to use for communication. Usually the program name.
 *  Pass NULL to disable sending the label in requests to lockdownd.
 *
 * @return MCINSTALL_E_SUCCESS on success, or an MCINSTALL_E_* error
 *     code otherwise.
 */
mcinstall_error_t mcinstall_client_start_service(idevice_t device, mcinstall_client_t* client, const char* label);

/**
 * Disconnects an mcinstall client from the device and frees up the
 * mcinstall client data.
 *
 * @param client The mcinstall client to disconnect and free.
 *
 * @return MCINSTALL_E_SUCCESS on success, MCINSTALL_E_INVALID_ARG when
 *     client is NULL, or an MCINSTALL_E_* error code otherwise.
 */
mcinstall_error_t mcinstall_client_free(mcinstall_client_t client);


/**
 * Installs the given configuration profile. Only works with valid profiles. The device should be unlocked.
 *
 * @param client The connected mcinstall to use for installation
 * @param profile The valid configuration profile to install. This has to be
 *    passed as a PLIST_DATA, otherwise the function will fail.
 *
 * @return MCINSTALL_E_SUCCESS on success, MCINSTALL_E_INVALID_ARG when
 *     client is invalid, or an MCINSTALL_E_* error code otherwise.
 */
mcinstall_error_t mcinstall_install(mcinstall_client_t client, plist_t profile);

/**
 * Retrieves the list of installed configuration profiles.
 *
 * @param client The connected mcinstall to use.
 * @param profiles Pointer to a plist_t that will be set to a PLIST_DICT
 *    if the function is successful.
 *
 * @return MCINSTALL_E_SUCCESS on success, MCINSTALL_E_INVALID_ARG when
 *     client is invalid, or an MCINSTALL_E_* error code otherwise.
 *
 * @note If no configuration profiles are installed on the device, this function
 *     still returns MCINSTALL_E_SUCCESS and profiles will just point to a
 *     dict with empty elements.
 */
mcinstall_error_t mcinstall_get_profile_list(mcinstall_client_t client, plist_t* profiles);

/**
 * Removes a given configuration profile.
 *
 * @param client The connected mcinstall to use.
 * @param payloadIdentifier Identifier of the configuration profile to remove.
 *    This is the name of the configuration profile.
 * @param payloadUUID Unique identifier of the configuration profile to remove.
 *    This is a UUID that can be obtained from the configuration profile data.
 * @param payloadVersion Version of the configuration profile to remove.
 *    This is an integer that can be obtained from the configuration profile data.
 * @see mcinstall_get_profile_list
 *
 * @return MCINSTALL_E_SUCCESS on success, MCINSTALL_E_INVALID_ARG when
 *     client is invalid, or an MCINSTALL_E_* error code otherwise.
 */
mcinstall_error_t mcinstall_remove(mcinstall_client_t client, const char* payloadIdentifier, const char * payloadUUID, const uint64_t payloadVersion);

/**
 * Retrieves the status code from the last operation.
 *
 * @param client The mcinstall to use.
 *
 * @return -1 if client is invalid, or the status code from the last operation
 */
int mcinstall_get_status_code(mcinstall_client_t client);

#ifdef __cplusplus
}
#endif

#endif