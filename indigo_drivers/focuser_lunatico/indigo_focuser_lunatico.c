// Copyright (C) 2020 Rumen G. Bogdanovski
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
// 2.0 by Rumen G. Bogdanovski

/** INDIGO Lunatico Armadillo/Platipus focuser driver
 \file indigo_focuser_lunatico.c
 */

#define DRIVER_VERSION 0x0001
//#define DRIVER_NAME    "indigo_focuser_lunatico"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

#if defined(INDIGO_FREEBSD)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#include <indigo/indigo_driver_xml.h>

#include <indigo/indigo_io.h>

#include "indigo_focuser_lunatico.h"

#define DEFAULT_BAUDRATE            "115200"

#define MAX_PORTS  3
#define MAX_DEVICES 4

#define DEVICE_CONNECTED_MASK            0x80
#define PORT_INDEX_MASK                  0x0F

#define DEVICE_CONNECTED                 (device->gp_bits & DEVICE_CONNECTED_MASK)

#define set_connected_flag(dev)          ((dev)->gp_bits |= DEVICE_CONNECTED_MASK)
#define clear_connected_flag(dev)        ((dev)->gp_bits &= ~DEVICE_CONNECTED_MASK)

#define get_port_index(dev)              ((dev)->gp_bits & PORT_INDEX_MASK)
#define set_port_index(dev, index)       ((dev)->gp_bits = ((dev)->gp_bits & ~PORT_INDEX_MASK) | (PORT_INDEX_MASK & index))

#define PRIVATE_DATA                    ((lunatico_private_data *)device->private_data)
#define PORT_DATA                       (PRIVATE_DATA->port_data[get_port_index(device)])

#define LA_MODEL_HINT_PROPERTY          (PORT_DATA.model_hint_property)
#define LA_MODEL_AUTO_ITEM              (LA_MODEL_HINT_PROPERTY->items+0)
#define LA_MODEL_ARMADILLO_ITEM         (LA_MODEL_HINT_PROPERTY->items+1)
#define LA_MODEL_PLATIPUS_ITEM          (LA_MODEL_HINT_PROPERTY->items+2)

#define LA_MODEL_HINT_PROPERTY_NAME     "LUNATICO_MODEL_HINT"
#define LA_MODEL_AUTO_ITEM_NAME         "AUTO_DETECT"
#define LA_MODEL_ARMADILLO_ITEM_NAME    "ARMADILLO"
#define LA_MODEL_PLATIPUS_ITEM_NAME     "PLATIPUS"


#define LA_STEP_MODE_PROPERTY          (PORT_DATA.step_mode_property)
#define LA_STEP_MODE_FULL_ITEM         (LA_STEP_MODE_PROPERTY->items+0)
#define LA_STEP_MODE_HALF_ITEM         (LA_STEP_MODE_PROPERTY->items+1)

#define LA_STEP_MODE_PROPERTY_NAME     "LA_STEP_MODE"
#define LA_STEP_MODE_FULL_ITEM_NAME    "FULL"
#define LA_STEP_MODE_HALF_ITEM_NAME    "HALF"

#define LA_POWER_CONTROL_PROPERTY         (PORT_DATA.current_control_property)
#define LA_POWER_CONTROL_MOVE_ITEM        (LA_POWER_CONTROL_PROPERTY->items+0)
#define LA_POWER_CONTROL_STOP_ITEM        (LA_POWER_CONTROL_PROPERTY->items+1)

#define LA_POWER_CONTROL_PROPERTY_NAME    "LA_POWER_CONTROL"
#define LA_POWER_CONTROL_MOVE_ITEM_NAME   "MOVE_POWER"
#define LA_POWER_CONTROL_STOP_ITEM_NAME   "STOP_POWER"

#define LA_TEMPERATURE_SENSOR_PROPERTY      (PORT_DATA.temperature_sensor_property)
#define LA_TEMPERATURE_SENSOR_INTERNAL_ITEM (LA_TEMPERATURE_SENSOR_PROPERTY->items+0)
#define LA_TEMPERATURE_SENSOR_EXTERNAL_ITEM (LA_TEMPERATURE_SENSOR_PROPERTY->items+1)

#define LA_TEMPERATURE_SENSOR_PROPERTY_NAME        "LA_TEMPERATURE_SENSOR"
#define LA_TEMPERATURE_SENSOR_INTERNAL_ITEM_NAME   "INTERNAL"
#define LA_TEMPERATURE_SENSOR_EXTERNAL_ITEM_NAME   "EXTERNAL"

#define LA_WIRING_PROPERTY          (PORT_DATA.wiring_property)
#define LA_WIRING_LUNATICO_ITEM     (LA_WIRING_PROPERTY->items+0)
#define LA_WIRING_MOONLITE_ITEM     (LA_WIRING_PROPERTY->items+1)

#define LA_WIRING_PROPERTY_NAME        "LA_MOTOR_WIRING"
#define LA_WIRING_LUNATICO_ITEM_NAME   "LUNATICO"
#define LA_WIRING_MOONLITE_ITEM_NAME   "MOONLITE"

#define LA_MOTOR_TYPE_PROPERTY         (PORT_DATA.motor_type_property)
#define LA_MOTOR_TYPE_UNIPOLAR_ITEM    (LA_MOTOR_TYPE_PROPERTY->items+0)
#define LA_MOTOR_TYPE_BIPOLAR_ITEM     (LA_MOTOR_TYPE_PROPERTY->items+1)
#define LA_MOTOR_TYPE_DC_ITEM          (LA_MOTOR_TYPE_PROPERTY->items+2)
#define LA_MOTOR_TYPE_STEP_DIR_ITEM    (LA_MOTOR_TYPE_PROPERTY->items+3)

#define LA_MOTOR_TYPE_PROPERTY_NAME        "LA_MOTOR_TYPE"
#define LA_MOTOR_TYPE_UNIPOLAR_ITEM_NAME   "UNIPOLAR"
#define LA_MOTOR_TYPE_BIPOLAR_ITEM_NAME    "BIPOLAR"
#define LA_MOTOR_TYPE_DC_ITEM_NAME         "DC"
#define LA_MOTOR_TYPE_STEP_DIR_ITEM_NAME   "STEP_DIR"

typedef struct {
	int current_position, target_position, max_position, backlash;
	indigo_timer *focuser_timer;
	indigo_property *step_mode_property,
	                *current_control_property,
	                *model_hint_property,
	                *temperature_sensor_property,
	                *wiring_property,
	                *motor_type_property;
} lunatico_port_data;

typedef struct {
	int handle;
	int count_open;
	int temperature_sensor_index;
	int focuser_version;
	double prev_temp;
	indigo_timer *temperature_timer;
	pthread_mutex_t port_mutex;
	lunatico_port_data port_data[MAX_PORTS];
} lunatico_private_data;

typedef struct {
	indigo_device *port[MAX_PORTS];
	lunatico_private_data *private_data;
} lunatico_device_data;

static void compensate_focus(indigo_device *device, double new_temp);

