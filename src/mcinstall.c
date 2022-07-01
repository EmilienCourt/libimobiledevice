/*
 * mcinstall.c
 * com.apple.mobile.MCInstall service implementation.
 *
 * Copyright (c) 2012 Nikias Bassen, All Rights Reserved.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <plist/plist.h>
#include <stdio.h>

#include "mcinstall.h"
#include "property_list_service.h"
#include "common/debug.h"

/**
 * Convert a property_list_service_error_t value to a mcinstall_error_t
 * value. Used internally to get correct error codes.
 *
 * @param err A property_list_service_error_t error code
 *
 * @return A matching mcinstall_error_t error code,
 *     MCINSTALL_E_UNKNOWN_ERROR otherwise.
 */
static mcinstall_error_t mcinstall_error(property_list_service_error_t err)
{
	switch (err) {
		case PROPERTY_LIST_SERVICE_E_SUCCESS:
			return MCINSTALL_E_SUCCESS;
		case PROPERTY_LIST_SERVICE_E_INVALID_ARG:
			return MCINSTALL_E_INVALID_ARG;
		case PROPERTY_LIST_SERVICE_E_PLIST_ERROR:
			return MCINSTALL_E_PLIST_ERROR;
		case PROPERTY_LIST_SERVICE_E_MUX_ERROR:
			return MCINSTALL_E_CONN_FAILED;
		default:
			break;
	}
	return MCINSTALL_E_UNKNOWN_ERROR;
}

/**
 * Checks the response from mcinstall to determine if the operation
 * was successful or an error occurred. Internally used only.
 *
 * @param response a PLIST_DICT received from device's mcinstall
 * @param status_code pointer to an int that will be set to the status code
 *   contained in the response
 * @note If possible, the error chain is parsed and printed
 */
static mcinstall_error_t mcinstall_check_result(plist_t response, int* status_code)
{
	if (plist_get_node_type(response) != PLIST_DICT) {
		return MCINSTALL_E_PLIST_ERROR;
	}

	plist_t node = plist_dict_get_item(response, "Status");
	if (!node || (plist_get_node_type(node) != PLIST_STRING)) {
		plist_free(node);
		return MCINSTALL_E_PLIST_ERROR;
	}

	char* status = NULL;
	plist_get_string_val(node, &status);
	plist_free(node);

	if (strcmp(status, "Acknowledged") == 0) {
		return MCINSTALL_E_SUCCESS;
	}
	else if (strcmp(status, "Error") == 0) {
		plist_t error_chain = plist_dict_get_item(response, "ErrorChain");
		if (error_chain && (plist_get_node_type(error_chain) == PLIST_ARRAY)) {
			uint32_t num_errors = plist_array_get_size(error_chain);
			uint32_t j;
			for (j = 0; j < num_errors; j++) {
				plist_t error = plist_array_get_item(error_chain, j);
				if (error && (plist_get_node_type(error) == PLIST_DICT)) {
					char* error_string = NULL;
					node = plist_dict_get_item(error, "LocalizedDescription");
					if (node && (plist_get_node_type(node) == PLIST_STRING)) {
						plist_get_string_val(node, &error_string);
						printf("Error %d : %s\n", j, error_string);
					}
					free(error_string);
				}
			}
		}
		plist_free(error_chain);
		return MCINSTALL_E_PLIST_ERROR;
	}
	return MCINSTALL_E_REQUEST_FAILED;
}

LIBIMOBILEDEVICE_API mcinstall_error_t mcinstall_client_new(idevice_t device, lockdownd_service_descriptor_t service, mcinstall_client_t *client)
{
	property_list_service_client_t plistclient = NULL;
	mcinstall_error_t err = mcinstall_error(property_list_service_client_new(device, service, &plistclient));
	if (err != MCINSTALL_E_SUCCESS) {
		return err;
	}

	mcinstall_client_t client_loc = (mcinstall_client_t) malloc(sizeof(struct mcinstall_client_private));
	client_loc->parent = plistclient;
	client_loc->last_error = 0;

	*client = client_loc;
	return MCINSTALL_E_SUCCESS;
}

LIBIMOBILEDEVICE_API mcinstall_error_t mcinstall_client_start_service(idevice_t device, mcinstall_client_t * client, const char* label)
{
	mcinstall_error_t err = MCINSTALL_E_UNKNOWN_ERROR;
	service_client_factory_start_service(device, MCINSTALL_SERVICE_NAME, (void**)client, label, SERVICE_CONSTRUCTOR(mcinstall_client_new), &err);
	return err;
}

LIBIMOBILEDEVICE_API mcinstall_error_t mcinstall_client_free(mcinstall_client_t client)
{
	if (!client)
		return MCINSTALL_E_INVALID_ARG;

	mcinstall_error_t err = MCINSTALL_E_SUCCESS;
	if (client->parent && client->parent->parent) {
		mcinstall_error(property_list_service_client_free(client->parent));
	}
	client->parent = NULL;
	free(client);

	return err;
}

