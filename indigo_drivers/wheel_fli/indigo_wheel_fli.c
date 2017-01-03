// Copyright (C) 2016 Rumen G. Bogdanovski
// All rights reserved.
//
// You can use this software under the terms of 'INDIGO Astronomy
// open-source license' (see LICENSE.md).
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS 'AS IS' AND ANY EXPRESS
// OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// version history
// 2.0 Build 0 - PoC by Rumen G. Bogdanovski

/** INDIGO FLI filter wheel driver
 \file indigo_wheel_fli.c
 */

#define DRIVER_VERSION 0x0001

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

#if defined(INDIGO_FREEBSD)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#define MAX_PATH 255
#define EFW_ID_MAX 0
#include <libfli/libfli.h>
#include "indigo_driver_xml.h"
#include "indigo_wheel_fli.h"

#define ASI_VENDOR_ID                   0x0f18

#undef PRIVATE_DATA
#define PRIVATE_DATA        ((asi_private_data *)DEVICE_CONTEXT->private_data)

typedef struct {
	flidev_t dev_id;
	char dev_file_name[MAX_PATH];
	char dev_name[MAX_PATH];
	flidomain_t domain;
	int current_slot, target_slot;
	int count;
} asi_private_data;

static int find_index_by_device_fname(char *fname);
// -------------------------------------------------------------------------------- INDIGO Wheel device implementation


static void wheel_timer_callback(indigo_device *device) {
	//TODO GET POSITION
	//EFWGetPosition(PRIVATE_DATA->dev_id, &(PRIVATE_DATA->current_slot));
	PRIVATE_DATA->current_slot++;
	WHEEL_SLOT_ITEM->number.value = PRIVATE_DATA->current_slot;
	if (PRIVATE_DATA->current_slot == PRIVATE_DATA->target_slot) {
		WHEEL_SLOT_PROPERTY->state = INDIGO_OK_STATE;
	} else {
		indigo_set_timer(device, 0.5, wheel_timer_callback);
	}
	indigo_update_property(device, WHEEL_SLOT_PROPERTY, NULL);
}


