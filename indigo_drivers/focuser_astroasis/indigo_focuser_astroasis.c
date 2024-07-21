// Copyright (C) 2024 Astroasis Vision Technology, Inc.
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
// 2.0 by Frank Chen <frank.chen@astroasis.com>

/** INDIGO Astroasis focuser driver
 \file indigo_focuser_astroasis.c
 */

#define DRIVER_VERSION 0x0001
#define DRIVER_NAME "indigo_focuser_astroasis"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

#include <indigo/indigo_driver_xml.h>
#include "indigo_focuser_astroasis.h"

#if !(defined(__APPLE__) || defined(__i386__))

#if defined(INDIGO_FREEBSD)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#include <AOFocus.h>

#define ASTROASIS_VENDOR_ID			0x338f
#define ASTROASIS_PRODUCT_FOCUSER_ID		0xa0f0

#define PRIVATE_DATA				((astroasis_private_data *)device->private_data)

typedef struct {
	int dev_id;
	AOFocuserConfig config;
	AOFocuserStatus status;
	char sdk_version[AO_FOCUSER_VERSION_LEN + 1];
	char firmware_version[AO_FOCUSER_VERSION_LEN + 1];
	char model[AO_FOCUSER_NAME_LEN + 1];
	char friendly_name[AO_FOCUSER_NAME_LEN + 1];
	char bluetooth_name[AO_FOCUSER_NAME_LEN + 1];
	double compensation_last_temp;
	indigo_timer *focuser_timer, *temperature_timer;
	indigo_property *beep_on_power_up_property;
	indigo_property *beep_on_move_property;
	indigo_property *backlash_direction_property;
	indigo_property *friendly_name_property;
	indigo_property *bluetooth_property;
	indigo_property *bluetooth_name_property;
	indigo_property *board_temperature_property;
} astroasis_private_data;

#define BEEP_ON_POWER_UP_PROPERTY		(PRIVATE_DATA->beep_on_power_up_property)
#define BEEP_ON_POWER_UP_ON_ITEM		(BEEP_ON_POWER_UP_PROPERTY->items+0)
#define BEEP_ON_POWER_UP_OFF_ITEM		(BEEP_ON_POWER_UP_PROPERTY->items+1)
#define BEEP_ON_POWER_UP_PROPERTY_NAME		"BEEP_ON_POWER_UP_PROPERTY"
#define BEEP_ON_POWER_UP_ON_ITEM_NAME		"ON"
#define BEEP_ON_POWER_UP_OFF_ITEM_NAME		"OFF"

#define BEEP_ON_MOVE_PROPERTY			(PRIVATE_DATA->beep_on_move_property)
#define BEEP_ON_MOVE_ON_ITEM			(BEEP_ON_MOVE_PROPERTY->items+0)
#define BEEP_ON_MOVE_OFF_ITEM			(BEEP_ON_MOVE_PROPERTY->items+1)
#define BEEP_ON_MOVE_PROPERTY_NAME		"BEEP_ON_MOVE_PROPERTY"
#define BEEP_ON_MOVE_ON_ITEM_NAME		"ON"
#define BEEP_ON_MOVE_OFF_ITEM_NAME		"OFF"

#define BACKLASH_DIRECTION_PROPERTY		(PRIVATE_DATA->backlash_direction_property)
#define BACKLASH_DIRECTION_IN_ITEM		(BACKLASH_DIRECTION_PROPERTY->items+0)
#define BACKLASH_DIRECTION_OUT_ITEM		(BACKLASH_DIRECTION_PROPERTY->items+1)
#define BACKLASH_DIRECTION_PROPERTY_NAME	"BACKLASH_DIRECTION_PROPERTY"
#define BACKLASH_DIRECTION_IN_ITEM_NAME		"INWARD"
#define BACKLASH_DIRECTION_OUT_ITEM_NAME	"OUTWARD"

#define FRIENDLY_NAME_PROPERTY			(PRIVATE_DATA->friendly_name_property)
#define FRIENDLY_NAME_ITEM			(FRIENDLY_NAME_PROPERTY->items+0)
#define FRIENDLY_NAME_PROPERTY_NAME		"FRIENDLY_NAME_PROPERTY"
#define FRIENDLY_NAME_NAME			"FRIENDLY_NAME"

#define BLUETOOTH_PROPERTY			(PRIVATE_DATA->bluetooth_property)
#define BLUETOOTH_ON_ITEM			(BLUETOOTH_PROPERTY->items+0)
#define BLUETOOTH_OFF_ITEM			(BLUETOOTH_PROPERTY->items+1)
#define BLUETOOTH_PROPERTY_NAME			"BLUETOOTH_PROPERTY"
#define BLUETOOTH_ON_ITEM_NAME			"ENABLED"
#define BLUETOOTH_OFF_ITEM_NAME			"DISABLED"

#define BLUETOOTH_NAME_PROPERTY			(PRIVATE_DATA->bluetooth_name_property)
#define BLUETOOTH_NAME_ITEM			(BLUETOOTH_NAME_PROPERTY->items+0)
#define BLUETOOTH_NAME_PROPERTY_NAME		"BLUETOOTH_NAME_PROPERTY"
#define BLUETOOTH_NAME_NAME			"BLUETOOTH_NAME"

#define FOCUSER_TEMPERATURE_BOARD_PROPERTY	(PRIVATE_DATA->board_temperature_property)
#define FOCUSER_TEMPERATURE_BOARD_ITEM		(FOCUSER_TEMPERATURE_BOARD_PROPERTY->items+0)
#define FOCUSER_TEMPERATURE_BOARD_PROPERTY_NAME	"BOARD_TEMPERATURE_PROPERTY"
#define FOCUSER_TEMPERATURE_BOARD_ITEM_NAME	"BOARD_TEMPERATURE"