static lunatico_device_data device_data[MAX_DEVICES] = {0};

static void create_port_device(int device_index, int port_index, char *name_ext);
static void delete_port_device(int device_index, int port_index);

/* Deepsky Dad Commands ======================================================================== */

#define LUNATICO_CMD_LEN 100

typedef enum {
	STEP_MODE_FULL = 0,
	STEP_MODE_HALF = 1,
} stepmode_t;

typedef enum {
	MW_LUNATICO_NORMAL = 0,
	MW_LUNATICO_REVERSED = 1,
	MW_MOONLITE_NORMAL = 2,
	MW_MOONLITE_REVERSED = 3
} wiring_t;

typedef enum {
	MT_UNIPOLAR = 0,
	MT_BIPOLAR = 1,
	MT_DC = 2,
	MT_STEP_DIR = 3
} motor_types_t;

#define NO_TEMP_READING                (-127)

static bool lunatico_command(indigo_device *device, const char *command, char *response, int max, int sleep) {
	char c;
	struct timeval tv;
	pthread_mutex_lock(&PRIVATE_DATA->port_mutex);
	// flush
	while (true) {
		fd_set readout;
		FD_ZERO(&readout);
		FD_SET(PRIVATE_DATA->handle, &readout);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		long result = select(PRIVATE_DATA->handle+1, &readout, NULL, NULL, &tv);
		if (result == 0)
			break;
		if (result < 0) {
			pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
			return false;
		}
		result = read(PRIVATE_DATA->handle, &c, 1);
		if (result < 1) {
			pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
			return false;
		}
	}
	// write command
	indigo_write(PRIVATE_DATA->handle, command, strlen(command));
	if (sleep > 0)
		usleep(sleep);

	// read responce
	if (response != NULL) {
		int index = 0;
		int timeout = 3;
		while (index < max) {
			fd_set readout;
			FD_ZERO(&readout);
			FD_SET(PRIVATE_DATA->handle, &readout);
			tv.tv_sec = timeout;
			tv.tv_usec = 100000;
			timeout = 0;
			long result = select(PRIVATE_DATA->handle+1, &readout, NULL, NULL, &tv);
			if (result <= 0)
				break;
			result = read(PRIVATE_DATA->handle, &c, 1);
			if (result < 1) {
				pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to read from %s -> %s (%d)", DEVICE_PORT_ITEM->text.value, strerror(errno), errno);
				return false;
			}
			response[index++] = c;

			if (c == ')')
				break;
		}
		response[index] = 0;
	}
	pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Command %s -> %s", command, response != NULL ? response : "NULL");
	return true;
}


static bool lunatico_get_info(indigo_device *device, char *board, char *firmware) {
	if(!board || !firmware) return false;

	const char *operative[3] = { "", "Bootloader", "Error" };
	const char *models[5] = { "Error", "Seletek", "Armadillo", "Platypus", "Dragonfly" };
	int fwmaj, fwmin, model, oper, data;
	char response[LUNATICO_CMD_LEN]={0};
	if (lunatico_command(device, "!seletek version#", response, sizeof(response), 100)) {
		// !seletek version:2510#
		int parsed = sscanf(response, "!seletek version:%d#", &data);
		if (parsed != 1) return false;
		oper = data / 10000;		// 0 normal, 1 bootloader
		model = ( data / 1000 ) % 10;	// 1 seletek, etc.
		fwmaj = ( data / 100 ) % 10;
		fwmin = ( data % 100 );
		if ( oper >= 2 ) oper = 2;
		if ( model >= 4 ) model = 0;
		sprintf(board, "%s", models[model]);
		sprintf(firmware, "%d.%d", fwmaj, fwmin);

		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "!seletek version# -> %s = %s %s", response, board, firmware);
		return true;
	}
	INDIGO_DRIVER_ERROR(DRIVER_NAME, "NO response");
	return false;
}


static bool lunatico_command_get_result(indigo_device *device, const char *command, uint32_t *result) {
	if (!result) return false;

	char response[LUNATICO_CMD_LEN]={0};
	char response_prefix[LUNATICO_CMD_LEN];
	char format[LUNATICO_CMD_LEN];

	if (lunatico_command(device, command, response, sizeof(response), 100)) {
		strncpy(response_prefix, command, LUNATICO_CMD_LEN);
		char *p = strrchr(response_prefix, '#');
		if (p) *p = ':';
		sprintf(format, "%s%%d#", response_prefix);
		int parsed = sscanf(response, format, result);
		if (parsed != 1) return false;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "%s -> %s = %d", command, response, *result);
		return true;
	}
	INDIGO_DRIVER_ERROR(DRIVER_NAME, "NO response");
	return false;
}


static bool lunatico_command_set_value(indigo_device *device, const char *command_format, uint32_t value) {
	char command[LUNATICO_CMD_LEN];
	char response[LUNATICO_CMD_LEN];
	char response_prefix[LUNATICO_CMD_LEN];
	char expected_format[LUNATICO_CMD_LEN];
	int res = -1;

	snprintf(command, LUNATICO_CMD_LEN, command_format, value);
	if (lunatico_command(device, command, response, sizeof(response), 100)) {
		strncpy(response_prefix, command, LUNATICO_CMD_LEN);
		char *p = strrchr(response_prefix, '#');
		if (p) *p = ':';
		sprintf(expected_format, "%s%%d#", response_prefix);
		int parsed = sscanf(response, expected_format, &res);
		if (parsed != 1) return false;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "%s -> %s = %d", command, response, res);
	}

	if (res == 0) {
		return true;
	}
	return false;
}


static bool lunatico_stop(indigo_device *device) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step stop %d#", get_port_index(device));
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_sync_position(indigo_device *device, uint32_t position) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step setpos %d %d#", get_port_index(device), position);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_get_position(indigo_device *device, uint32_t *pos) {
	char command[LUNATICO_CMD_LEN]={0};

	sprintf(command, "!step getpos %d#", get_port_index(device));
	return lunatico_command_get_result(device, command, pos);
}


static bool lunatico_goto_position(indigo_device *device, uint32_t position) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step goto %d %d %d#", get_port_index(device), position, 0);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_goto_position_relative(indigo_device *device, uint32_t position) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step gopr %d %d#", get_port_index(device), position);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_is_moving(indigo_device *device, bool *is_moving) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step ismoving %d#", get_port_index(device));
	if (!lunatico_command_get_result(device, command, &res)) return false;

	if (res == 0) *is_moving = false;
	else *is_moving = true;

	return true;
}

static bool lunatico_get_temperature(indigo_device *device, int sensor_index, double *temperature) {
	if (!temperature) return false;

	char command[LUNATICO_CMD_LEN];
	int value;
	double idC1 = 261;
	double idC2 = 250;
	double idF = 1.8;

	snprintf(command, LUNATICO_CMD_LEN, "!read temps %d#", sensor_index);
	if (!lunatico_command_get_result(device, command, &value)) return false;

	if (sensor_index != 0) { // not insternal
		idC1 = 192;
		idC2 = 0;
		idF = 1.7;
	}

	*temperature = (((value - idC1) * idF) - idC2) / 10;
	return true;
}


