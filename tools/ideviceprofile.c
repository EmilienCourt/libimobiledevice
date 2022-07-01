/*
 * ideviceprofile.c
 * Simple utility to install, get, or remove configuration profiles
 *   to/from idevices
 *
 * Copyright (c) 2012-2016 Nikias Bassen, All Rights Reserved.
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

#define TOOL_NAME "ideviceprofile"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#ifndef WIN32
#include <signal.h>
#endif

#ifdef WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#endif

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/mcinstall.h>
#include <libimobiledevice-glue/utils.h>

static void print_usage(int argc, char **argv, int is_error)
{
	char *name = strrchr(argv[0], '/');
	fprintf(is_error ? stderr : stdout, "Usage: %s [OPTIONS] COMMAND\n", (name ? name + 1: argv[0]));
	fprintf(is_error ? stderr : stdout,
		"\n"
		"Manage configuration profiles on a device.\n"
		"\n"
		"Where COMMAND is one of:\n"
		"  install FILE  Installs the configuration profile specified by FILE.\n"
		"                A valid .mobileconfig file is expected.\n"
		"  list          Get a list of all configuration profiles on the device.\n"
		"  remove IDENTIFIER   Removes the configuration profile identified by IDENTIFIER.\n"
		"  remove-all    Removes all installed configuration profiles.\n"
		"\n"
		"The following OPTIONS are accepted:\n"
		"  -u, --udid UDID       target specific device by UDID\n"
		"  -n, --network         connect to network device\n"
		"  -d, --debug           enable communication debugging\n"
		"  -h, --help            prints usage information\n"
		"  -v, --version         prints version information\n"
		"\n"
		"Homepage:    <" PACKAGE_URL ">\n"
		"Bug Reports: <" PACKAGE_BUGREPORT ">\n"
	);
}

enum {
	OP_INSTALL,
	OP_LIST,
	OP_REMOVE,
	OP_REMOVE_ALL,
	NUM_OPS
};

static int profile_read_from_file(const char* path, unsigned char **profile_data, unsigned int *profile_size)
{
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Could not open file '%s'\n", path);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	long int size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size >= 0x1000000) {
		fprintf(stderr, "The file '%s' is too large for processing.\n", path);
		fclose(f);
		return -1;
	}

	unsigned char* buf = malloc(size);
	if (!buf) {
		fprintf(stderr, "Could not allocate memory...\n");
		fclose(f);
		return -1;
	}

	long int cur = 0;
	while (cur < size) {
		ssize_t r = fread(buf+cur, 1, 512, f);
		if (r <= 0) {
			break;
		}
		cur += r;
	}
	fclose(f);

	if (cur != size) {
		free(buf);
		fprintf(stderr, "Could not read in file '%s' (size %ld read %ld)\n", path, size, cur);
		return -1;
	}

	*profile_data = buf;
	*profile_size = (unsigned int)size;

	return 0;
}

int main(int argc, char *argv[])
{
	lockdownd_client_t client = NULL;
	lockdownd_error_t ldret = LOCKDOWN_E_UNKNOWN_ERROR;
	lockdownd_service_descriptor_t service = NULL;
	idevice_t device = NULL;
	idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
	int res = 0;
	int i;
	int op = -1;
	const char* udid = NULL;
	const char* param = NULL;
	int use_network = 0;
	int c = 0;
	const struct option longopts[] = {
		{ "debug", no_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ "udid", required_argument, NULL, 'u' },
		{ "network", no_argument, NULL, 'n' },
		{ "version", no_argument, NULL, 'v' },
		{ NULL, 0, NULL, 0}
	};

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	/* parse cmdline args */
	while ((c = getopt_long(argc, argv, "dhu:nv", longopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			idevice_set_debug_level(1);
			break;
		case 'u':
			if (!*optarg) {
				fprintf(stderr, "ERROR: UDID argument must not be empty!\n");
				print_usage(argc, argv, 1);
				return 2;
			}
			udid = optarg;
			break;
		case 'n':
			use_network = 1;
			break;
		case 'h':
			print_usage(argc, argv, 0);
			return 0;
		case 'v':
			printf("%s %s\n", TOOL_NAME, PACKAGE_VERSION);
			return 0;
		default:
			print_usage(argc, argv, 1);
			return 2;
		}
	}
	argc -= optind;
	argv += optind;

	if (!argv[0]) {
		fprintf(stderr, "ERROR: Missing command.\n");
		print_usage(argc+optind, argv-optind, 1);
		return 2;
	}

	i = 0;
	if (!strcmp(argv[i], "install")) {
		op = OP_INSTALL;
		i++;
		if (!argv[i] || !*argv[i]) {
			fprintf(stderr, "Missing argument for 'install' command.\n");
			print_usage(argc+optind, argv-optind, 1);
			return 2;
		}
		param = argv[i];
	}
	else if (!strcmp(argv[i], "list")) {
		op = OP_LIST;
	}
	else if (!strcmp(argv[i], "remove")) {
		op = OP_REMOVE;
		i++;
		if (!argv[i] || !*argv[i]) {
			fprintf(stderr, "Missing argument for 'remove' command.\n");
			print_usage(argc+optind, argv-optind, 1);
			return 2;
		}
		param = argv[i];
	}
	else if (!strcmp(argv[i], "remove-all")) {
		op = OP_REMOVE_ALL;
	}
	if ((op == -1) || (op >= NUM_OPS)) {
		fprintf(stderr, "ERROR: Unsupported command '%s'\n", argv[i]);
		print_usage(argc+optind, argv-optind, 1);
		return 2;
	}

	ret = idevice_new_with_options(&device, udid, (use_network) ? IDEVICE_LOOKUP_NETWORK : IDEVICE_LOOKUP_USBMUX);
	if (ret != IDEVICE_E_SUCCESS) {
		if (udid) {
			printf("No device found with udid %s.\n", udid);
		} else {
			printf("No device found.\n");
		}
		return -1;
	}

	if (LOCKDOWN_E_SUCCESS != (ldret = lockdownd_client_new_with_handshake(device, &client, TOOL_NAME))) {
		fprintf(stderr, "ERROR: Could not connect to lockdownd, error code %d\n", ldret);
		idevice_free(device);
		return -1;
	}

	lockdownd_error_t lerr = lockdownd_start_service(client, MCINSTALL_SERVICE_NAME, &service);
	if (lerr != LOCKDOWN_E_SUCCESS) {
		fprintf(stderr, "Could not start service %s: %s\n", MCINSTALL_SERVICE_NAME, lockdownd_strerror(lerr));
		lockdownd_client_free(client);
		idevice_free(device);
		return -1;
	}
	lockdownd_client_free(client);
	client = NULL;

	mcinstall_client_t mis = NULL;
	if (mcinstall_client_new(device, service, &mis) != MCINSTALL_E_SUCCESS) {
		fprintf(stderr, "Could not connect to %s on device\n", MCINSTALL_SERVICE_NAME);
		if (service)
			lockdownd_service_descriptor_free(service);
		lockdownd_client_free(client);
		idevice_free(device);
		return -1;
	}

	if (service)
		lockdownd_service_descriptor_free(service);

	switch (op) {
		case OP_INSTALL:
		{
			unsigned char* profile_data = NULL;
			unsigned int profile_size = 0;
			if (profile_read_from_file(param, &profile_data, &profile_size) != 0) {
				break;
			}

			uint64_t psize = profile_size;
			plist_t pdata = plist_new_data((const char*)profile_data, psize);
			free(profile_data);

			if (mcinstall_install(mis, pdata) == MCINSTALL_E_SUCCESS) {
				printf("Profile '%s' installed successfully.\n", param);
			} else {
				int sc = mcinstall_get_status_code(mis);
				fprintf(stderr, "Could not install profile '%s', status code: 0x%x\n", param, sc);
			}
		}
			break;
		case OP_REMOVE:
		case OP_REMOVE_ALL:
		case OP_LIST:
		{
			plist_t profiles = NULL;
			mcinstall_error_t merr;
			merr = mcinstall_get_profile_list(mis, &profiles);
			if (merr == MCINSTALL_E_SUCCESS) {
				plist_t ordered_identifiers = NULL;
				plist_t profile_metadata = NULL;
				ordered_identifiers = plist_dict_get_item(profiles, "OrderedIdentifiers");
				profile_metadata = plist_dict_get_item(profiles, "ProfileMetadata");
				if (ordered_identifiers && (plist_get_node_type(ordered_identifiers) == PLIST_ARRAY) && profile_metadata && (plist_get_node_type(profile_metadata) == PLIST_DICT)){
					uint32_t num_profiles = plist_array_get_size(ordered_identifiers);
					if (op == OP_LIST) {
						printf("Device has %d configuration %s installed%s\n", num_profiles, (num_profiles == 1) ? "profile" : "profiles", (num_profiles == 0) ? "." : ":");
					}
					uint32_t j;
					for (j = 0; j < num_profiles; j++) {
						plist_t pl = NULL;
						char* p_iden = NULL;
						char* p_name = NULL;
						char* p_uuid = NULL;
						uint64_t p_vers = 0;
						pl = plist_array_get_item(ordered_identifiers, j);
						if (pl && (plist_get_node_type(pl) == PLIST_STRING)) {
							plist_get_string_val(pl, &p_iden);
						}
						plist_t profile = plist_dict_get_item(profile_metadata, p_iden);
						if (profile && (plist_get_node_type(profile) == PLIST_DICT)) {
							plist_t node;
							node = plist_dict_get_item(profile, "PayloadDisplayName");
							if (node && (plist_get_node_type(node) == PLIST_STRING)) {
								plist_get_string_val(node, &p_name);
							}
							node = plist_dict_get_item(profile, "PayloadUUID");
							if (node && (plist_get_node_type(node) == PLIST_STRING)) {
								plist_get_string_val(node, &p_uuid);
							}
							node = plist_dict_get_item(profile, "PayloadVersion");
							if (node && (plist_get_node_type(node) == PLIST_UINT)) {
								plist_get_uint_val(node, &p_vers);
							}
						}
						if (op == OP_LIST){
							printf("%s - %s - %s\n", (p_iden) ? p_iden : "(unknown identifier)", (p_uuid) ? p_uuid : "(unknown id)", (p_name) ? p_name : "(no name)");
						}
						else if (op == OP_REMOVE && param){
							/* remove specified configuration profile */
							if (strcmp(param, p_iden) == 0){
								if (mcinstall_remove(mis, p_iden, p_uuid, p_vers) == MCINSTALL_E_SUCCESS) {
									printf("Profile '%s' removed.\n", param);
								} else {
									int sc = mcinstall_get_status_code(mis);
									fprintf(stderr, "Could not remove profile '%s', status code 0x%x\n", param, sc);
								}
							}
						}
						else if (op == OP_REMOVE_ALL){
							if (mcinstall_remove(mis, p_iden, p_uuid, p_vers) == MCINSTALL_E_SUCCESS) {
								printf("Profile '%s' removed.\n", p_iden);
							} else {
								int sc = mcinstall_get_status_code(mis);
								fprintf(stderr, "Could not remove profile '%s', status code 0x%x\n", p_iden, sc);
							}
						}
						free(p_iden);
						free(p_uuid);
						free(p_name);
					}
					plist_free(profile_metadata);
					plist_free(ordered_identifiers);
				}
				else {
					fprintf(stderr, "Malformed output from mcinstall.\n");
					res = -1;
				}
			} else {
				int sc = mcinstall_get_status_code(mis);
				fprintf(stderr, "Could not get installed profiles from device, status code: 0x%x\n", sc);
				res = -1;
			}
			plist_free(profiles);
		}
			break;
		default:
			break;
	}

	mcinstall_client_free(mis);

	idevice_free(device);

	return res;
}