// INDIGO focuser device implementation
static bool focuser_config(indigo_device *device, unsigned int mask, int value)
{
	AOFocuserConfig *config = &PRIVATE_DATA->config;

	config->mask = mask;

	switch (mask) {
	case MASK_MAX_STEP:
		config->maxStep = value;
		break;
	case MASK_BACKLASH:
		config->backlash = value;
		break;
	case MASK_BACKLASH_DIRECTION:
		config->backlashDirection = value;
		break;
	case MASK_REVERSE_DIRECTION:
		config->reverseDirection = value;
		break;
	case MASK_SPEED:
		config->speed = value;
		break;
	case MASK_BEEP_ON_MOVE:
		config->beepOnMove = value;
		break;
	case MASK_BEEP_ON_STARTUP:
		config->beepOnStartup = value;
		break;
	case MASK_BLUETOOTH:
		config->bluetoothOn = value;
		break;
	default:
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "Invalid Oasis Focuser configuration mask %08X\n", mask);
		return false;
	}

	AOReturn ret = AOFocuserSetConfig(PRIVATE_DATA->dev_id, config);

	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to set Oasis Focuser configuration, ret = %d\n", ret);
		return false;
	}

	return true;
}

static void focuser_timer_callback(indigo_device *device) {
	AOReturn ret = AOFocuserGetStatus(PRIVATE_DATA->dev_id, &PRIVATE_DATA->status);

	if (ret == AO_SUCCESS) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Moving = %d, Position = %d", PRIVATE_DATA->status.moving, PRIVATE_DATA->status.position);

		FOCUSER_POSITION_ITEM->number.value = PRIVATE_DATA->status.position;
		if (!PRIVATE_DATA->status.moving) {
			FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
			FOCUSER_STEPS_PROPERTY->state = INDIGO_OK_STATE;
		} else {
			indigo_reschedule_timer(device, 0.5, &(PRIVATE_DATA->focuser_timer));
		}
	} else {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetStatus() failed, ret = %d", ret);
		FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
		FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
	}

	indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
	indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
}

static void focuser_compensation(indigo_device *device, double curr_temp) {
	int compensation;
	double temp_diff = curr_temp - PRIVATE_DATA->compensation_last_temp;

	/* Last compensation temperature is invalid */
	if (PRIVATE_DATA->compensation_last_temp < -270) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Compensation not started yet, last temperature = %f", PRIVATE_DATA->compensation_last_temp);
		PRIVATE_DATA->compensation_last_temp = curr_temp;
		return;
	}

	/* Current temperature is invalid or focuser is moving */
	if ((curr_temp < -270) || (FOCUSER_POSITION_PROPERTY->state != INDIGO_OK_STATE)) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Compensation not started: curr_temp = %f, FOCUSER_POSITION_PROPERTY->state = %d", curr_temp, FOCUSER_POSITION_PROPERTY->state);
		return;
	}

	/* Temperature difference is big enough to do compensation */
	if ((fabs(temp_diff) >= FOCUSER_COMPENSATION_THRESHOLD_ITEM->number.value) && (fabs(temp_diff) < 100)) {
		compensation = (int)(temp_diff * FOCUSER_COMPENSATION_ITEM->number.value);
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Compensation: temperature difference = %.2f, ompensation = %d, steps/degC = %.0f, threshold = %.2f",
			temp_diff, compensation, FOCUSER_COMPENSATION_ITEM->number.value, FOCUSER_COMPENSATION_THRESHOLD_ITEM->number.value);
	} else {
		return;
	}

	AOReturn ret = AOFocuserMove(PRIVATE_DATA->dev_id, compensation);
	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to move Oasis Focuser, ret = %d\n", ret);
		FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
	}

	PRIVATE_DATA->compensation_last_temp = curr_temp;
	FOCUSER_POSITION_ITEM->number.value = PRIVATE_DATA->status.position;
	FOCUSER_POSITION_PROPERTY->state = INDIGO_BUSY_STATE;
	indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
	indigo_set_timer(device, 0.5, focuser_timer_callback, &PRIVATE_DATA->focuser_timer);
}

static void temperature_timer_callback(indigo_device *device) {
	AOReturn ret = AOFocuserGetStatus(PRIVATE_DATA->dev_id, &PRIVATE_DATA->status);

	if (ret == AO_SUCCESS) {
		FOCUSER_TEMPERATURE_BOARD_ITEM->number.value = (double)PRIVATE_DATA->status.temperatureInt / 100;
		FOCUSER_TEMPERATURE_BOARD_PROPERTY->state = INDIGO_OK_STATE;

		if (PRIVATE_DATA->status.temperatureDetection && (PRIVATE_DATA->status.temperatureExt != TEMPERATURE_INVALID)) {
			FOCUSER_TEMPERATURE_ITEM->number.value = (double)PRIVATE_DATA->status.temperatureExt / 100;
			FOCUSER_TEMPERATURE_PROPERTY->state = INDIGO_OK_STATE;

			if (FOCUSER_MODE_AUTOMATIC_ITEM->sw.value) {
				focuser_compensation(device, (double)PRIVATE_DATA->status.temperatureExt / 100);
			} else {
				/* reset temp so that the compensation starts when auto mode is selected */
				PRIVATE_DATA->compensation_last_temp = -273.15;
			}
		} else {
			FOCUSER_TEMPERATURE_ITEM->number.value = -273.15;
			FOCUSER_TEMPERATURE_PROPERTY->state = PRIVATE_DATA->status.temperatureDetection ? INDIGO_ALERT_STATE : INDIGO_IDLE_STATE;
		}

	} else {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetStatus() failed, ret = %d", ret);
		FOCUSER_TEMPERATURE_BOARD_PROPERTY->state = INDIGO_ALERT_STATE;
		FOCUSER_TEMPERATURE_PROPERTY->state = INDIGO_ALERT_STATE;
	}

	indigo_update_property(device, FOCUSER_TEMPERATURE_BOARD_PROPERTY, NULL);
	indigo_update_property(device, FOCUSER_TEMPERATURE_PROPERTY, NULL);

	indigo_reschedule_timer(device, 2, &(PRIVATE_DATA->temperature_timer));
}