static indigo_result wheel_attach(indigo_device *device) {
	assert(device != NULL);
	assert(device->device_context != NULL);
	asi_private_data *private_data = device->device_context;
	device->device_context = NULL;
	if (indigo_wheel_attach(device, DRIVER_VERSION) == INDIGO_OK) {
		DEVICE_CONTEXT->private_data = private_data;
		return indigo_wheel_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}


static indigo_result wheel_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(device->device_context != NULL);
	assert(property != NULL);
	//EFW_INFO info;
	if (indigo_property_match(CONNECTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- CONNECTION
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);

		int index = find_index_by_device_fname(PRIVATE_DATA->dev_file_name);
		if (index < 0) {
			return INDIGO_NOT_FOUND;
		}

		if (CONNECTION_CONNECTED_ITEM->sw.value) {
			//TODO OPEN 
			//EFWGetID(index, &(PRIVATE_DATA->dev_id));
			//int res = EFWOpen(PRIVATE_DATA->dev_id);
			int res;
			if (!res) {
				//TODO GET INFO NEEDED
				//EFWGetProperty(PRIVATE_DATA->dev_id, &info);
				//WHEEL_SLOT_ITEM->number.max = WHEEL_SLOT_NAME_PROPERTY->count = PRIVATE_DATA->count = info.slotNum;
				//EFWGetPosition(PRIVATE_DATA->dev_id, &(PRIVATE_DATA->target_slot));
				PRIVATE_DATA->target_slot++;
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
				indigo_set_timer(device, 0.5, wheel_timer_callback);
			} else {
				INDIGO_LOG(indigo_log("indigo_wheel_fli: EFWOpen(%d) = %d", index, res));
				CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
				return INDIGO_FAILED;
			}
		} else {
			//TODO CLOSE
			//EFWClose(PRIVATE_DATA->dev_id);
			//EFWGetID(index, &(PRIVATE_DATA->dev_id));
			CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
		}
		
		CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
	} else if (indigo_property_match(WHEEL_SLOT_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- WHEEL_SLOT
		indigo_property_copy_values(WHEEL_SLOT_PROPERTY, property, false);
		if (WHEEL_SLOT_ITEM->number.value < 1 || WHEEL_SLOT_ITEM->number.value > WHEEL_SLOT_ITEM->number.max) {
			WHEEL_SLOT_PROPERTY->state = INDIGO_ALERT_STATE;
		} else if (WHEEL_SLOT_ITEM->number.value == PRIVATE_DATA->current_slot) {
			WHEEL_SLOT_PROPERTY->state = INDIGO_OK_STATE;
		} else {
			WHEEL_SLOT_PROPERTY->state = INDIGO_BUSY_STATE;
			PRIVATE_DATA->target_slot = WHEEL_SLOT_ITEM->number.value;
			WHEEL_SLOT_ITEM->number.value = PRIVATE_DATA->current_slot;
			//TODO SET_POSITION 
			//EFWSetPosition(PRIVATE_DATA->dev_id, PRIVATE_DATA->target_slot-1);
			indigo_set_timer(device, 0.5, wheel_timer_callback);
		}
		indigo_update_property(device, WHEEL_SLOT_PROPERTY, NULL);
		return INDIGO_OK;
		// --------------------------------------------------------------------------------
	}
	return indigo_wheel_change_property(device, client, property);
}


static indigo_result wheel_detach(indigo_device *device) {
	assert(device != NULL);
	indigo_device_disconnect(NULL, device);
	INDIGO_LOG(indigo_log("indigo_wheel_fli: '%s' detached.", device->name));
	return indigo_wheel_detach(device);
}

// -------------------------------------------------------------------------------- hot-plug support

#define MAX_DEVICES                   32
#define NO_DEVICE                 (-1000)

flidomain_t enum_domain = FLIDOMAIN_USB | FLIDEVICE_FILTERWHEEL;
int num_devices = 0;
char fli_file_names[MAX_DEVICES][MAX_PATH] = {""};
char fli_dev_names[MAX_DEVICES][MAX_PATH] = {""};
flidomain_t fli_domains[MAX_DEVICES] = {0};

static int efw_products[100];
static int efw_id_count = 0;


static indigo_device *devices[MAX_DEVICES] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static bool connected_ids[EFW_ID_MAX];


static int enumerate_devices() {
	num_devices = 0;
	FLICreateList(enum_domain);
	if(FLIListFirst(&fli_domains[num_devices], fli_file_names[num_devices], MAX_PATH, fli_dev_names[num_devices], MAX_PATH) == 0) {
		do {
			num_devices++;
		} while((FLIListNext(&fli_domains[num_devices], fli_file_names[num_devices], MAX_PATH, fli_dev_names[num_devices], MAX_PATH) == 0) && (num_devices < MAX_DEVICES));
	}
	FLIDeleteList();
}

static int find_plugged_device(char *fname) {
	enumerate_devices();
	for (int dev_no = 0; dev_no < num_devices; dev_no++) {
		bool found = false;
		for(int slot = 0; slot < MAX_DEVICES; slot++) {
			indigo_device *device = devices[slot];
			if (device == NULL) continue;
			if (!strncmp(PRIVATE_DATA->dev_file_name, fli_file_names[dev_no], MAX_PATH)) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		} else {
			assert(fname==NULL);
			strncpy(fname, fli_file_names[dev_no], MAX_PATH);
			return dev_no;
		}
	}
	return -1;
}


static int find_index_by_device_fname(char *fname) {
	for (int dev_no = 0; dev_no < num_devices; dev_no++) {
		if (!strncmp(fli_file_names[dev_no], fname, MAX_PATH)) {
			return dev_no;
		}
	}
	return -1;
}


static int find_available_device_slot() {
	for(int slot = 0; slot < MAX_DEVICES; slot++) {
		if (devices[slot] == NULL) return slot;
	}
	return -1;
}


static int find_device_slot(char *fname) {
	for(int slot = 0; slot < MAX_DEVICES; slot++) {
		indigo_device *device = devices[slot];
		if (device == NULL) continue;
		if (!strncmp(PRIVATE_DATA->dev_file_name, fname, 255)) return slot;
	}
	return -1;
}


static int find_unplugged_device(char *fname) {
	enumerate_devices();
	for(int slot = 0; slot < MAX_DEVICES; slot++) {
		bool found = false;
		for (int dev_no = 0; dev_no < num_devices; dev_no++) {
			indigo_device *device = devices[slot];
			if (device == NULL) continue;
			if (!strncmp(PRIVATE_DATA->dev_file_name, fli_file_names[dev_no], MAX_PATH)) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		} else {
			assert(fname==NULL);
			strncpy(fname, fli_file_names[slot], MAX_PATH);
			return slot;
		}
	}
	return -1;
}


static int hotplug_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data) {
	//EFW_INFO info;

	static indigo_device wheel_template = {
		"", NULL, INDIGO_OK, INDIGO_VERSION_CURRENT,
		wheel_attach,
		indigo_wheel_enumerate_properties,
		wheel_change_property,
		wheel_detach
	};

	struct libusb_device_descriptor descriptor;

	switch (event) {
		case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED: {
			INDIGO_DEBUG_DRIVER(int rc =) libusb_get_device_descriptor(dev, &descriptor);
			for (int i = 0; i < efw_id_count; i++) {
				if (descriptor.idVendor != ASI_VENDOR_ID || efw_products[i] != descriptor.idProduct) continue;

				int slot = find_available_device_slot();
				if (slot < 0) {
					INDIGO_LOG(indigo_log("indigo_wheel_fli: No available device slots available."));
					return 0;
				}

				char file_name[MAX_PATH];
				int id = find_plugged_device(file_name);
				if (id == NO_DEVICE) {
					INDIGO_LOG(indigo_log("indigo_wheel_fli: No plugged device found."));
					return 0;
				}

				indigo_device *device = malloc(sizeof(indigo_device));
				//EFWGetProperty(id, &info);
				assert(device != NULL);
				memcpy(device, &wheel_template, sizeof(indigo_device));
				//sprintf(device->name, "%s #%d", info.Name, id);
				INDIGO_LOG(indigo_log("indigo_wheel_fli: '%s' attached.", device->name));
				device->device_context = malloc(sizeof(asi_private_data));
				assert(device->device_context);
				memset(device->device_context, 0, sizeof(asi_private_data));
				((asi_private_data*)device->device_context)->dev_id = id;
				indigo_attach_device(device);
				devices[slot]=device;
			}
			break;
		}
		case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT: {
			int slot, id;
			char file_name[MAX_PATH];
			bool removed = false;
			while ((id = find_unplugged_device(file_name)) != -1) {
				slot = find_device_slot(file_name);
				if (slot < 0) continue;
				indigo_device **device = &devices[slot];
				if (*device == NULL)
					return 0;
				indigo_detach_device(*device);
				free((*device)->device_context);
				free(*device);
				libusb_unref_device(dev);
				*device = NULL;
				removed = true;
			}
			if (!removed) {
				INDIGO_LOG(indigo_log("indigo_wheel_fli: No FLI FW device unplugged (maybe ASI Camera)!"));
			}
		}
	}
	return 0;
};