LIBIMOBILEDEVICE_API mcinstall_error_t mcinstall_install(mcinstall_client_t client, plist_t profile)
{
	if (!client || !client->parent || !profile || (plist_get_node_type(profile) != PLIST_DATA))
		return MCINSTALL_E_INVALID_ARG;

	client->last_error = MCINSTALL_E_UNKNOWN_ERROR;

	plist_t dict = plist_new_dict();
	plist_dict_set_item(dict, "RequestType", plist_new_string("InstallProfile"));
	plist_dict_set_item(dict, "Payload", plist_copy(profile));

	mcinstall_error_t res = mcinstall_error(property_list_service_send_xml_plist(client->parent, dict));
	plist_free(dict);
	dict = NULL;

	if (res != MCINSTALL_E_SUCCESS) {
		debug_info("could not send plist, error %d", res);
		return res;
	}

	res = mcinstall_error(property_list_service_receive_plist(client->parent, &dict));
	if (res != MCINSTALL_E_SUCCESS) {
		debug_info("could not receive response, error %d", res);
		return res;
	}
	if (!dict) {
		debug_info("could not get response plist");
		return MCINSTALL_E_UNKNOWN_ERROR;
	}

	res = mcinstall_check_result(dict, &client->last_error);
	plist_free(dict);

	return res;
}

LIBIMOBILEDEVICE_API mcinstall_error_t mcinstall_get_profile_list(mcinstall_client_t client, plist_t* profiles)
{
	if (!client || !client->parent || !profiles)
		return MCINSTALL_E_INVALID_ARG;

	client->last_error = MCINSTALL_E_UNKNOWN_ERROR;

	plist_t dict = plist_new_dict();
	plist_dict_set_item(dict, "RequestType", plist_new_string("GetProfileList"));

	mcinstall_error_t res = mcinstall_error(property_list_service_send_xml_plist(client->parent, dict));
	plist_free(dict);
	dict = NULL;

	if (res != MCINSTALL_E_SUCCESS) {
		printf("could not send plist, error %d", res);
		return res;
	}

	res = mcinstall_error(property_list_service_receive_plist(client->parent, &dict));
	if (res != MCINSTALL_E_SUCCESS) {
		printf("could not receive response, error %d", res);
		return res;
	}
	if (!dict) {
		printf("could not get response plist");
		return MCINSTALL_E_UNKNOWN_ERROR;
	}

	res = mcinstall_check_result(dict, &client->last_error);
	if (res == MCINSTALL_E_SUCCESS) {
		*profiles = plist_copy(dict);
	}
	plist_free(dict);

	return res;

}

LIBIMOBILEDEVICE_API mcinstall_error_t mcinstall_remove(mcinstall_client_t client, const char* payloadIdentifier, const char* payloadUUID, const uint64_t payloadVersion)
{
	if (!client || !client->parent || !payloadIdentifier || !payloadUUID || !payloadVersion)
		return MCINSTALL_E_INVALID_ARG;
	client->last_error = MCINSTALL_E_UNKNOWN_ERROR;

	plist_t data = plist_new_dict();
	plist_dict_set_item(data, "PayloadType", plist_new_string("Configuration"));
	plist_dict_set_item(data, "PayloadIdentifier", plist_new_string(payloadIdentifier));
	plist_dict_set_item(data, "PayloadUUID", plist_new_string(payloadUUID));
	plist_dict_set_item(data, "PayloadVersion", plist_new_uint(payloadVersion));

	/* convert data payload to binary */
	uint32_t bin_len = 0;
	char* bin = NULL;
	plist_to_bin(data, &bin, &bin_len);
	plist_free(data);
	data = NULL;

	plist_t dict = plist_new_dict();
	plist_dict_set_item(dict, "RequestType", plist_new_string("RemoveProfile"));
	plist_dict_set_item(dict, "ProfileIdentifier", plist_new_data((char*) bin, bin_len));

	mcinstall_error_t res = mcinstall_error(property_list_service_send_xml_plist(client->parent, dict));
	free(bin);
	plist_free(dict);
	dict = NULL;

	if (res != MCINSTALL_E_SUCCESS) {
		debug_info("could not send plist, error %d", res);
		return res;
	}

	res = mcinstall_error(property_list_service_receive_plist(client->parent, &dict));
	if (res != MCINSTALL_E_SUCCESS) {
		debug_info("could not receive response, error %d", res);
		return res;
	}
	if (!dict) {
		debug_info("could not get response plist");
		return MCINSTALL_E_UNKNOWN_ERROR;
	}

	res = mcinstall_check_result(dict, &client->last_error);
	plist_free(dict);

	return res;
}

LIBIMOBILEDEVICE_API int mcinstall_get_status_code(mcinstall_client_t client)
{
	if (!client) {
		return -1;
	}
	return client->last_error;
}