static indigo_result focuser_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property) {
	if (IS_CONNECTED) {
		if (indigo_property_match(BEEP_ON_POWER_UP_PROPERTY, property))
			indigo_define_property(device, BEEP_ON_POWER_UP_PROPERTY, NULL);
		if (indigo_property_match(BEEP_ON_MOVE_PROPERTY, property))
			indigo_define_property(device, BEEP_ON_MOVE_PROPERTY, NULL);
		if (indigo_property_match(BACKLASH_DIRECTION_PROPERTY, property))
			indigo_define_property(device, BACKLASH_DIRECTION_PROPERTY, NULL);
		if (indigo_property_match(FRIENDLY_NAME_PROPERTY, property))
			indigo_define_property(device, FRIENDLY_NAME_PROPERTY, NULL);
		if (indigo_property_match(BLUETOOTH_PROPERTY, property))
			indigo_define_property(device, BLUETOOTH_PROPERTY, NULL);
		if (indigo_property_match(BLUETOOTH_NAME_PROPERTY, property))
			indigo_define_property(device, BLUETOOTH_NAME_PROPERTY, NULL);
		if (indigo_property_match(FOCUSER_TEMPERATURE_BOARD_PROPERTY, property))
			indigo_define_property(device, FOCUSER_TEMPERATURE_BOARD_PROPERTY, NULL);
	}

	return indigo_focuser_enumerate_properties(device, NULL, NULL);
}