static void remove_all_devices() {
	int i;
	for(i = 0; i < MAX_DEVICES; i++) {
		indigo_device **device = &devices[i];
		if (*device == NULL) continue;
		indigo_detach_device(*device);
		free((*device)->device_context);
		free(*device);
		*device = NULL;
	}
	for(i = 0; i < EFW_ID_MAX; i++)
		connected_ids[i] = false;
}


static libusb_hotplug_callback_handle callback_handle;

indigo_result indigo_wheel_fli(indigo_driver_action action, indigo_driver_info *info) {
	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, "FLI Filter Wheel", __FUNCTION__, DRIVER_VERSION, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch (action) {
	case INDIGO_DRIVER_INIT:
		last_action = action;
		//efw_id_count = EFWGetProductIDs(efw_products);
		if (efw_id_count <= 0) {
			INDIGO_LOG(indigo_log("indigo_wheel_fli: Can not get the list of supported IDs."));
			return INDIGO_FAILED;
		}
		indigo_start_usb_event_handler();
		int rc = libusb_hotplug_register_callback(NULL, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, LIBUSB_HOTPLUG_ENUMERATE, ASI_VENDOR_ID, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, hotplug_callback, NULL, &callback_handle);
		INDIGO_DEBUG_DRIVER(indigo_debug("indigo_wheel_fli: libusb_hotplug_register_callback [%d] ->  %s", __LINE__, rc < 0 ? libusb_error_name(rc) : "OK"));
		return rc >= 0 ? INDIGO_OK : INDIGO_FAILED;

	case INDIGO_DRIVER_SHUTDOWN:
		last_action = action;
		libusb_hotplug_deregister_callback(NULL, callback_handle);
		INDIGO_DEBUG_DRIVER(indigo_debug("indigo_wheel_fli: libusb_hotplug_deregister_callback [%d]", __LINE__));
		remove_all_devices();
		break;

	case INDIGO_DRIVER_INFO:
		break;
	}

	return INDIGO_OK;
}