static bool lunatico_set_step(indigo_device *device, stepmode_t mode) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step halfstep %d %d#", get_port_index(device), mode ? 1 : 0);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}

static bool lunatico_set_wiring(indigo_device *device, wiring_t wiring) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step wiremode %d %d#", get_port_index(device), wiring);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_set_motor_type(indigo_device *device, motor_types_t type) {
	char command[LUNATICO_CMD_LEN];
	int res;

	snprintf(command, LUNATICO_CMD_LEN, "!step model %d %d#", get_port_index(device), type);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_set_move_power(indigo_device *device, double power_percent) {
	char command[LUNATICO_CMD_LEN];
	int res;

	int power = (int)(power_percent * 10.23);

	snprintf(command, LUNATICO_CMD_LEN, "!step movepow %d %d#", get_port_index(device), power);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_set_stop_power(indigo_device *device, double power_percent) {
	char command[LUNATICO_CMD_LEN];
	int res;

	int power = (int)(power_percent * 10.23);

	snprintf(command, LUNATICO_CMD_LEN, "!step stoppow %d %d#", get_port_index(device), power);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


static bool lunatico_set_speed(indigo_device *device, uint32_t speed) {
	char command[LUNATICO_CMD_LEN];
	int res;

	int speed_us = (int)(500000 - ((speed - 1) * 50));
    if ((speed_us < 50) || (speed_us > 500000 )) {
        INDIGO_DRIVER_ERROR(DRIVER_NAME, "Speed out of range %d", speed);
        return false;
    }

	snprintf(command, LUNATICO_CMD_LEN, "!step speedrangeus %d %d %d#", get_port_index(device), speed_us, speed_us);
	if (!lunatico_command_get_result(device, command, &res)) return false;
	if (res != 0) return false;
	return true;
}


// -------------------------------------------------------------------------------- INDIGO focuser device implementation
static void focuser_timer_callback(indigo_device *device) {
	bool moving;
	uint32_t position;

	if (!lunatico_is_moving(device, &moving)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_is_moving(%d) failed", PRIVATE_DATA->handle);
		FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
		FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
	}

	if (!lunatico_get_position(device, &position)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_get_position(%d) failed", PRIVATE_DATA->handle);
		FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
		FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
	} else {
		PORT_DATA.current_position = (double)position;
	}

	FOCUSER_POSITION_ITEM->number.value = PORT_DATA.current_position;
	if ((!moving) || (PORT_DATA.current_position == PORT_DATA.target_position)) {
		FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
		FOCUSER_STEPS_PROPERTY->state = INDIGO_OK_STATE;
	} else {
		indigo_reschedule_timer(device, 0.5, &(PORT_DATA.focuser_timer));
	}
	indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
	indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
}


static void temperature_timer_callback(indigo_device *device) {
	double temp;
	static bool has_sensor = true;
	bool moving = false;

	FOCUSER_TEMPERATURE_PROPERTY->state = INDIGO_OK_STATE;
	if (!lunatico_get_temperature(device, PRIVATE_DATA->temperature_sensor_index, &temp)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_get_temperature(%d) -> %f failed", PRIVATE_DATA->handle, temp);
		FOCUSER_TEMPERATURE_PROPERTY->state = INDIGO_ALERT_STATE;
	} else {
		FOCUSER_TEMPERATURE_ITEM->number.value = temp;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "lunatico_get_temperature(%d) -> %f succeeded", PRIVATE_DATA->handle, FOCUSER_TEMPERATURE_ITEM->number.value);
	}

	if (FOCUSER_TEMPERATURE_ITEM->number.value <= NO_TEMP_READING) { /* -127 is returned when the sensor is not connected */
		FOCUSER_TEMPERATURE_PROPERTY->state = INDIGO_IDLE_STATE;
		if (has_sensor) {
			INDIGO_DRIVER_LOG(DRIVER_NAME, "The temperature sensor is not connected.");
			indigo_update_property(device, FOCUSER_TEMPERATURE_PROPERTY, "The temperature sensor is not connected.");
			has_sensor = false;
		}
	} else {
		has_sensor = true;
		indigo_update_property(device, FOCUSER_TEMPERATURE_PROPERTY, NULL);
	}
	if (FOCUSER_MODE_AUTOMATIC_ITEM->sw.value) {
		compensate_focus(device, temp);
	} else {
		/* reset temp so that the compensation starts when auto mode is selected */
		PRIVATE_DATA->prev_temp = NO_TEMP_READING;
	}

	indigo_reschedule_timer(device, 2, &(PRIVATE_DATA->temperature_timer));
}


static void compensate_focus(indigo_device *device, double new_temp) {
	int compensation;
	double temp_difference = new_temp - PRIVATE_DATA->prev_temp;

	/* we do not have previous temperature reading */
	if (PRIVATE_DATA->prev_temp <= NO_TEMP_READING) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Not compensating: PRIVATE_DATA->prev_temp = %f", PRIVATE_DATA->prev_temp);
		PRIVATE_DATA->prev_temp = new_temp;
		return;
	}

	/* we do not have current temperature reading or focuser is moving */
	if ((new_temp <= NO_TEMP_READING) || (FOCUSER_POSITION_PROPERTY->state != INDIGO_OK_STATE)) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Not compensating: new_temp = %f, FOCUSER_POSITION_PROPERTY->state = %d", new_temp, FOCUSER_POSITION_PROPERTY->state);
		return;
	}

	/* temperature difference if more than 1 degree so compensation needed */
	if ((fabs(temp_difference) >= 1.0) && (fabs(temp_difference) < 100)) {
		compensation = (int)(temp_difference * FOCUSER_COMPENSATION_ITEM->number.value);
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Compensation: temp_difference = %.2f, Compensation = %d, steps/degC = %.1f", temp_difference, compensation, FOCUSER_COMPENSATION_ITEM->number.value);
	} else {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Not compensating (not needed): temp_difference = %f", temp_difference);
		return;
	}

	PORT_DATA.target_position = PORT_DATA.current_position + compensation;
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Compensation: PORT_DATA.current_position = %d, PORT_DATA.target_position = %d", PORT_DATA.current_position, PORT_DATA.target_position);

	uint32_t current_position;
	if (!lunatico_get_position(device, &current_position)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_get_position(%d) failed", PRIVATE_DATA->handle);
	}
	PORT_DATA.current_position = (double)current_position;

	/* Make sure we do not attempt to go beyond the limits */
	if (FOCUSER_POSITION_ITEM->number.max < PORT_DATA.target_position) {
		PORT_DATA.target_position = FOCUSER_POSITION_ITEM->number.max;
	} else if (FOCUSER_POSITION_ITEM->number.min > PORT_DATA.target_position) {
		PORT_DATA.target_position = FOCUSER_POSITION_ITEM->number.min;
	}
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Compensating: Corrected PORT_DATA.target_position = %d", PORT_DATA.target_position);

	if (!lunatico_goto_position(device, (uint32_t)PORT_DATA.target_position)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_goto_position(%d, %d) failed", PRIVATE_DATA->handle, PORT_DATA.target_position);
		FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
	}

	PRIVATE_DATA->prev_temp = new_temp;
	FOCUSER_POSITION_ITEM->number.value = PORT_DATA.current_position;
	FOCUSER_POSITION_PROPERTY->state = INDIGO_BUSY_STATE;
	indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
	PORT_DATA.focuser_timer = indigo_set_timer(device, 0.5, focuser_timer_callback);
}