static indigo_result focuser_attach(indigo_device *device) {
	assert(device != NULL);
	assert(PRIVATE_DATA != NULL);

	if (indigo_focuser_attach(device, DRIVER_NAME, DRIVER_VERSION) == INDIGO_OK) {
		INFO_PROPERTY->count = 7;

		indigo_copy_value(INFO_DEVICE_MODEL_ITEM->text.value, PRIVATE_DATA->model);
		indigo_copy_value(INFO_DEVICE_FW_REVISION_ITEM->text.value, PRIVATE_DATA->firmware_version);
		indigo_copy_value(INFO_DEVICE_HW_REVISION_ITEM->text.value, PRIVATE_DATA->sdk_version);
		indigo_copy_value(INFO_DEVICE_HW_REVISION_ITEM->label, "SDK version");

		FOCUSER_LIMITS_PROPERTY->hidden = false;
		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.min = 0;
		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.value = 0;
		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.max = 0x7fffffff;
		FOCUSER_LIMITS_MIN_POSITION_ITEM->number.min = 0;
		FOCUSER_LIMITS_MIN_POSITION_ITEM->number.value = 0;
		FOCUSER_LIMITS_MIN_POSITION_ITEM->number.max = 0;

		FOCUSER_SPEED_PROPERTY->hidden = true;

		FOCUSER_BACKLASH_PROPERTY->hidden = false;
		FOCUSER_BACKLASH_ITEM->number.min = 0;
		FOCUSER_BACKLASH_ITEM->number.max = 10000;
		FOCUSER_BACKLASH_ITEM->number.step = 1;

		FOCUSER_POSITION_ITEM->number.min = 0;
		FOCUSER_POSITION_ITEM->number.step = 1;
		FOCUSER_POSITION_ITEM->number.max = PRIVATE_DATA->config.maxStep;

		FOCUSER_STEPS_ITEM->number.min = 0;
		FOCUSER_STEPS_ITEM->number.step = 1;
		FOCUSER_STEPS_ITEM->number.max = PRIVATE_DATA->config.maxStep;

		FOCUSER_ON_POSITION_SET_PROPERTY->hidden = false;
		FOCUSER_TEMPERATURE_PROPERTY->hidden = false;
		FOCUSER_REVERSE_MOTION_PROPERTY->hidden = false;

		// FOCUSER_COMPENSATION
		FOCUSER_COMPENSATION_PROPERTY->hidden = false;
		FOCUSER_COMPENSATION_ITEM->number.min = -10000;
		FOCUSER_COMPENSATION_ITEM->number.max = 10000;
		FOCUSER_COMPENSATION_PROPERTY->count = 2;

		// FOCUSER_MODE
		FOCUSER_MODE_PROPERTY->hidden = false;

		// BEEP_ON_POWER_UP_PROPERTY
		BEEP_ON_POWER_UP_PROPERTY = indigo_init_switch_property(NULL, device->name, BEEP_ON_POWER_UP_PROPERTY_NAME, "Advanced", "Beep on power up", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (BEEP_ON_POWER_UP_PROPERTY == NULL)
			return INDIGO_FAILED;

		indigo_init_switch_item(BEEP_ON_POWER_UP_ON_ITEM, BEEP_ON_POWER_UP_ON_ITEM_NAME, "On", false);
		indigo_init_switch_item(BEEP_ON_POWER_UP_OFF_ITEM, BEEP_ON_POWER_UP_OFF_ITEM_NAME, "Off", true);

		// BEEP_ON_MOVE_PROPERTY
		BEEP_ON_MOVE_PROPERTY = indigo_init_switch_property(NULL, device->name, BEEP_ON_MOVE_PROPERTY_NAME, "Advanced", "Beep on move", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (BEEP_ON_MOVE_PROPERTY == NULL)
			return INDIGO_FAILED;

		indigo_init_switch_item(BEEP_ON_MOVE_ON_ITEM, BEEP_ON_MOVE_ON_ITEM_NAME, "On", false);
		indigo_init_switch_item(BEEP_ON_MOVE_OFF_ITEM, BEEP_ON_MOVE_OFF_ITEM_NAME, "Off", true);

		// BACKLASH_DIRECTION_PROPERTY
		BACKLASH_DIRECTION_PROPERTY = indigo_init_switch_property(NULL, device->name, BACKLASH_DIRECTION_PROPERTY_NAME, FOCUSER_MAIN_GROUP, "Backlash compensation overshot direction", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (BACKLASH_DIRECTION_PROPERTY == NULL)
			return INDIGO_FAILED;

		indigo_init_switch_item(BACKLASH_DIRECTION_IN_ITEM, BACKLASH_DIRECTION_IN_ITEM_NAME, "Inward", false);
		indigo_init_switch_item(BACKLASH_DIRECTION_OUT_ITEM, BACKLASH_DIRECTION_OUT_ITEM_NAME, "Outward", true);

		// FRIENDLY_NAME_PROPERTY
		FRIENDLY_NAME_PROPERTY = indigo_init_text_property(NULL, device->name, FRIENDLY_NAME_PROPERTY_NAME, "Advanced", "Friendly name", INDIGO_OK_STATE, INDIGO_RW_PERM, 1);
		if (FRIENDLY_NAME_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_text_item(FRIENDLY_NAME_ITEM, FRIENDLY_NAME_NAME, "Friendly name", PRIVATE_DATA->friendly_name);

		// BLUETOOTH_PROPERTY
		BLUETOOTH_PROPERTY = indigo_init_switch_property(NULL, device->name, BLUETOOTH_PROPERTY_NAME, "Advanced", "Bluetooth", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (BLUETOOTH_PROPERTY == NULL)
			return INDIGO_FAILED;

		indigo_init_switch_item(BLUETOOTH_ON_ITEM, BLUETOOTH_ON_ITEM_NAME, "Enabled", false);
		indigo_init_switch_item(BLUETOOTH_OFF_ITEM, BLUETOOTH_OFF_ITEM_NAME, "Disabled", true);

		// BLUETOOTH_NAME_PROPERTY
		BLUETOOTH_NAME_PROPERTY = indigo_init_text_property(NULL, device->name, BLUETOOTH_NAME_PROPERTY_NAME, "Advanced", "Bluetooth name", INDIGO_OK_STATE, INDIGO_RW_PERM, 1);
		if (BLUETOOTH_NAME_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_text_item(BLUETOOTH_NAME_ITEM, BLUETOOTH_NAME_NAME, "Bluetooth name", PRIVATE_DATA->bluetooth_name);

		// FOCUSER_TEMPERATURE and FOCUSER_TEMPERATURE_BOARD
		FOCUSER_TEMPERATURE_BOARD_PROPERTY = indigo_init_number_property(NULL, device->name, FOCUSER_TEMPERATURE_BOARD_PROPERTY_NAME, FOCUSER_MAIN_GROUP, "Temperature 1 (Board)", INDIGO_OK_STATE, INDIGO_RO_PERM, 1);
		FOCUSER_TEMPERATURE_BOARD_PROPERTY->hidden = false;
		indigo_init_number_item(FOCUSER_TEMPERATURE_BOARD_ITEM, "Internal Temp.", "Temperature (°C)", -50, 50, 1, 0);
		indigo_copy_value(FOCUSER_TEMPERATURE_PROPERTY->label, "Temperature 2 (Ambient)");

		return focuser_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static void focuser_connect_callback(indigo_device *device) {
	CONNECTION_PROPERTY->state = INDIGO_OK_STATE;

	if (CONNECTION_CONNECTED_ITEM->sw.value) {
		if (indigo_try_global_lock(device) != INDIGO_OK) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "indigo_try_global_lock(): failed to get lock.");
			CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
			indigo_update_property(device, CONNECTION_PROPERTY, NULL);
		} else {
			AOReturn ret = AOFocuserOpen(PRIVATE_DATA->dev_id);

			if (ret != AO_SUCCESS) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserOpen() failed, ret = %d", ret);
			} else {
				ret = AOFocuserGetConfig(PRIVATE_DATA->dev_id, &PRIVATE_DATA->config);
				
				if (ret != AO_SUCCESS)
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetConfig() failed, ret = %d", ret);
			}

			if (ret == AO_SUCCESS) {
				FOCUSER_LIMITS_MAX_POSITION_ITEM->number.value = (double)PRIVATE_DATA->config.maxStep;
				FOCUSER_BACKLASH_ITEM->number.value = (double)PRIVATE_DATA->config.backlash;
				FOCUSER_REVERSE_MOTION_ENABLED_ITEM->sw.value = PRIVATE_DATA->config.reverseDirection ? true : false;
				FOCUSER_REVERSE_MOTION_DISABLED_ITEM->sw.value = !FOCUSER_REVERSE_MOTION_ENABLED_ITEM->sw.value;

				BEEP_ON_POWER_UP_ON_ITEM->sw.value = PRIVATE_DATA->config.beepOnStartup ? true : false;;
				BEEP_ON_POWER_UP_OFF_ITEM->sw.value = !BEEP_ON_POWER_UP_ON_ITEM->sw.value;

				BEEP_ON_MOVE_ON_ITEM->sw.value = PRIVATE_DATA->config.beepOnMove ? true : false;;
				BEEP_ON_MOVE_OFF_ITEM->sw.value = !BEEP_ON_MOVE_ON_ITEM->sw.value;

				BACKLASH_DIRECTION_IN_ITEM->sw.value = PRIVATE_DATA->config.backlashDirection ? false : true;;
				BACKLASH_DIRECTION_OUT_ITEM->sw.value = !BACKLASH_DIRECTION_IN_ITEM->sw.value;

				BLUETOOTH_ON_ITEM->sw.value = PRIVATE_DATA->config.bluetoothOn ? true : false;;
				BLUETOOTH_OFF_ITEM->sw.value = !BLUETOOTH_ON_ITEM->sw.value;

				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;

				indigo_define_property(device, BEEP_ON_POWER_UP_PROPERTY, NULL);
				indigo_define_property(device, BEEP_ON_MOVE_PROPERTY, NULL);
				indigo_define_property(device, BACKLASH_DIRECTION_PROPERTY, NULL);
				indigo_define_property(device, FRIENDLY_NAME_PROPERTY, NULL);
				indigo_define_property(device, BLUETOOTH_PROPERTY, NULL);
				indigo_define_property(device, BLUETOOTH_NAME_PROPERTY, NULL);
				indigo_define_property(device, FOCUSER_TEMPERATURE_BOARD_PROPERTY, NULL);

				PRIVATE_DATA->compensation_last_temp = -273.15;  /* we do not have previous temperature reading */
				indigo_set_timer(device, 0.5, focuser_timer_callback, &PRIVATE_DATA->focuser_timer);
				indigo_set_timer(device, 0.1, temperature_timer_callback, &PRIVATE_DATA->temperature_timer);
			} else {
				CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
				indigo_update_property(device, CONNECTION_PROPERTY, NULL);
			}
		}
	} else {
		indigo_cancel_timer_sync(device, &PRIVATE_DATA->focuser_timer);
		indigo_cancel_timer_sync(device, &PRIVATE_DATA->temperature_timer);

		indigo_delete_property(device, BEEP_ON_POWER_UP_PROPERTY, NULL);
		indigo_delete_property(device, BEEP_ON_MOVE_PROPERTY, NULL);
		indigo_delete_property(device, BACKLASH_DIRECTION_PROPERTY, NULL);
		indigo_delete_property(device, FRIENDLY_NAME_PROPERTY, NULL);
		indigo_delete_property(device, BLUETOOTH_PROPERTY, NULL);
		indigo_delete_property(device, BLUETOOTH_NAME_PROPERTY, NULL);
		indigo_delete_property(device, FOCUSER_TEMPERATURE_BOARD_PROPERTY, NULL);

		AOReturn ret = AOFocuserStopMove(PRIVATE_DATA->dev_id);
		if (ret != AO_SUCCESS)
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserStopMove() failed, ret = %d", ret);

		AOFocuserClose(PRIVATE_DATA->dev_id);

		indigo_global_unlock(device);
		CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
	}
	indigo_focuser_change_property(device, NULL, CONNECTION_PROPERTY);
}

static indigo_result focuser_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);

	if (indigo_property_match_changeable(CONNECTION_PROPERTY, property)) {
		// CONNECTION
		if (indigo_ignore_connection_change(device, property))
			return INDIGO_OK;
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		CONNECTION_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, CONNECTION_PROPERTY, NULL);
		indigo_set_timer(device, 0, focuser_connect_callback, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_REVERSE_MOTION_PROPERTY, property)) {
		// FOCUSER_REVERSE_MOTION
		indigo_property_copy_values(FOCUSER_REVERSE_MOTION_PROPERTY, property, false);

		if (focuser_config(device, MASK_REVERSE_DIRECTION, FOCUSER_REVERSE_MOTION_ENABLED_ITEM->sw.value))
			FOCUSER_REVERSE_MOTION_PROPERTY->state = INDIGO_OK_STATE;
		else
			FOCUSER_REVERSE_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;

		indigo_update_property(device, FOCUSER_REVERSE_MOTION_PROPERTY, NULL);

		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_POSITION_PROPERTY, property)) {
		// FOCUSER_POSITION
		indigo_property_copy_values(FOCUSER_POSITION_PROPERTY, property, false);
		if (FOCUSER_POSITION_PROPERTY->state == INDIGO_BUSY_STATE) {
			return INDIGO_OK;
		}
		if (FOCUSER_POSITION_ITEM->number.target < 0 || FOCUSER_POSITION_ITEM->number.target > FOCUSER_POSITION_ITEM->number.max) {
			FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
			FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
			indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		} else if (FOCUSER_POSITION_ITEM->number.target == PRIVATE_DATA->status.position) {
			FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
			FOCUSER_STEPS_PROPERTY->state = INDIGO_OK_STATE;
			indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
			indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		} else {
			FOCUSER_POSITION_PROPERTY->state = INDIGO_BUSY_STATE;
			FOCUSER_STEPS_PROPERTY->state = INDIGO_BUSY_STATE;
			FOCUSER_POSITION_ITEM->number.value = PRIVATE_DATA->status.position;
			indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
			indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);

			if (FOCUSER_ON_POSITION_SET_GOTO_ITEM->sw.value) {
				/* Goto position */
				AOReturn ret = AOFocuserMoveTo(PRIVATE_DATA->dev_id, FOCUSER_POSITION_ITEM->number.target);

				if (ret != AO_SUCCESS)
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to move Oasis Focuser, ret = %d\n", ret);

				indigo_set_timer(device, 0.5, focuser_timer_callback, &PRIVATE_DATA->focuser_timer);
			} else {
				/* Sync position */
				AOReturn ret;

				FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
				FOCUSER_STEPS_PROPERTY->state = INDIGO_OK_STATE;

				ret = AOFocuserSyncPosition(PRIVATE_DATA->dev_id, FOCUSER_POSITION_ITEM->number.target);

				if (ret != AO_SUCCESS) {
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to sync Oasis Focuser, ret = %d\n", ret);
					FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
					FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
				}

				ret = AOFocuserGetStatus(PRIVATE_DATA->dev_id, &PRIVATE_DATA->status);

				if (ret != AO_SUCCESS) {
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetStatus() failed, ret = %d", ret);

					FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
					FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
				}

				FOCUSER_POSITION_ITEM->number.value = PRIVATE_DATA->status.position;

				indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
				indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
			}
		}
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_LIMITS_PROPERTY, property)) {
		// FOCUSER_LIMITS
		indigo_property_copy_values(FOCUSER_LIMITS_PROPERTY, property, false);
		
		int max_position = (int)FOCUSER_LIMITS_MAX_POSITION_ITEM->number.target;

		if (focuser_config(device, MASK_MAX_STEP, max_position))
			FOCUSER_LIMITS_PROPERTY->state = INDIGO_OK_STATE;
		else
			FOCUSER_LIMITS_PROPERTY->state = INDIGO_ALERT_STATE;

		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.value = max_position;
		indigo_update_property(device, FOCUSER_LIMITS_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_BACKLASH_PROPERTY, property)) {
		// FOCUSER_BACKLASH
		indigo_property_copy_values(FOCUSER_BACKLASH_PROPERTY, property, false);

		int backlash = (int)FOCUSER_BACKLASH_ITEM->number.target;

		if (focuser_config(device, MASK_BACKLASH, backlash))
			FOCUSER_BACKLASH_PROPERTY->state = INDIGO_OK_STATE;
		else
			FOCUSER_BACKLASH_PROPERTY->state = INDIGO_ALERT_STATE;

		FOCUSER_BACKLASH_ITEM->number.value = backlash;
		indigo_update_property(device, FOCUSER_BACKLASH_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_STEPS_PROPERTY, property)) {
		// FOCUSER_STEPS
		indigo_property_copy_values(FOCUSER_STEPS_PROPERTY, property, false);
		if (FOCUSER_STEPS_PROPERTY->state == INDIGO_BUSY_STATE) {
			return INDIGO_OK;
		}
		if (FOCUSER_STEPS_ITEM->number.value < 0 || FOCUSER_STEPS_ITEM->number.value > FOCUSER_STEPS_ITEM->number.max) {
			FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
			FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
			indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		} else {
			FOCUSER_STEPS_PROPERTY->state = INDIGO_BUSY_STATE;
			FOCUSER_POSITION_PROPERTY->state = INDIGO_BUSY_STATE;
			indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
			indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);

			int step = (FOCUSER_DIRECTION_MOVE_INWARD_ITEM->sw.value) ? (-FOCUSER_STEPS_ITEM->number.value) : FOCUSER_STEPS_ITEM->number.value;
			AOReturn ret = AOFocuserMove(PRIVATE_DATA->dev_id, step);

			if (ret != AO_SUCCESS)
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to move Oasis Focuser, ret = %d\n", ret);

			indigo_set_timer(device, 0.5, focuser_timer_callback, &PRIVATE_DATA->focuser_timer);
		}
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_ABORT_MOTION_PROPERTY, property)) {
		// FOCUSER_ABORT_MOTION
		AOReturn ret;

		indigo_property_copy_values(FOCUSER_ABORT_MOTION_PROPERTY, property, false);

		FOCUSER_STEPS_PROPERTY->state = INDIGO_OK_STATE;
		FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
		FOCUSER_ABORT_MOTION_PROPERTY->state = INDIGO_OK_STATE;

		indigo_cancel_timer(device, &PRIVATE_DATA->focuser_timer);

		ret = AOFocuserStopMove(PRIVATE_DATA->dev_id);

		if (ret != AO_SUCCESS) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to stop Oasis Focuser, ret = %d\n", ret);
			FOCUSER_ABORT_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;
		}

		ret = AOFocuserGetStatus(PRIVATE_DATA->dev_id, &PRIVATE_DATA->status);

		if (ret != AO_SUCCESS) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to get Oasis Focuser status, ret = %d\n", ret);
			FOCUSER_ABORT_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;
		}

		FOCUSER_POSITION_ITEM->number.value = PRIVATE_DATA->status.position;
		FOCUSER_ABORT_MOTION_ITEM->sw.value = false;

		indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
		indigo_update_property(device, FOCUSER_ABORT_MOTION_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_COMPENSATION_PROPERTY, property)) {
		// FOCUSER_COMPENSATION_PROPERTY
		indigo_property_copy_values(FOCUSER_COMPENSATION_PROPERTY, property, false);
		FOCUSER_COMPENSATION_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, FOCUSER_COMPENSATION_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(BEEP_ON_POWER_UP_PROPERTY, property)) {
		// BEEP_ON_POWER_UP_PROPERTY
		indigo_property_copy_values(BEEP_ON_POWER_UP_PROPERTY, property, false);

		if (focuser_config(device, MASK_BEEP_ON_STARTUP, BEEP_ON_POWER_UP_ON_ITEM->sw.value))
			BEEP_ON_POWER_UP_PROPERTY->state = INDIGO_OK_STATE;
		else
			BEEP_ON_POWER_UP_PROPERTY->state = INDIGO_ALERT_STATE;

		indigo_update_property(device, BEEP_ON_POWER_UP_PROPERTY, NULL);

		return INDIGO_OK;
	} else if (indigo_property_match_changeable(BEEP_ON_MOVE_PROPERTY, property)) {
		// BEEP_ON_MOVE_PROPERTY
		indigo_property_copy_values(BEEP_ON_MOVE_PROPERTY, property, false);

		if (focuser_config(device, MASK_BEEP_ON_MOVE, BEEP_ON_MOVE_ON_ITEM->sw.value))
			BEEP_ON_MOVE_PROPERTY->state = INDIGO_OK_STATE;
		else
			BEEP_ON_MOVE_PROPERTY->state = INDIGO_ALERT_STATE;

		indigo_update_property(device, BEEP_ON_MOVE_PROPERTY, NULL);

		return INDIGO_OK;
	} else if (indigo_property_match_changeable(BACKLASH_DIRECTION_PROPERTY, property)) {
		// BACKLASH_DIRECTION_PROPERTY
		indigo_property_copy_values(BACKLASH_DIRECTION_PROPERTY, property, false);

		if (focuser_config(device, MASK_BACKLASH_DIRECTION, BACKLASH_DIRECTION_OUT_ITEM->sw.value))
			BACKLASH_DIRECTION_PROPERTY->state = INDIGO_OK_STATE;
		else
			BACKLASH_DIRECTION_PROPERTY->state = INDIGO_ALERT_STATE;

		indigo_update_property(device, BACKLASH_DIRECTION_PROPERTY, NULL);

		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FRIENDLY_NAME_PROPERTY, property)) {
		// FRIENDLY_NAME_PROPERTY
		indigo_property_copy_values(FRIENDLY_NAME_PROPERTY, property, false);

		if (strlen(FRIENDLY_NAME_ITEM->text.value) > AO_FOCUSER_VERSION_LEN) {
			FRIENDLY_NAME_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, FRIENDLY_NAME_PROPERTY, "Friendly name is too long");
			return INDIGO_OK;
		}

		strcpy(PRIVATE_DATA->friendly_name, FRIENDLY_NAME_ITEM->text.value);

		AOReturn ret = AOFocuserSetFriendlyName(PRIVATE_DATA->dev_id, PRIVATE_DATA->friendly_name);

		if (ret == AO_SUCCESS) {
			FRIENDLY_NAME_PROPERTY->state = INDIGO_OK_STATE;
		} else {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to set Oasis Focuser friendly name, ret = %d\n", ret);
			FRIENDLY_NAME_PROPERTY->state = INDIGO_ALERT_STATE;
		}

		indigo_update_property(device, FRIENDLY_NAME_PROPERTY, NULL);

		return INDIGO_OK;
	} else if (indigo_property_match_changeable(BLUETOOTH_PROPERTY, property)) {
		// BLUETOOTH_PROPERTY
		indigo_property_copy_values(BLUETOOTH_PROPERTY, property, false);

		if (focuser_config(device, MASK_BLUETOOTH, BLUETOOTH_ON_ITEM->sw.value))
			BLUETOOTH_PROPERTY->state = INDIGO_OK_STATE;
		else
			BLUETOOTH_PROPERTY->state = INDIGO_ALERT_STATE;

		indigo_update_property(device, BLUETOOTH_PROPERTY, NULL);

		return INDIGO_OK;
	} else if (indigo_property_match_changeable(BLUETOOTH_NAME_PROPERTY, property)) {
		// BLUETOOTH_NAME_PROPERTY
		indigo_property_copy_values(BLUETOOTH_NAME_PROPERTY, property, false);

		if (strlen(BLUETOOTH_NAME_ITEM->text.value) > AO_FOCUSER_VERSION_LEN) {
			BLUETOOTH_NAME_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, BLUETOOTH_NAME_PROPERTY, "Bluetooth name is too long");
			return INDIGO_OK;
		}

		strcpy(PRIVATE_DATA->bluetooth_name, BLUETOOTH_NAME_ITEM->text.value);

		AOReturn ret = AOFocuserSetBluetoothName(PRIVATE_DATA->dev_id, PRIVATE_DATA->bluetooth_name);

		if (ret == AO_SUCCESS) {
			BLUETOOTH_NAME_PROPERTY->state = INDIGO_OK_STATE;
		} else {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to set Oasis Focuser bluetooth name, ret = %d\n", ret);
			BLUETOOTH_NAME_PROPERTY->state = INDIGO_ALERT_STATE;
		}

		indigo_update_property(device, BLUETOOTH_NAME_PROPERTY, NULL);
		
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(FOCUSER_MODE_PROPERTY, property)) {
		// FOCUSER_MODE
		indigo_property_copy_values(FOCUSER_MODE_PROPERTY, property, false);
		if (FOCUSER_MODE_MANUAL_ITEM->sw.value) {
			indigo_define_property(device, FOCUSER_ON_POSITION_SET_PROPERTY, NULL);
			indigo_define_property(device, FOCUSER_SPEED_PROPERTY, NULL);
			indigo_define_property(device, FOCUSER_REVERSE_MOTION_PROPERTY, NULL);
			indigo_define_property(device, FOCUSER_DIRECTION_PROPERTY, NULL);
			indigo_define_property(device, FOCUSER_STEPS_PROPERTY, NULL);
			indigo_define_property(device, FOCUSER_ABORT_MOTION_PROPERTY, NULL);
			indigo_define_property(device, FOCUSER_BACKLASH_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_POSITION_PROPERTY, NULL);
			FOCUSER_POSITION_PROPERTY->perm = INDIGO_RW_PERM;
			indigo_define_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		} else {
			indigo_delete_property(device, FOCUSER_ON_POSITION_SET_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_SPEED_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_REVERSE_MOTION_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_DIRECTION_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_STEPS_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_ABORT_MOTION_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_BACKLASH_PROPERTY, NULL);
			indigo_delete_property(device, FOCUSER_POSITION_PROPERTY, NULL);
			FOCUSER_POSITION_PROPERTY->perm = INDIGO_RO_PERM;
			indigo_define_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		}
		FOCUSER_MODE_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, FOCUSER_MODE_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(CONFIG_PROPERTY, property)) {
		// CONFIG
		if (indigo_switch_match(CONFIG_SAVE_ITEM, property)) {
			indigo_save_property(device, NULL, BEEP_ON_MOVE_PROPERTY);
		}
	}
	return indigo_focuser_change_property(device, client, property);
}

static indigo_result focuser_detach(indigo_device *device) {
	assert(device != NULL);
	if (IS_CONNECTED) {
		indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
		focuser_connect_callback(device);
	}
	indigo_release_property(BEEP_ON_POWER_UP_PROPERTY);
	indigo_release_property(BEEP_ON_MOVE_PROPERTY);
	indigo_release_property(BACKLASH_DIRECTION_PROPERTY);
	indigo_release_property(FRIENDLY_NAME_PROPERTY);
	indigo_release_property(BLUETOOTH_PROPERTY);
	indigo_release_property(BLUETOOTH_NAME_PROPERTY);
	indigo_release_property(FOCUSER_TEMPERATURE_BOARD_PROPERTY);
	INDIGO_DEVICE_DETACH_LOG(DRIVER_NAME, device->name);
	return indigo_focuser_detach(device);
}

// hot-plug support
static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct _FOCUSER_LIST {
	indigo_device *device[AO_FOCUSER_MAX_NUM];
	int count;
} FOCUSER_LIST;

static FOCUSER_LIST gFocusers = { {}, 0 };

static int focuser_get_index(int id) {
	int i;

	for (i = 0; i < gFocusers.count; i++) {
		indigo_device *device = gFocusers.device[i];

		if (device && (PRIVATE_DATA->dev_id == id))
			return i;
	}

	return -1;
}