static indigo_result lunatico_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property) {
	if (IS_CONNECTED) {
		if (indigo_property_match(LA_STEP_MODE_PROPERTY, property))
			indigo_define_property(device, LA_STEP_MODE_PROPERTY, NULL);
		if (indigo_property_match(LA_POWER_CONTROL_PROPERTY, property))
			indigo_define_property(device, LA_POWER_CONTROL_PROPERTY, NULL);
		if (indigo_property_match(LA_TEMPERATURE_SENSOR_PROPERTY, property))
			indigo_define_property(device, LA_TEMPERATURE_SENSOR_PROPERTY, NULL);
		if (indigo_property_match(LA_WIRING_PROPERTY, property))
			indigo_define_property(device, LA_WIRING_PROPERTY, NULL);
		if (indigo_property_match(LA_MOTOR_TYPE_PROPERTY, property))
			indigo_define_property(device, LA_MOTOR_TYPE_PROPERTY, NULL);
	}
	indigo_define_property(device, LA_MODEL_HINT_PROPERTY, NULL);
	return indigo_focuser_enumerate_properties(device, NULL, NULL);
}


static indigo_result focuser_attach(indigo_device *device) {
	assert(device != NULL);
	assert(PRIVATE_DATA != NULL);
	if (indigo_focuser_attach(device, DRIVER_VERSION) == INDIGO_OK) {
		pthread_mutex_init(&PRIVATE_DATA->port_mutex, NULL);
		// -------------------------------------------------------------------------------- SIMULATION
		SIMULATION_PROPERTY->hidden = true;
		// -------------------------------------------------------------------------------- DEVICE_PORT
		DEVICE_PORT_PROPERTY->hidden = false;
		// -------------------------------------------------------------------------------- DEVICE_PORTS
		DEVICE_PORTS_PROPERTY->hidden = false;
		// -------------------------------------------------------------------------------- DEVICE_BAUDRATE
		DEVICE_BAUDRATE_PROPERTY->hidden = false;
		strncpy(DEVICE_BAUDRATE_ITEM->text.value, DEFAULT_BAUDRATE, INDIGO_VALUE_SIZE);
		// --------------------------------------------------------------------------------
		INFO_PROPERTY->count = 5;

		if (get_port_index(device) == 0) FOCUSER_TEMPERATURE_PROPERTY->hidden = false;

		FOCUSER_LIMITS_PROPERTY->hidden = false;
		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.min = 10000;
		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.max = 1000000;
		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.step = FOCUSER_LIMITS_MAX_POSITION_ITEM->number.min;

		FOCUSER_LIMITS_MIN_POSITION_ITEM->number.min = 0;
		FOCUSER_LIMITS_MIN_POSITION_ITEM->number.value = 0;
		FOCUSER_LIMITS_MIN_POSITION_ITEM->number.max = 0;

		FOCUSER_SPEED_PROPERTY->hidden = false;
		FOCUSER_SPEED_ITEM->number.min = 1;
		FOCUSER_SPEED_ITEM->number.max = 10000;
		FOCUSER_SPEED_ITEM->number.step = 1;
		FOCUSER_SPEED_ITEM->number.value = FOCUSER_SPEED_ITEM->number.target = 9800;

		FOCUSER_POSITION_ITEM->number.min = 0;
		FOCUSER_POSITION_ITEM->number.step = 100;
		FOCUSER_POSITION_ITEM->number.max = FOCUSER_LIMITS_MAX_POSITION_ITEM->number.max;

		FOCUSER_STEPS_ITEM->number.min = 0;
		FOCUSER_STEPS_ITEM->number.step = 1;

		FOCUSER_COMPENSATION_PROPERTY->hidden = false;
		FOCUSER_COMPENSATION_ITEM->number.min = -10000;
		FOCUSER_COMPENSATION_ITEM->number.max = 10000;
		//FOCUSER_STEPS_ITEM->number.max = PRIVATE_DATA->info.MaxStep;

		FOCUSER_ON_POSITION_SET_PROPERTY->hidden = false;
		FOCUSER_REVERSE_MOTION_PROPERTY->hidden = false;

		// -------------------------------------------------------------------------- LA_MODEL_HINT_PROPERTY
		LA_MODEL_HINT_PROPERTY = indigo_init_switch_property(NULL, device->name, LA_MODEL_HINT_PROPERTY_NAME, MAIN_GROUP, "Focuser model hint", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 3);
		if (LA_MODEL_HINT_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(LA_MODEL_AUTO_ITEM, LA_MODEL_AUTO_ITEM_NAME, "Auto detect (on connect)", true);
		indigo_init_switch_item(LA_MODEL_ARMADILLO_ITEM, LA_MODEL_ARMADILLO_ITEM_NAME, "Armadillo (2 ports)", false);
		indigo_init_switch_item(LA_MODEL_PLATIPUS_ITEM, LA_MODEL_PLATIPUS_ITEM_NAME, "Platipus (3 ports)", false);
		if (get_port_index(device) != 0) LA_MODEL_HINT_PROPERTY->hidden = true;
		// -------------------------------------------------------------------------- STEP_MODE_PROPERTY
		LA_STEP_MODE_PROPERTY = indigo_init_switch_property(NULL, device->name, LA_STEP_MODE_PROPERTY_NAME, "Advanced", "Step mode", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (LA_STEP_MODE_PROPERTY == NULL)
			return INDIGO_FAILED;
		LA_STEP_MODE_PROPERTY->hidden = false;
		indigo_init_switch_item(LA_STEP_MODE_FULL_ITEM, LA_STEP_MODE_FULL_ITEM_NAME, "Full step", true);
		indigo_init_switch_item(LA_STEP_MODE_HALF_ITEM, LA_STEP_MODE_HALF_ITEM_NAME, "1/2 step", false);
		//--------------------------------------------------------------------------- CURRENT_CONTROL_PROPERTY
		LA_POWER_CONTROL_PROPERTY = indigo_init_number_property(NULL, device->name, LA_POWER_CONTROL_PROPERTY_NAME, "Advanced", "Coils current control", INDIGO_OK_STATE, INDIGO_RW_PERM, 2);
		if (LA_POWER_CONTROL_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(LA_POWER_CONTROL_MOVE_ITEM, LA_POWER_CONTROL_MOVE_ITEM_NAME, "Move power (%)", 0, 100, 1, 100);
		indigo_init_number_item(LA_POWER_CONTROL_STOP_ITEM, LA_POWER_CONTROL_STOP_ITEM_NAME, "Stop power (%)", 0, 100, 1, 0);
		//--------------------------------------------------------------------------- TEMPERATURE_SENSOR_PROPERTY
		LA_TEMPERATURE_SENSOR_PROPERTY = indigo_init_switch_property(NULL, device->name, LA_TEMPERATURE_SENSOR_PROPERTY_NAME, "Advanced", "Temperature Sensor in use", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (LA_TEMPERATURE_SENSOR_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(LA_TEMPERATURE_SENSOR_INTERNAL_ITEM, LA_TEMPERATURE_SENSOR_INTERNAL_ITEM_NAME, "Internal sensor", true);
		indigo_init_switch_item(LA_TEMPERATURE_SENSOR_EXTERNAL_ITEM, LA_TEMPERATURE_SENSOR_EXTERNAL_ITEM_NAME, "External Sensor", false);
		if (get_port_index(device) != 0) LA_TEMPERATURE_SENSOR_PROPERTY->hidden = true;
		//--------------------------------------------------------------------------- WIRING_PROPERTY
		LA_WIRING_PROPERTY = indigo_init_switch_property(NULL, device->name, LA_WIRING_PROPERTY_NAME, "Advanced", "Motor wiring", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (LA_WIRING_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(LA_WIRING_LUNATICO_ITEM, LA_WIRING_LUNATICO_ITEM_NAME, "Lunatico", true);
		indigo_init_switch_item(LA_WIRING_MOONLITE_ITEM, LA_WIRING_MOONLITE_ITEM_NAME, "RF/Moonlite", false);
		//--------------------------------------------------------------------------- LA_MOTOR_TYPE_PROPERTY
		LA_MOTOR_TYPE_PROPERTY = indigo_init_switch_property(NULL, device->name, LA_MOTOR_TYPE_PROPERTY_NAME, "Advanced", "Motor type", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 4);
		if (LA_MOTOR_TYPE_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(LA_MOTOR_TYPE_UNIPOLAR_ITEM, LA_MOTOR_TYPE_UNIPOLAR_ITEM_NAME, "Unipolar", true);
		indigo_init_switch_item(LA_MOTOR_TYPE_BIPOLAR_ITEM, LA_MOTOR_TYPE_BIPOLAR_ITEM_NAME, "Bipolar", false);
		indigo_init_switch_item(LA_MOTOR_TYPE_DC_ITEM, LA_MOTOR_TYPE_DC_ITEM_NAME, "DC", false);
		indigo_init_switch_item(LA_MOTOR_TYPE_STEP_DIR_ITEM, LA_MOTOR_TYPE_STEP_DIR_ITEM_NAME, "Step-dir", false);
		//---------------------------------------------------------------------------
		INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);
		indigo_define_property(device, LA_MODEL_HINT_PROPERTY, NULL);
		return indigo_focuser_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}


static bool lunatico_open(indigo_device *device) {
	if (DEVICE_CONNECTED) return false;

	pthread_mutex_lock(&PRIVATE_DATA->port_mutex);
	if (PRIVATE_DATA->count_open++ == 0) {
		if (indigo_try_global_lock(device) != INDIGO_OK) {
			pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "indigo_try_global_lock(): failed to get lock.");
			PRIVATE_DATA->count_open--;
			return false;
		}
		char *name = DEVICE_PORT_ITEM->text.value;
		if (strncmp(name, "lunatico://", 11)) {
			PRIVATE_DATA->handle = indigo_open_serial_with_speed(name, atoi(DEVICE_BAUDRATE_ITEM->text.value));
		} else {
			char *host = name + 6;
			char *colon = strchr(host, ':');
			if (colon == NULL) {
				PRIVATE_DATA->handle = indigo_open_tcp(host, 8080);
			} else {
				char host_name[INDIGO_NAME_SIZE];
				strncpy(host_name, host, colon - host);
				host_name[colon - host] = 0;
				int port = atoi(colon + 1);
				PRIVATE_DATA->handle = indigo_open_tcp(host_name, port);
			}
		}
		if (PRIVATE_DATA->handle < 0) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "indigo_open_serial(%s): failed", DEVICE_PORT_ITEM->text.value);
			CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
			indigo_update_property(device, CONNECTION_PROPERTY, NULL);
			indigo_global_unlock(device);
			pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
			PRIVATE_DATA->count_open--;
			return false;
		}
		pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
		lunatico_get_temperature(device, 0, &FOCUSER_TEMPERATURE_ITEM->number.value);
		PRIVATE_DATA->prev_temp = FOCUSER_TEMPERATURE_ITEM->number.value;
		PRIVATE_DATA->temperature_timer = indigo_set_timer(device, 1, temperature_timer_callback);
	} else {
		pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
	}
	return true;
}

static void lunatico_close(indigo_device *device) {

	INDIGO_DRIVER_LOG(DRIVER_NAME, "CLOSE REQUESTED: %d -> %d", PRIVATE_DATA->handle, DEVICE_CONNECTED);
	if (!DEVICE_CONNECTED) return;

	pthread_mutex_lock(&PRIVATE_DATA->port_mutex);
	if (--PRIVATE_DATA->count_open == 0) {
		indigo_cancel_timer(device, &PRIVATE_DATA->temperature_timer);
		INDIGO_DRIVER_LOG(DRIVER_NAME, "PRIVATE_DATA->temperature_timer == %p", PRIVATE_DATA->temperature_timer);
		close(PRIVATE_DATA->handle);
		INDIGO_DRIVER_LOG(DRIVER_NAME, "close(%d)", PRIVATE_DATA->handle);
		indigo_global_unlock(device);
		PRIVATE_DATA->handle = 0;
	}
	pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
}


static indigo_result focuser_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);
	if (indigo_property_match(CONNECTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- CONNECTION
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		int position;
		if (CONNECTION_CONNECTED_ITEM->sw.value) {
			if (!DEVICE_CONNECTED) {
				CONNECTION_PROPERTY->state = INDIGO_BUSY_STATE;
				indigo_update_property(device, CONNECTION_PROPERTY, NULL);
				if (lunatico_open(device)) {
					char board[LUNATICO_CMD_LEN] = "N/A";
					char firmware[LUNATICO_CMD_LEN] = "N/A";
					uint32_t value;
					if (lunatico_get_info(device, board, firmware)) {
						strncpy(INFO_DEVICE_MODEL_ITEM->text.value, board, INDIGO_VALUE_SIZE);
						strncpy(INFO_DEVICE_FW_REVISION_ITEM->text.value, firmware, INDIGO_VALUE_SIZE);
						indigo_update_property(device, INFO_PROPERTY, NULL);
					}
					//PRIVATE_DATA->focuser_version = 3;

					lunatico_get_position(device, &position);
					FOCUSER_POSITION_ITEM->number.value = (double)position;

					FOCUSER_LIMITS_MAX_POSITION_ITEM->number.value = (double)PORT_DATA.max_position;

					if (!lunatico_set_step(device, 0)) {
						INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_step(%d) failed", PRIVATE_DATA->handle);
					}
					indigo_define_property(device, LA_STEP_MODE_PROPERTY, NULL);

					if (!lunatico_set_move_power(device, LA_POWER_CONTROL_MOVE_ITEM->number.value)) {
						INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_move_power(%d) failed", PRIVATE_DATA->handle);
					}
					if (!lunatico_set_stop_power(device, LA_POWER_CONTROL_STOP_ITEM->number.value)) {
						INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_stop_power(%d) failed", PRIVATE_DATA->handle);
					}
					indigo_define_property(device, LA_POWER_CONTROL_PROPERTY, NULL);

					indigo_define_property(device, LA_TEMPERATURE_SENSOR_PROPERTY, NULL);
					PRIVATE_DATA->temperature_sensor_index = 0;

					if (!lunatico_set_wiring(device, MW_LUNATICO_NORMAL)) {
						INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_wiring(%d) failed", PRIVATE_DATA->handle);
					}
					indigo_define_property(device, LA_WIRING_PROPERTY, NULL);

					if (!lunatico_set_motor_type(device, MT_UNIPOLAR)) {
						INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_motor_type(%d) failed", PRIVATE_DATA->handle);
					}
					indigo_define_property(device, LA_MOTOR_TYPE_PROPERTY, NULL);

					CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
					set_connected_flag(device);

					PORT_DATA.focuser_timer = indigo_set_timer(device, 0.5, focuser_timer_callback);
				}
			}
		} else {
			if (DEVICE_CONNECTED) {
				indigo_cancel_timer(device, &PORT_DATA.focuser_timer);
				indigo_delete_property(device, LA_STEP_MODE_PROPERTY, NULL);
				indigo_delete_property(device, LA_POWER_CONTROL_PROPERTY, NULL);
				indigo_delete_property(device, LA_TEMPERATURE_SENSOR_PROPERTY, NULL);
				indigo_delete_property(device, LA_WIRING_PROPERTY, NULL);
				indigo_delete_property(device, LA_MOTOR_TYPE_PROPERTY, NULL);
				lunatico_close(device);
				clear_connected_flag(device);
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
			}
		}
	} else if (indigo_property_match(LA_MODEL_HINT_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- LA_MODEL_HINT
		indigo_property_copy_values(LA_MODEL_HINT_PROPERTY, property, false);
		LA_MODEL_HINT_PROPERTY->state = INDIGO_OK_STATE;
		if (LA_MODEL_PLATIPUS_ITEM->sw.value) {
			create_port_device(0, 2, "Third");
		} else {
			delete_port_device(0, 2);
		}
		indigo_update_property(device, LA_MODEL_HINT_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(FOCUSER_REVERSE_MOTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- FOCUSER_REVERSE_MOTION
		if (!IS_CONNECTED) return INDIGO_OK;
		bool res = true;
		indigo_property_copy_values(FOCUSER_REVERSE_MOTION_PROPERTY, property, false);
		FOCUSER_REVERSE_MOTION_PROPERTY->state = INDIGO_OK_STATE;
		if (LA_WIRING_LUNATICO_ITEM->sw.value) {
			if(FOCUSER_REVERSE_MOTION_DISABLED_ITEM->sw.value) {
				res = lunatico_set_wiring(device, MW_LUNATICO_NORMAL);
			} else {
				res = lunatico_set_wiring(device, MW_LUNATICO_REVERSED);
			}
		} else if (LA_WIRING_MOONLITE_ITEM->sw.value) {
			if (FOCUSER_REVERSE_MOTION_DISABLED_ITEM->sw.value) {
				res = lunatico_set_wiring(device, MW_MOONLITE_NORMAL);
			} else {
				res = lunatico_set_wiring(device, MW_MOONLITE_REVERSED);
			}
		} else {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Unsupported Motor wiring");
			FOCUSER_REVERSE_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		if (res == false) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_wiring() failed");
			FOCUSER_REVERSE_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		indigo_update_property(device, FOCUSER_REVERSE_MOTION_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(FOCUSER_POSITION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- FOCUSER_POSITION
		indigo_property_copy_values(FOCUSER_POSITION_PROPERTY, property, false);
		if (FOCUSER_POSITION_ITEM->number.target < 0 || FOCUSER_POSITION_ITEM->number.target > FOCUSER_POSITION_ITEM->number.max) {
			FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
		} else if (FOCUSER_POSITION_ITEM->number.target == PORT_DATA.current_position) {
			FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
		} else { /* GOTO position */
			FOCUSER_POSITION_PROPERTY->state = INDIGO_BUSY_STATE;
			PORT_DATA.target_position = FOCUSER_POSITION_ITEM->number.target;
			FOCUSER_POSITION_ITEM->number.value = PORT_DATA.current_position;
			if (FOCUSER_ON_POSITION_SET_GOTO_ITEM->sw.value) { /* GOTO POSITION */
				FOCUSER_POSITION_PROPERTY->state = INDIGO_BUSY_STATE;
				if (!lunatico_goto_position(device, (uint32_t)PORT_DATA.target_position)) {
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_goto_position(%d, %d) failed", PRIVATE_DATA->handle, PORT_DATA.target_position);
					FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
				}
				PORT_DATA.focuser_timer = indigo_set_timer(device, 0.5, focuser_timer_callback);
			} else { /* RESET CURRENT POSITION */
				FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
				if(!lunatico_sync_position(device, PORT_DATA.target_position)) {
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_sync_position(%d, %d) failed", PRIVATE_DATA->handle, PORT_DATA.target_position);
					FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
				}
				uint32_t position;
				if (!lunatico_get_position(device, &position)) {
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_get_position(%d) failed", PRIVATE_DATA->handle);
					FOCUSER_POSITION_PROPERTY->state = INDIGO_ALERT_STATE;
				} else {
					FOCUSER_POSITION_ITEM->number.value = PORT_DATA.current_position = (double)position;
				}
			}
		}
		indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(FOCUSER_LIMITS_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- FOCUSER_LIMITS
		if (!IS_CONNECTED) return INDIGO_OK;
		indigo_property_copy_values(FOCUSER_LIMITS_PROPERTY, property, false);
		FOCUSER_LIMITS_PROPERTY->state = INDIGO_OK_STATE;
		PORT_DATA.max_position = (int)FOCUSER_LIMITS_MAX_POSITION_ITEM->number.target;

		// TODO

		FOCUSER_LIMITS_MAX_POSITION_ITEM->number.value = (double)PORT_DATA.max_position;
		indigo_update_property(device, FOCUSER_LIMITS_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(FOCUSER_SPEED_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- FOCUSER_SPEED
		if (!IS_CONNECTED) return INDIGO_OK;
		indigo_property_copy_values(FOCUSER_SPEED_PROPERTY, property, false);
		FOCUSER_SPEED_PROPERTY->state = INDIGO_OK_STATE;

		if (!lunatico_set_speed(device, (uint32_t)FOCUSER_SPEED_ITEM->number.target)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_speed(%d) failed", PRIVATE_DATA->handle);
			FOCUSER_SPEED_PROPERTY->state = INDIGO_ALERT_STATE;
		}

		indigo_update_property(device, FOCUSER_SPEED_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(FOCUSER_STEPS_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- FOCUSER_STEPS
		indigo_property_copy_values(FOCUSER_STEPS_PROPERTY, property, false);
		if (FOCUSER_STEPS_ITEM->number.value < 0 || FOCUSER_STEPS_ITEM->number.value > FOCUSER_STEPS_ITEM->number.max) {
			FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
		} else {
			FOCUSER_STEPS_PROPERTY->state = INDIGO_BUSY_STATE;
			uint32_t position;
			if (!lunatico_get_position(device, &position)) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_get_position(%d) failed", PRIVATE_DATA->handle);
			} else {
				PORT_DATA.current_position = (double)position;
			}

			if (FOCUSER_DIRECTION_MOVE_INWARD_ITEM->sw.value) {
				PORT_DATA.target_position = PORT_DATA.current_position - FOCUSER_STEPS_ITEM->number.value;
			} else {
				PORT_DATA.target_position = PORT_DATA.current_position + FOCUSER_STEPS_ITEM->number.value;
			}

			// Make sure we do not attempt to go beyond the limits
			if (FOCUSER_POSITION_ITEM->number.max < PORT_DATA.target_position) {
				PORT_DATA.target_position = FOCUSER_POSITION_ITEM->number.max;
			} else if (FOCUSER_POSITION_ITEM->number.min > PORT_DATA.target_position) {
				PORT_DATA.target_position = FOCUSER_POSITION_ITEM->number.min;
			}

			FOCUSER_POSITION_ITEM->number.value = PORT_DATA.current_position;
			if (!lunatico_goto_position(device, (uint32_t)PORT_DATA.target_position)) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_goto_position(%d, %d) failed", PRIVATE_DATA->handle, PORT_DATA.target_position);
				FOCUSER_STEPS_PROPERTY->state = INDIGO_ALERT_STATE;
			}
			PORT_DATA.focuser_timer = indigo_set_timer(device, 0.5, focuser_timer_callback);
		}
		indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(FOCUSER_ABORT_MOTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- FOCUSER_ABORT_MOTION
		indigo_property_copy_values(FOCUSER_ABORT_MOTION_PROPERTY, property, false);
		FOCUSER_STEPS_PROPERTY->state = INDIGO_OK_STATE;
		FOCUSER_POSITION_PROPERTY->state = INDIGO_OK_STATE;
		FOCUSER_ABORT_MOTION_PROPERTY->state = INDIGO_OK_STATE;
		indigo_cancel_timer(device, &PORT_DATA.focuser_timer);

		if (!lunatico_stop(device)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_stop(%d) failed", PRIVATE_DATA->handle);
			FOCUSER_ABORT_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		uint32_t position;
		if (!lunatico_get_position(device, &position)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_get_position(%d) failed", PRIVATE_DATA->handle);
			FOCUSER_ABORT_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;
		} else {
			PORT_DATA.current_position = (double)position;
		}
		FOCUSER_POSITION_ITEM->number.value = PORT_DATA.current_position;
		FOCUSER_ABORT_MOTION_ITEM->sw.value = false;
		indigo_update_property(device, FOCUSER_POSITION_PROPERTY, NULL);
		indigo_update_property(device, FOCUSER_STEPS_PROPERTY, NULL);
		indigo_update_property(device, FOCUSER_ABORT_MOTION_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(FOCUSER_COMPENSATION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- FOCUSER_COMPENSATION_PROPERTY
		indigo_property_copy_values(FOCUSER_COMPENSATION_PROPERTY, property, false);
		FOCUSER_COMPENSATION_PROPERTY->state = INDIGO_OK_STATE;
		if (IS_CONNECTED) {
			indigo_update_property(device, FOCUSER_COMPENSATION_PROPERTY, NULL);
		}
		return INDIGO_OK;
	} else if (indigo_property_match(LA_STEP_MODE_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- LA_STEP_MODE_PROPERTY
		if (!IS_CONNECTED) return INDIGO_OK;
		indigo_property_copy_values(LA_STEP_MODE_PROPERTY, property, false);
		LA_STEP_MODE_PROPERTY->state = INDIGO_OK_STATE;
		stepmode_t mode;
		if(LA_STEP_MODE_FULL_ITEM->sw.value) {
			mode = STEP_MODE_FULL;
		} else if(LA_STEP_MODE_HALF_ITEM->sw.value) {
			mode = STEP_MODE_HALF;
		}
		if (!lunatico_set_step(device, mode)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_step(%d, %d) failed", PRIVATE_DATA->handle, mode);
			LA_STEP_MODE_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		indigo_update_property(device, LA_STEP_MODE_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(LA_POWER_CONTROL_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- LA_POWER_CONTROL_PROPERTY
		if (!IS_CONNECTED) return INDIGO_OK;
		indigo_property_copy_values(LA_POWER_CONTROL_PROPERTY, property, false);
		LA_POWER_CONTROL_PROPERTY->state = INDIGO_OK_STATE;

		if (!lunatico_set_move_power(device, LA_POWER_CONTROL_MOVE_ITEM->number.value)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_move_power(%d) failed", PRIVATE_DATA->handle);
			LA_POWER_CONTROL_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, LA_POWER_CONTROL_PROPERTY, NULL);
			return INDIGO_OK;
		}
		if (!lunatico_set_stop_power(device, LA_POWER_CONTROL_STOP_ITEM->number.value)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_stop_power(%d) failed", PRIVATE_DATA->handle);
			LA_POWER_CONTROL_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, LA_POWER_CONTROL_PROPERTY, NULL);
			return INDIGO_OK;
		}

		indigo_update_property(device, LA_POWER_CONTROL_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(LA_TEMPERATURE_SENSOR_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- LA_TEMPERATURE_SENSOR
		if (!IS_CONNECTED) return INDIGO_OK;
		indigo_property_copy_values(LA_TEMPERATURE_SENSOR_PROPERTY, property, false);
		LA_TEMPERATURE_SENSOR_PROPERTY->state = INDIGO_OK_STATE;
		if (LA_TEMPERATURE_SENSOR_INTERNAL_ITEM->sw.value) {
			PRIVATE_DATA->temperature_sensor_index = 0;
		} else {
			PRIVATE_DATA->temperature_sensor_index = 1;
		}
		indigo_update_property(device, LA_TEMPERATURE_SENSOR_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(LA_WIRING_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- LA_WIRING_PROPERTY
		if (!IS_CONNECTED) return INDIGO_OK;
		bool res = true;
		indigo_property_copy_values(LA_WIRING_PROPERTY, property, false);
		LA_WIRING_PROPERTY->state = INDIGO_OK_STATE;
		if (LA_WIRING_LUNATICO_ITEM->sw.value) {
			if(FOCUSER_REVERSE_MOTION_DISABLED_ITEM->sw.value) {
				res = lunatico_set_wiring(device, MW_LUNATICO_NORMAL);
			} else {
				res = lunatico_set_wiring(device, MW_LUNATICO_REVERSED);
			}
		} else if (LA_WIRING_MOONLITE_ITEM->sw.value) {
			if (FOCUSER_REVERSE_MOTION_DISABLED_ITEM->sw.value) {
				res = lunatico_set_wiring(device, MW_MOONLITE_NORMAL);
			} else {
				res = lunatico_set_wiring(device, MW_MOONLITE_REVERSED);
			}
		} else {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Unsupported Motor wiring");
			LA_WIRING_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		if (res == false) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_wiring() failed");
			LA_WIRING_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		indigo_update_property(device, LA_WIRING_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(LA_MOTOR_TYPE_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- LA_MOTOR_TYPE_PROPERTY
		if (!IS_CONNECTED) return INDIGO_OK;
		bool res = true;
		indigo_property_copy_values(LA_MOTOR_TYPE_PROPERTY, property, false);
		LA_MOTOR_TYPE_PROPERTY->state = INDIGO_OK_STATE;
		if (LA_MOTOR_TYPE_UNIPOLAR_ITEM->sw.value) {
				res = lunatico_set_motor_type(device, MT_UNIPOLAR);
		} else if (LA_MOTOR_TYPE_BIPOLAR_ITEM->sw.value) {
				res = lunatico_set_motor_type(device, MT_BIPOLAR);
		} else if (LA_MOTOR_TYPE_DC_ITEM->sw.value) {
				res = lunatico_set_motor_type(device, MT_DC);
		} else if (LA_MOTOR_TYPE_STEP_DIR_ITEM->sw.value) {
				res = lunatico_set_motor_type(device, MT_STEP_DIR);
		} else {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Unsupported Motor type");
			LA_MOTOR_TYPE_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		if (res == false) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "lunatico_set_motor_type() failed");
			LA_MOTOR_TYPE_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		indigo_update_property(device, LA_MOTOR_TYPE_PROPERTY, NULL);
		return INDIGO_OK;
		// -------------------------------------------------------------------------------- FOCUSER_MODE
	} else if (indigo_property_match(FOCUSER_MODE_PROPERTY, property)) {
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
	} else if (indigo_property_match(CONFIG_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- CONFIG
		if (indigo_switch_match(CONFIG_SAVE_ITEM, property)) {
			indigo_save_property(device, NULL, LA_MODEL_HINT_PROPERTY);
			indigo_save_property(device, NULL, LA_STEP_MODE_PROPERTY);
			indigo_save_property(device, NULL, LA_POWER_CONTROL_PROPERTY);
			indigo_save_property(device, NULL, LA_TEMPERATURE_SENSOR_PROPERTY);
			indigo_save_property(device, NULL, LA_WIRING_PROPERTY);
			indigo_save_property(device, NULL, LA_MOTOR_TYPE_PROPERTY);
		}
		// --------------------------------------------------------------------------------
	}
	return indigo_focuser_change_property(device, client, property);
}


static indigo_result focuser_detach(indigo_device *device) {
	assert(device != NULL);
	lunatico_close(device);
	indigo_device_disconnect(NULL, device->name);
	indigo_release_property(LA_STEP_MODE_PROPERTY);
	indigo_release_property(LA_POWER_CONTROL_PROPERTY);
	indigo_release_property(LA_TEMPERATURE_SENSOR_PROPERTY);
	indigo_release_property(LA_WIRING_PROPERTY);
	indigo_release_property(LA_MOTOR_TYPE_PROPERTY);
	INDIGO_DEVICE_DETACH_LOG(DRIVER_NAME, device->name);

	indigo_delete_property(device, LA_MODEL_HINT_PROPERTY, NULL);
	indigo_release_property(LA_MODEL_HINT_PROPERTY);
	return indigo_focuser_detach(device);
}


// --------------------------------------------------------------------------------

static int device_number = 0;


static void create_port_device(int device_index, int port_index, char *name_ext) {
	static indigo_device focuser_template = INDIGO_DEVICE_INITIALIZER(
		FOCUSER_LUNATICO_NAME,
		focuser_attach,
		lunatico_enumerate_properties,
		focuser_change_property,
		NULL,
		focuser_detach
	);

	if (port_index >= MAX_PORTS) return;
	if (device_index >= MAX_DEVICES) return;
	if (device_data[device_index].port[port_index] != NULL) return;

	if (device_data[device_index].private_data == NULL) {
		device_data[device_index].private_data = malloc(sizeof(lunatico_private_data));
		assert(device_data[device_index].private_data != NULL);
		memset(device_data[device_index].private_data, 0, sizeof(lunatico_private_data));
		INDIGO_DRIVER_LOG(DRIVER_NAME, "ADD: PRIVATE_DATA");
	}

	device_data[device_index].port[port_index] = malloc(sizeof(indigo_device));
	assert(device_data[device_index].port[port_index] != NULL);
	memcpy(device_data[device_index].port[port_index], &focuser_template, sizeof(indigo_device));
	device_data[device_index].port[port_index]->private_data = device_data[device_index].private_data;
	sprintf(device_data[device_index].port[port_index]->name, "%s (%s)", FOCUSER_LUNATICO_NAME, name_ext);
	set_port_index(device_data[device_index].port[port_index], port_index);
	indigo_attach_device(device_data[device_index].port[port_index]);
	INDIGO_DRIVER_LOG(DRIVER_NAME, "ADD: Device with portindex = %d", get_port_index(device_data[device_index].port[port_index]));
}


static void delete_port_device(int device_index, int port_index) {
	if (port_index >= MAX_PORTS) return;
	if (device_index >= MAX_DEVICES) return;

	if (device_data[device_index].port[port_index] != NULL) {
		indigo_detach_device(device_data[device_index].port[port_index]);
		INDIGO_DRIVER_LOG(DRIVER_NAME, "REMOVE: Device with portindex = %d", get_port_index(device_data[device_index].port[port_index]));
		free(device_data[device_index].port[port_index]);
		device_data[device_index].port[port_index] = NULL;
	}

	for (int i = 0; i < MAX_PORTS; i++) {
		if (device_data[device_index].port[i] != NULL) return;
	}

	if (device_data[device_index].private_data != NULL) {
		free(device_data[device_index].private_data);
		device_data[device_index].private_data = NULL;
		INDIGO_DRIVER_LOG(DRIVER_NAME, "REMOVE: PRIVATE_DATA");
	}
}


indigo_result DRIVER_ENTRY_POINT(indigo_driver_action action, indigo_driver_info *info) {

	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, "Lunatico Astronomia Focuser", __FUNCTION__, DRIVER_VERSION, false, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch (action) {
	case INDIGO_DRIVER_INIT:
		last_action = action;

		create_port_device(0, 0, "Main");
		create_port_device(0, 1, "Ext");
		break;

	case INDIGO_DRIVER_SHUTDOWN:
		last_action = action;
		for (int index = 0; index < MAX_PORTS; index++) {
			delete_port_device(0, index);
		}
		break;

	case INDIGO_DRIVER_INFO:
		break;
	}

	return INDIGO_OK;
}