static indigo_device *focuser_create(int id) {
	AOFocuserVersion version;
	AOFocuserConfig config;
	char model[AO_FOCUSER_NAME_LEN + 1];
	char friendly_name[AO_FOCUSER_NAME_LEN + 1];
	char bluetooth_name[AO_FOCUSER_NAME_LEN + 1];
	indigo_device *device = NULL;

	static indigo_device focuser_template = INDIGO_DEVICE_INITIALIZER(
		"",
		focuser_attach,
		focuser_enumerate_properties,
		focuser_change_property,
		NULL,
		focuser_detach
		);

	AOReturn ret = AOFocuserOpen(id);
	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserOpen() failed, ret = %d", ret);
		return NULL;
	}

	ret = AOFocuserGetVersion(id, &version);
	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetVersion() failed, ret = %d", ret);
		goto out;
	}

	ret = AOFocuserGetProductModel(id, model);
	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetProductModel() failed, ret = %d", ret);
		goto out;
	}

	ret = AOFocuserGetFriendlyName(id, friendly_name);
	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetFriendlyName() failed, ret = %d", ret);
		goto out;
	}

	ret = AOFocuserGetBluetoothName(id, bluetooth_name);
	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetBluetoothName() failed, ret = %d", ret);
		goto out;
	}

	ret = AOFocuserGetConfig(id, &config);
	if (ret != AO_SUCCESS) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "AOFocuserGetConfig() failed, ret = %d", ret);
		goto out;
	}

	device = indigo_safe_malloc_copy(sizeof(indigo_device), &focuser_template);

	astroasis_private_data *private_data = indigo_safe_malloc(sizeof(astroasis_private_data));

	private_data->dev_id = id;

	AOFocuserGetSDKVersion(private_data->sdk_version);

	sprintf(private_data->firmware_version, "%d.%d.%d", version.firmware >> 24, (version.firmware & 0x00FF0000) >> 16, (version.firmware & 0x0000FF00) >> 8);

	strcpy(private_data->model, model);
	strcpy(private_data->friendly_name, friendly_name);
	strcpy(private_data->bluetooth_name, bluetooth_name);

	if (strlen(private_data->friendly_name) > 0)
		sprintf(device->name, "%s (%s)", "Oasis Focuser", private_data->friendly_name);
	else
		sprintf(device->name, "%s", "Oasis Focuser");

	memcpy(&private_data->config, &config, sizeof(AOFocuserConfig));

	device->private_data = private_data;

	indigo_make_name_unique(device->name, "%d", id);

	INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);

	indigo_attach_device(device);

out:
	AOFocuserClose(id);

	return device;
}

static void focuser_refresh(void) {
	FOCUSER_LIST focusers = { {}, 0 };
	int number, ids[AO_FOCUSER_MAX_NUM];
	int i;

	AOFocuserScan(&number, ids);

	pthread_mutex_lock(&global_mutex);

	for (i = 0; i < number; i++) {
		int pos = focuser_get_index(ids[i]);

		if (pos == -1) {
			// This focuser is not found in previous scan. Create focuser device instance for it
			focusers.device[focusers.count] = focuser_create(ids[i]);
			focusers.count += focusers.device[focusers.count] ? 1 : 0;
		} else {
			// This focuser is found in previous scan
			focusers.device[focusers.count] = gFocusers.device[pos];
			focusers.count++;

			gFocusers.device[pos] = 0;
		}
	}

	for (int i = 0; i < gFocusers.count; i++) {
		indigo_device *device = gFocusers.device[i];

		if (device) {
			indigo_detach_device(device);
			free(device->private_data);
			free(device);
		}
	}

	memcpy(&gFocusers, &focusers, sizeof(FOCUSER_LIST));

	pthread_mutex_unlock(&global_mutex);
}

static void process_plug_event(indigo_device *unused) {
	focuser_refresh();
}

static void process_unplug_event(indigo_device *unused) {
	focuser_refresh();
}

static int hotplug_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data) {
	struct libusb_device_descriptor descriptor;
	switch (event) {
		case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED: {
			libusb_get_device_descriptor(dev, &descriptor);
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Device plugged has PID:VID = %x:%x", descriptor.idVendor, descriptor.idProduct);
			indigo_set_timer(NULL, 0.5, process_plug_event, NULL);
			break;
		}
		case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT: {
			indigo_set_timer(NULL, 0.5, process_unplug_event, NULL);
			break;
		}
	}
	return 0;
};

static void remove_all_devices() {
	pthread_mutex_lock(&global_mutex);

	for (int i = 0; i < gFocusers.count; i++) {
		indigo_device *device = gFocusers.device[i];

		if (device) {
			indigo_detach_device(device);
			free(device->private_data);
			free(device);
		}
	}

	memset(&gFocusers, 0, sizeof(FOCUSER_LIST));

	pthread_mutex_unlock(&global_mutex);
}

static libusb_hotplug_callback_handle callback_handle;

indigo_result indigo_focuser_astroasis(indigo_driver_action action, indigo_driver_info *info) {
	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, "Astroasis Oasis Focuser", __FUNCTION__, DRIVER_VERSION, false, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch (action) {
	case INDIGO_DRIVER_INIT:
		last_action = action;

		char sdk_version[AO_FOCUSER_VERSION_LEN];

		AOFocuserGetSDKVersion(sdk_version);
		INDIGO_DRIVER_LOG(DRIVER_NAME, "Oasis Focuser SDK version: %s", sdk_version);

		indigo_start_usb_event_handler();
		int rc = libusb_hotplug_register_callback(NULL, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, LIBUSB_HOTPLUG_ENUMERATE, ASTROASIS_VENDOR_ID, ASTROASIS_PRODUCT_FOCUSER_ID, LIBUSB_HOTPLUG_MATCH_ANY, hotplug_callback, NULL, &callback_handle);
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "libusb_hotplug_register_callback ->  %s", rc < 0 ? libusb_error_name(rc) : "OK");
		return rc >= 0 ? INDIGO_OK : INDIGO_FAILED;

	case INDIGO_DRIVER_SHUTDOWN:
		for (int i = 0; i < gFocusers.count; i++)
			VERIFY_NOT_CONNECTED(gFocusers.device[i]);
		last_action = action;
		libusb_hotplug_deregister_callback(NULL, callback_handle);
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "libusb_hotplug_deregister_callback");
		remove_all_devices();
		break;

	case INDIGO_DRIVER_INFO:
		break;
	}

	return INDIGO_OK;
}

#else
indigo_result indigo_focuser_astroasis(indigo_driver_action action, indigo_driver_info *info) {
	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, "Astroasis Oasis Focuser", __FUNCTION__, DRIVER_VERSION, false, last_action);

	switch(action) {
		case INDIGO_DRIVER_INIT:
		case INDIGO_DRIVER_SHUTDOWN:
			return INDIGO_UNSUPPORTED_ARCH;
		case INDIGO_DRIVER_INFO:
			break;
	}
	return INDIGO_OK;
}
#endif
