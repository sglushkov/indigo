// Copyright (c) 2020 Rumen G. Bogdanovski
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

/** INDIGO Astromi.ch MGBox driver
 \file indigo_aux_mgbox.c
 */

#define DRIVER_VERSION 0x0001
#define DRIVER_NAME	"idnigo_aux_mgbox"

#define DEFAULT_BAUDRATE "9600"

// gp_bits is used as boolean
#define is_connected               gp_bits

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <assert.h>

#include <indigo/indigo_driver_xml.h>
#include <indigo/indigo_io.h>

#include "indigo_aux_mgbox.h"

#define PRIVATE_DATA        ((nmea_private_data *)device->private_data)

#define SETTINGS_GROUP	 "Settings"
#define THRESHOLDS_GROUP "Tresholds"
#define WARNINGS_GROUP   "Warnings"
#define WEATHER_GROUP    "Weather"
#define SWITCH_GROUP     "Switch Control"
#define STATUS_GROUP     "Device status"

// Switch
#define AUX_OUTLET_NAMES_PROPERTY      (PRIVATE_DATA->outlet_names_property)
#define AUX_OUTLET_NAME_1_ITEM         (AUX_OUTLET_NAMES_PROPERTY->items + 0)

#define AUX_GPIO_OUTLET_PROPERTY       (PRIVATE_DATA->gpio_outlet_property)
#define AUX_GPIO_OUTLET_1_ITEM         (AUX_GPIO_OUTLET_PROPERTY->items + 0)


#define X_CORRECTION_PROPERTY_NAME  "X_WEATHER_CORRECTION"

#define X_CORRECTION_PROPERTY               (PRIVATE_DATA->sky_correction_property)
#define X_CORRECTION_TEMPERATURE_ITEM       (X_CORRECTION_PROPERTY->items + 0)
#define X_CORRECTION_HUMIDIDTY_ITEM         (X_CORRECTION_PROPERTY->items + 1)
#define X_CORRECTION_PRESSURE_ITEM          (X_CORRECTION_PROPERTY->items + 2)

#define AUX_WEATHER_PROPERTY                     (PRIVATE_DATA->weather_property)
#define AUX_WEATHER_TEMPERATURE_ITEM             (AUX_WEATHER_PROPERTY->items + 0)
#define AUX_WEATHER_DEWPOINT_ITEM                (AUX_WEATHER_PROPERTY->items + 1)
#define AUX_WEATHER_HUMIDITY_ITEM                (AUX_WEATHER_PROPERTY->items + 2)
#define AUX_WEATHER_PRESSURE_ITEM                 (AUX_WEATHER_PROPERTY->items + 3)

// DEW
#define AUX_DEW_THRESHOLD_PROPERTY				(PRIVATE_DATA->dew_threshold_property)
#define AUX_DEW_THRESHOLD_SENSOR_1_ITEM			(AUX_DEW_THRESHOLD_PROPERTY->items + 0)

#define AUX_DEW_WARNING_PROPERTY				(PRIVATE_DATA->dew_warning_property)
#define AUX_DEW_WARNING_SENSOR_1_ITEM			(AUX_DEW_WARNING_PROPERTY->items + 0)

typedef struct {
	int handle;
	int count_open;
	pthread_mutex_t serial_mutex;
	char firmware[INDIGO_VALUE_SIZE];
	indigo_property *outlet_names_property,
	                *gpio_outlet_property,
	                *sky_correction_property,
	                *weather_property,
	                *dew_threshold_property,
	                *dew_warning_property;
} nmea_private_data;

static nmea_private_data *private_data = NULL;
static indigo_device *gps = NULL;
static indigo_device *aux_weather = NULL;
static indigo_timer *global_timer = NULL;

static char **parse(char *buffer) {
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "%s", buffer);
	int offset = 3;
	if (strncmp("$GP", buffer, 3) && strncmp("$P", buffer, 2)) return NULL;
	else if (buffer[1] == 'G') offset = 3;
	else offset = 2;

	char *index = strchr(buffer, '*');
	if (index) {
		*index++ = 0;
		int c1 = (int)strtol(index, NULL, 16);
		int c2 = 0;
		index = buffer + 1;
		while (*index)
			c2 ^= *index++;
		if (c1 != c2)
			return NULL;
	}
	static char *tokens[32];
	int token = 0;
	memset(tokens, 0, sizeof(tokens));
	index = buffer + offset;
	while (index) {
		tokens[token++] = index;
		index = strchr(index, ',');
		if (index)
			*index++ = 0;
	}
	return tokens;
}

static void gps_refresh_callback(indigo_device *gdevice) {
	char buffer[128];
	char **tokens;
	indigo_device* device;
	static bool inject = false;
	device = gps;
	INDIGO_DRIVER_LOG(DRIVER_NAME, "NMEA reader started");
	while (PRIVATE_DATA->handle > 0) {
		if (indigo_read_line(PRIVATE_DATA->handle, buffer, sizeof(buffer)) > 0 && (tokens = parse(buffer))) {
			// GPS Update
			device = gps;
			if (!strcmp(tokens[0], "RMC")) { // Recommended Minimum sentence C
				int time = atoi(tokens[1]);
				int date = atoi(tokens[9]);
				sprintf(GPS_UTC_ITEM->text.value, "20%02d-%02d-%02dT%02d:%02d:%02d", date % 100, (date / 100) % 100, date / 10000, time / 10000, (time / 100) % 100, time % 100);
				GPS_UTC_TIME_PROPERTY->state = INDIGO_OK_STATE;
				indigo_update_property(device, GPS_UTC_TIME_PROPERTY, NULL);
				double lat = indigo_atod(tokens[3]);
				lat = floor(lat / 100) + fmod(lat, 100) / 60;
				if (!strcmp(tokens[4], "S"))
					lat = -lat;
				lat = round(lat * 10000) / 10000;
				double lon = indigo_atod(tokens[5]);
				lon = floor(lon / 100) + fmod(lon, 100) / 60;
				if (!strcmp(tokens[6], "W"))
					lon = -lon;
				lon = round(lon * 10000) / 10000;
				if (GPS_GEOGRAPHIC_COORDINATES_LONGITUDE_ITEM->number.value != lon || GPS_GEOGRAPHIC_COORDINATES_LATITUDE_ITEM->number.value != lat) {
					GPS_GEOGRAPHIC_COORDINATES_LONGITUDE_ITEM->number.value = lon;
					GPS_GEOGRAPHIC_COORDINATES_LATITUDE_ITEM->number.value = lat;
					GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state = INDIGO_OK_STATE;
					indigo_update_property(device, GPS_GEOGRAPHIC_COORDINATES_PROPERTY, NULL);
				}
			} else if (!strcmp(tokens[0], "GGA")) { // Global Positioning System Fix Data
				double lat = indigo_atod(tokens[2]);
				lat = floor(lat / 100) + fmod(lat, 100) / 60;
				if (!strcmp(tokens[3], "S"))
					lat = -lat;
				lat = round(lat * 10000) / 10000;
				double lon = indigo_atod(tokens[4]);
				lon = floor(lon / 100) + fmod(lon, 100) / 60;
				if (!strcmp(tokens[5], "W"))
					lon = -lon;
				lon = round(lon * 10000) / 10000;
				double elv = round(indigo_atod(tokens[9]));
				if (GPS_GEOGRAPHIC_COORDINATES_LONGITUDE_ITEM->number.value != lon || GPS_GEOGRAPHIC_COORDINATES_LATITUDE_ITEM->number.value != lat || GPS_GEOGRAPHIC_COORDINATES_ELEVATION_ITEM->number.value != elv) {
					GPS_GEOGRAPHIC_COORDINATES_LONGITUDE_ITEM->number.value = lon;
					GPS_GEOGRAPHIC_COORDINATES_LATITUDE_ITEM->number.value = lat;
					GPS_GEOGRAPHIC_COORDINATES_ELEVATION_ITEM->number.value = elv;
					GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state = INDIGO_OK_STATE;
					indigo_update_property(device, GPS_GEOGRAPHIC_COORDINATES_PROPERTY, NULL);
				}
				int in_use = atoi(tokens[7]);
				if (GPS_ADVANCED_STATUS_SVS_IN_USE_ITEM->number.value != in_use) {
					GPS_ADVANCED_STATUS_SVS_IN_USE_ITEM->number.value = in_use;
					GPS_ADVANCED_STATUS_PROPERTY->state = INDIGO_OK_STATE;
					if (GPS_ADVANCED_ENABLED_ITEM->sw.value) {
						indigo_update_property(device, GPS_ADVANCED_STATUS_PROPERTY, NULL);
					}
				}
			} else if (!strcmp(tokens[0], "GSV")) { // Satellites in view
				int in_view = atoi(tokens[3]);
				if (GPS_ADVANCED_STATUS_SVS_IN_VIEW_ITEM->number.value != in_view) {
					GPS_ADVANCED_STATUS_SVS_IN_VIEW_ITEM->number.value = in_view;
					GPS_ADVANCED_STATUS_PROPERTY->state = INDIGO_OK_STATE;
					if (GPS_ADVANCED_ENABLED_ITEM->sw.value) {
						indigo_update_property(device, GPS_ADVANCED_STATUS_PROPERTY, NULL);
					}
				}
			} else if (!strcmp(tokens[0], "GSA")) { // Satellite status
				char fix = *tokens[2] - '0';
				if (fix == 1 && GPS_STATUS_NO_FIX_ITEM->light.value != INDIGO_ALERT_STATE) {
					GPS_STATUS_NO_FIX_ITEM->light.value = INDIGO_ALERT_STATE;
					GPS_STATUS_2D_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
					GPS_STATUS_3D_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
					GPS_STATUS_PROPERTY->state = INDIGO_OK_STATE;
					if (GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state != INDIGO_BUSY_STATE) {
						GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state = INDIGO_BUSY_STATE;
						indigo_update_property(device, GPS_GEOGRAPHIC_COORDINATES_PROPERTY, NULL);
					}
					if (GPS_UTC_TIME_PROPERTY->state != INDIGO_BUSY_STATE) {
						GPS_UTC_TIME_PROPERTY->state = INDIGO_BUSY_STATE;
						indigo_update_property(device, GPS_UTC_TIME_PROPERTY, NULL);
					}
					indigo_update_property(device, GPS_STATUS_PROPERTY, NULL);
				} else if (fix == 2 && GPS_STATUS_2D_FIX_ITEM->light.value != INDIGO_BUSY_STATE) {
					GPS_STATUS_NO_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
					GPS_STATUS_2D_FIX_ITEM->light.value = INDIGO_BUSY_STATE;
					GPS_STATUS_3D_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
					GPS_STATUS_PROPERTY->state = INDIGO_OK_STATE;
					indigo_update_property(device, GPS_STATUS_PROPERTY, NULL);
					if (GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state != INDIGO_BUSY_STATE) {
						GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state = INDIGO_BUSY_STATE;
						indigo_update_property(device, GPS_GEOGRAPHIC_COORDINATES_PROPERTY, NULL);
					}
					if (GPS_UTC_TIME_PROPERTY->state != INDIGO_BUSY_STATE) {
						GPS_UTC_TIME_PROPERTY->state = INDIGO_BUSY_STATE;
						indigo_update_property(device, GPS_UTC_TIME_PROPERTY, NULL);
					}
				} else if (fix == 3 && GPS_STATUS_3D_FIX_ITEM->light.value != INDIGO_OK_STATE) {
					GPS_STATUS_NO_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
					GPS_STATUS_2D_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
					GPS_STATUS_3D_FIX_ITEM->light.value = INDIGO_OK_STATE;
					GPS_STATUS_PROPERTY->state = INDIGO_OK_STATE;
					if (GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state != INDIGO_OK_STATE) {
						GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state = INDIGO_OK_STATE;
						indigo_update_property(device, GPS_GEOGRAPHIC_COORDINATES_PROPERTY, NULL);
					}
					if (GPS_UTC_TIME_PROPERTY->state != INDIGO_OK_STATE) {
						GPS_UTC_TIME_PROPERTY->state = INDIGO_OK_STATE;
						indigo_update_property(device, GPS_UTC_TIME_PROPERTY, NULL);
					}
					indigo_update_property(device, GPS_STATUS_PROPERTY, NULL);
				}
				double pdop = indigo_atod(tokens[15]);
				double hdop = indigo_atod(tokens[16]);
				double vdop = indigo_atod(tokens[17]);
				if (GPS_ADVANCED_STATUS_PDOP_ITEM->number.value != pdop || GPS_ADVANCED_STATUS_HDOP_ITEM->number.value != hdop || GPS_ADVANCED_STATUS_VDOP_ITEM->number.value != vdop) {
					GPS_ADVANCED_STATUS_PDOP_ITEM->number.value = pdop;
					GPS_ADVANCED_STATUS_HDOP_ITEM->number.value = hdop;
					GPS_ADVANCED_STATUS_VDOP_ITEM->number.value = vdop;
					GPS_ADVANCED_STATUS_PROPERTY->state = INDIGO_OK_STATE;
					if (GPS_ADVANCED_ENABLED_ITEM->sw.value) {
						indigo_update_property(device, GPS_ADVANCED_STATUS_PROPERTY, NULL);
					}
				}
			}
			/*
			if (inject) {
				INDIGO_DRIVER_LOG(DRIVER_NAME, "INJECT");
				strcpy(buffer, "$PXDR,P,96816.0,P,0,C,24.9,C,1,H,34.0,P,2,C,8.0,C,3,1.1*04");
				tokens = parse(buffer);
				inject = false;
			} else {
				strcpy(buffer, "$PCAL,P,16960,T,16960,H,0,MM,0,MG,0*68");
				tokens = parse(buffer);
				inject = true;
				if (!tokens) {
					INDIGO_DRIVER_LOG(DRIVER_NAME, "TOKEN: NULL");
				}
			}
			*/
			// Weather update
			device = aux_weather;

			if (!strcmp(tokens[0], "XDR")) { // Weather data
				INDIGO_DRIVER_LOG(DRIVER_NAME, "PXDR");
				AUX_WEATHER_PRESSURE_ITEM->number.value = indigo_atod(tokens[2]) / 100.0; // We need hPa
				AUX_WEATHER_TEMPERATURE_ITEM->number.value = indigo_atod(tokens[6]);
				AUX_WEATHER_HUMIDITY_ITEM->number.value = indigo_atod(tokens[10]);
				AUX_WEATHER_DEWPOINT_ITEM->number.value = indigo_atod(tokens[14]);
				AUX_WEATHER_PROPERTY->state = INDIGO_OK_STATE;
				indigo_update_property(device, AUX_WEATHER_PROPERTY, NULL);
				if (PRIVATE_DATA->firmware[0] == '\0') {
					strncpy(INFO_DEVICE_FW_REVISION_ITEM->text.value, tokens[17], INDIGO_VALUE_SIZE);
					strncpy(PRIVATE_DATA->firmware, tokens[17], INDIGO_VALUE_SIZE);
					indigo_update_property(device, INFO_PROPERTY, NULL);
				}
			} else if (!strcmp(tokens[0], "CAL")) {
				X_CORRECTION_PRESSURE_ITEM->number.value = indigo_atod(tokens[2]) / 10.0;
				X_CORRECTION_TEMPERATURE_ITEM->number.value = indigo_atod(tokens[4]) / 10.0;
				X_CORRECTION_HUMIDIDTY_ITEM->number.value = indigo_atod(tokens[6]) / 10.0;
				X_CORRECTION_PROPERTY->state = INDIGO_OK_STATE;
				indigo_update_property(device, X_CORRECTION_PROPERTY, NULL);
				// handle switches here
			}
		}

	//
	}
	INDIGO_DRIVER_LOG(DRIVER_NAME, "NMEA reader finished");
}

// -------------------------------------------------------------------------------- INDIGO GPS device implementation

static bool mgbox_open(indigo_device *device) {
	pthread_mutex_lock(&PRIVATE_DATA->serial_mutex);
	if (PRIVATE_DATA->count_open++ == 0) {
		char *name = DEVICE_PORT_ITEM->text.value;
		if (!indigo_is_device_url(name, "mgbox")) {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Opening local device on port: '%s', baudrate = %s", DEVICE_PORT_ITEM->text.value, DEVICE_BAUDRATE_ITEM->text.value);
			PRIVATE_DATA->handle = indigo_open_serial_with_speed(name, atoi(DEVICE_BAUDRATE_ITEM->text.value));
		} else {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Opening netwotk device on host: %s", DEVICE_PORT_ITEM->text.value);
			indigo_network_protocol proto = INDIGO_PROTOCOL_TCP;
			PRIVATE_DATA->handle = indigo_open_network_device(name, 9999, &proto);
		}
		if (PRIVATE_DATA->handle >= 0) {
			INDIGO_DRIVER_LOG(DRIVER_NAME, "Connected to %s", name);
			indigo_set_timer(gps, 0, gps_refresh_callback, &global_timer);
		} else {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to connect to %s", name);
			PRIVATE_DATA->count_open--;
			pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
			return false;
		}
	}
	pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
	return true;
}

static void mgbox_close(indigo_device *device) {
	pthread_mutex_lock(&PRIVATE_DATA->serial_mutex);
	if (--PRIVATE_DATA->count_open == 0) {
		close(PRIVATE_DATA->handle);
		PRIVATE_DATA->handle = -1;
		indigo_cancel_timer_sync(gps, &global_timer);
		INDIGO_DRIVER_LOG(DRIVER_NAME, "Disconnected from %s", DEVICE_PORT_ITEM->text.value);
	}
	pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
}

static indigo_result gps_attach(indigo_device *device) {
	assert(device != NULL);
	assert(PRIVATE_DATA != NULL);
	if (indigo_gps_attach(device, DRIVER_VERSION) == INDIGO_OK) {
		pthread_mutex_init(&PRIVATE_DATA->serial_mutex, NULL);
		SIMULATION_PROPERTY->hidden = true;
		DEVICE_PORT_PROPERTY->hidden = false;
		DEVICE_PORTS_PROPERTY->hidden = false;
		DEVICE_BAUDRATE_PROPERTY->hidden = false;
		GPS_ADVANCED_PROPERTY->hidden = false;
		GPS_GEOGRAPHIC_COORDINATES_PROPERTY->hidden = false;
		GPS_GEOGRAPHIC_COORDINATES_PROPERTY->count = 3;
		GPS_UTC_TIME_PROPERTY->hidden = false;
		GPS_UTC_TIME_PROPERTY->count = 1;
#ifdef INDIGO_LINUX
		for (int i = 0; i < DEVICE_PORTS_PROPERTY->count; i++) {
			if (strstr(DEVICE_PORTS_PROPERTY->items[i].name, "ttyGPS")) {
				strncpy(DEVICE_PORT_ITEM->text.value, DEVICE_PORTS_PROPERTY->items[i].name, INDIGO_VALUE_SIZE);
				break;
			}
		}
#endif
		INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);
		return indigo_gps_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static void gps_connect_callback(indigo_device *device) {
	if (CONNECTION_CONNECTED_ITEM->sw.value) {
		if (!device->is_connected) {
			if (mgbox_open(device)) {
				GPS_GEOGRAPHIC_COORDINATES_PROPERTY->state = INDIGO_BUSY_STATE;
				GPS_GEOGRAPHIC_COORDINATES_LONGITUDE_ITEM->number.value = 0;
				GPS_GEOGRAPHIC_COORDINATES_LATITUDE_ITEM->number.value = 0;
				GPS_GEOGRAPHIC_COORDINATES_ELEVATION_ITEM->number.value = 0;
				GPS_STATUS_NO_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
				GPS_STATUS_2D_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
				GPS_STATUS_3D_FIX_ITEM->light.value = INDIGO_IDLE_STATE;
				GPS_STATUS_PROPERTY->state = INDIGO_BUSY_STATE;
				GPS_UTC_TIME_PROPERTY->state = INDIGO_BUSY_STATE;
				sprintf(GPS_UTC_ITEM->text.value, "0000-00-00T00:00:00.00");
				device->is_connected = true;
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
			} else {
				indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
				device->is_connected = false;
				CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
			}
		}
	} else {
		if (device->is_connected) {
			mgbox_close(device);
			device->is_connected = false;
			CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
		}
	}
	indigo_gps_change_property(device, NULL, CONNECTION_PROPERTY);
}

static indigo_result gps_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);
	// -------------------------------------------------------------------------------- CONNECTION
	if (indigo_property_match(CONNECTION_PROPERTY, property)) {
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		CONNECTION_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, CONNECTION_PROPERTY, NULL);
		indigo_set_timer(device, 0, gps_connect_callback, NULL);
		return INDIGO_OK;
	}
	return indigo_gps_change_property(device, client, property);
}

static indigo_result gps_detach(indigo_device *device) {
	assert(device != NULL);
	if (IS_CONNECTED) {
		indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
		gps_connect_callback(device);
	}
	INDIGO_DEVICE_DETACH_LOG(DRIVER_NAME, device->name);
	return indigo_gps_detach(device);
}

// -----------------------XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX-----------------

static int aux_init_properties(indigo_device *device) {
	// -------------------------------------------------------------------------------- SIMULATION
	SIMULATION_PROPERTY->hidden = true;
	// -------------------------------------------------------------------------------- DEVICE_PORT
	DEVICE_PORT_PROPERTY->hidden = false;
	// -------------------------------------------------------------------------------- DEVICE_PORTS
	DEVICE_PORTS_PROPERTY->hidden = false;
	// -------------------------------------------------------------------------------- DEVICE_BAUDRATE
	DEVICE_BAUDRATE_PROPERTY->hidden = true;
	strncpy(DEVICE_BAUDRATE_ITEM->text.value, DEFAULT_BAUDRATE, INDIGO_VALUE_SIZE);
	// --------------------------------------------------------------------------------
	INFO_PROPERTY->count = 7;
	// -------------------------------------------------------------------------------- GPIO OUTLETS
	AUX_GPIO_OUTLET_PROPERTY = indigo_init_switch_property(NULL, device->name, AUX_GPIO_OUTLETS_PROPERTY_NAME, SWITCH_GROUP, "Switch outlet", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE, 1);
	if (AUX_GPIO_OUTLET_PROPERTY == NULL)
		return INDIGO_FAILED;
	indigo_init_switch_item(AUX_GPIO_OUTLET_1_ITEM, AUX_GPIO_OUTLETS_OUTLET_1_ITEM_NAME, "Switch", false);
	// -------------------------------------------------------------------------------- OUTLET_NAMES
	AUX_OUTLET_NAMES_PROPERTY = indigo_init_text_property(NULL, device->name, AUX_OUTLET_NAMES_PROPERTY_NAME, SWITCH_GROUP, "Switch name", INDIGO_OK_STATE, INDIGO_RW_PERM, 1);
	if (AUX_OUTLET_NAMES_PROPERTY == NULL)
		return INDIGO_FAILED;
	indigo_init_text_item(AUX_OUTLET_NAME_1_ITEM, AUX_GPIO_OUTLET_NAME_1_ITEM_NAME, "Internal switch", "Switch");
	// -------------------------------------------------------------------------------- DEW_THRESHOLD
	AUX_DEW_THRESHOLD_PROPERTY = indigo_init_number_property(NULL, device->name, AUX_DEW_THRESHOLD_PROPERTY_NAME, THRESHOLDS_GROUP, "Dew warning threshold", INDIGO_OK_STATE, INDIGO_RW_PERM, 1);
	if (AUX_DEW_THRESHOLD_PROPERTY == NULL)
		return INDIGO_FAILED;
	indigo_init_number_item(AUX_DEW_THRESHOLD_SENSOR_1_ITEM, AUX_DEW_THRESHOLD_SENSOR_1_ITEM_NAME, "Temerature difference (°C)", 0, 9, 0, 2);
	// -------------------------------------------------------------------------------- DEW_WARNING
	AUX_DEW_WARNING_PROPERTY = indigo_init_light_property(NULL, device->name, AUX_DEW_WARNING_PROPERTY_NAME, WARNINGS_GROUP, "Dew warning", INDIGO_BUSY_STATE, 1);
	if (AUX_DEW_WARNING_PROPERTY == NULL)
		return INDIGO_FAILED;
	indigo_init_light_item(AUX_DEW_WARNING_SENSOR_1_ITEM, AUX_DEW_WARNING_SENSOR_1_ITEM_NAME, "Dew warning", INDIGO_IDLE_STATE);
	// -------------------------------------------------------------------------------- X_CORRECTION
	X_CORRECTION_PROPERTY = indigo_init_number_property(NULL, device->name, X_CORRECTION_PROPERTY_NAME, SETTINGS_GROUP, "Weather correction factors", INDIGO_OK_STATE, INDIGO_RW_PERM, 3);
	if (X_CORRECTION_PROPERTY == NULL)
		return INDIGO_FAILED;
	indigo_init_number_item(X_CORRECTION_TEMPERATURE_ITEM, AUX_WEATHER_TEMPERATURE_ITEM_NAME, "Temperature (°C)", -999, 999, 0, 0);
	indigo_init_number_item(X_CORRECTION_HUMIDIDTY_ITEM, AUX_WEATHER_HUMIDITY_ITEM_NAME, "Relative Humidity (%)", -99, 99, 0, 0);
	indigo_init_number_item(X_CORRECTION_PRESSURE_ITEM, AUX_WEATHER_PRESSURE_ITEM_NAME, "Atmospheric Pressure (hPa)", -999, 999, 0, 0);
	// -------------------------------------------------------------------------------- AUX_WEATHER
	AUX_WEATHER_PROPERTY = indigo_init_number_property(NULL, device->name, AUX_WEATHER_PROPERTY_NAME, WEATHER_GROUP, "Weather conditions", INDIGO_BUSY_STATE, INDIGO_RO_PERM, 4);
	if (AUX_WEATHER_PROPERTY == NULL)
		return INDIGO_FAILED;
	indigo_init_number_item(AUX_WEATHER_TEMPERATURE_ITEM, AUX_WEATHER_TEMPERATURE_ITEM_NAME, "Ambient temperature (°C)", -200, 80, 0, 0);
	strncpy(AUX_WEATHER_TEMPERATURE_ITEM->number.format, "%.1f", INDIGO_VALUE_SIZE);
	indigo_init_number_item(AUX_WEATHER_DEWPOINT_ITEM, AUX_WEATHER_DEWPOINT_ITEM_NAME, "Dewpoint (°C)", -200, 80, 1, 0);
	strncpy(AUX_WEATHER_DEWPOINT_ITEM->number.format, "%.1f", INDIGO_VALUE_SIZE);
	indigo_init_number_item(AUX_WEATHER_HUMIDITY_ITEM, AUX_WEATHER_HUMIDITY_ITEM_NAME, "Relative humidity (%)", 0, 100, 0, 0);
	strncpy(AUX_WEATHER_HUMIDITY_ITEM->number.format, "%.0f", INDIGO_VALUE_SIZE);
	indigo_init_number_item(AUX_WEATHER_PRESSURE_ITEM, AUX_WEATHER_PRESSURE_ITEM_NAME, "Atmospheric Pressure (hPa)", 0, 10000, 0, 0);
	strncpy(AUX_WEATHER_PRESSURE_ITEM->number.format, "%.1f", INDIGO_VALUE_SIZE);
	//---------------------------------------------------------------------------
	return INDIGO_OK;
}

static indigo_result aux_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property) {
	if (IS_CONNECTED) {
		if (indigo_property_match(AUX_GPIO_OUTLET_PROPERTY, property))
			indigo_define_property(device, AUX_GPIO_OUTLET_PROPERTY, NULL);
		if (indigo_property_match(AUX_WEATHER_PROPERTY, property))
			indigo_define_property(device, AUX_WEATHER_PROPERTY, NULL);
		if (indigo_property_match(AUX_DEW_WARNING_PROPERTY, property))
			indigo_define_property(device, AUX_DEW_WARNING_PROPERTY, NULL);
	}
	if (indigo_property_match(AUX_OUTLET_NAMES_PROPERTY, property))
		indigo_define_property(device, AUX_OUTLET_NAMES_PROPERTY, NULL);
	if (indigo_property_match(X_CORRECTION_PROPERTY, property))
		indigo_define_property(device, X_CORRECTION_PROPERTY, NULL);
	if (indigo_property_match(AUX_DEW_THRESHOLD_PROPERTY, property))
		indigo_define_property(device, AUX_DEW_THRESHOLD_PROPERTY, NULL);

	return indigo_aux_enumerate_properties(device, NULL, NULL);
}


static indigo_result aux_attach(indigo_device *device) {
	assert(device != NULL);
	assert(PRIVATE_DATA != NULL);
	if (indigo_aux_attach(device, DRIVER_VERSION, INDIGO_INTERFACE_AUX_WEATHER) == INDIGO_OK) {
		// --------------------------------------------------------------------------------
		if (aux_init_properties(device) != INDIGO_OK) return INDIGO_FAILED;
		INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);
		return aux_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}


static void handle_aux_connect_property(indigo_device *device) {
	if (CONNECTION_CONNECTED_ITEM->sw.value) {
		if (!device->is_connected) {
			if (mgbox_open(device)) {
				char board[INDIGO_VALUE_SIZE] = "N/A";
				char firmware[INDIGO_VALUE_SIZE] = "N/A";
				char serial_number[INDIGO_VALUE_SIZE] = "N/A";

				strncpy(INFO_DEVICE_MODEL_ITEM->text.value, board, INDIGO_VALUE_SIZE);
				strncpy(INFO_DEVICE_FW_REVISION_ITEM->text.value, firmware, INDIGO_VALUE_SIZE);
				strncpy(INFO_DEVICE_SERIAL_NUM_ITEM->text.value, serial_number, INDIGO_VALUE_SIZE);
				//aag_get_swith(device, &AUX_GPIO_OUTLET_1_ITEM->sw.value);
				indigo_define_property(device, AUX_GPIO_OUTLET_PROPERTY, NULL);
				indigo_define_property(device, AUX_WEATHER_PROPERTY, NULL);
				indigo_define_property(device, AUX_DEW_WARNING_PROPERTY, NULL);
				device->is_connected = true;
				//indigo_set_timer(device, 0, sensors_timer_callback, &PRIVATE_DATA->sensors_timer);
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
			} else {
				CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
				device->is_connected = false;
				indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, false);
			}
		}
	} else {
		if (device->is_connected) {
			// Stop timer faster - do not wait to finish readout cycle
			//indigo_cancel_timer_sync(device, &PRIVATE_DATA->sensors_timer);
			indigo_delete_property(device, AUX_GPIO_OUTLET_PROPERTY, NULL);
			indigo_delete_property(device, AUX_WEATHER_PROPERTY, NULL);
			indigo_delete_property(device, AUX_DEW_WARNING_PROPERTY, NULL);
			mgbox_close(device);
			device->is_connected = false;
			CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
		}
	}
	indigo_aux_change_property(device, NULL, CONNECTION_PROPERTY);
}


static indigo_result aux_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);
	if (indigo_property_match(CONNECTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- CONNECTION
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		CONNECTION_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, CONNECTION_PROPERTY, NULL);
		indigo_set_timer(device, 0, handle_aux_connect_property, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AUX_OUTLET_NAMES_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- X_AUX_OUTLET_NAMES
		indigo_property_copy_values(AUX_OUTLET_NAMES_PROPERTY, property, false);
		if (IS_CONNECTED) {
			indigo_delete_property(device, AUX_GPIO_OUTLET_PROPERTY, NULL);
		}
		snprintf(AUX_GPIO_OUTLET_1_ITEM->label, INDIGO_NAME_SIZE, "%s", AUX_OUTLET_NAME_1_ITEM->text.value);
		if (IS_CONNECTED) {
			indigo_define_property(device, AUX_GPIO_OUTLET_PROPERTY, NULL);
		}
		AUX_OUTLET_NAMES_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AUX_OUTLET_NAMES_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AUX_GPIO_OUTLET_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AUX_GPIO_OUTLET
		indigo_property_copy_values(AUX_GPIO_OUTLET_PROPERTY, property, false);
		if (!IS_CONNECTED) return INDIGO_OK;

		bool success = false;
		if (AUX_GPIO_OUTLET_1_ITEM->sw.value) {
			//success = aag_close_swith(device);
		} else {
			//success = aag_open_swith(device);
		}
		if (success) {
			AUX_GPIO_OUTLET_PROPERTY->state = INDIGO_OK_STATE;
			indigo_update_property(device, AUX_GPIO_OUTLET_PROPERTY, NULL);
		} else {
			AUX_GPIO_OUTLET_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, AUX_GPIO_OUTLET_PROPERTY, "Open/Close switch failed");
		}
		return INDIGO_OK;
	} else if (indigo_property_match(X_CORRECTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- X_CORRECTION
		indigo_property_copy_values(X_CORRECTION_PROPERTY, property, false);
		X_CORRECTION_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, X_CORRECTION_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AUX_DEW_THRESHOLD_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AUX_DEW_THRESHOLD
		indigo_property_copy_values(AUX_DEW_THRESHOLD_PROPERTY, property, false);
		AUX_DEW_THRESHOLD_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AUX_DEW_THRESHOLD_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(CONFIG_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- CONFIG
		if (indigo_switch_match(CONFIG_SAVE_ITEM, property)) {
			indigo_save_property(device, NULL, AUX_OUTLET_NAMES_PROPERTY);
			indigo_save_property(device, NULL, AUX_DEW_THRESHOLD_PROPERTY);
		}
	}
	// --------------------------------------------------------------------------------
	return indigo_aux_change_property(device, client, property);
}

static indigo_result aux_detach(indigo_device *device) {
	assert(device != NULL);
	if (IS_CONNECTED) {
		indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
		handle_aux_connect_property(device);
	}
	indigo_release_property(AUX_GPIO_OUTLET_PROPERTY);
	indigo_release_property(AUX_WEATHER_PROPERTY);
	indigo_release_property(AUX_DEW_WARNING_PROPERTY);

	INDIGO_DEVICE_DETACH_LOG(DRIVER_NAME, device->name);

	indigo_delete_property(device, AUX_OUTLET_NAMES_PROPERTY, NULL);
	indigo_release_property(AUX_OUTLET_NAMES_PROPERTY);

	indigo_delete_property(device, X_CORRECTION_PROPERTY, NULL);
	indigo_release_property(X_CORRECTION_PROPERTY);

	indigo_delete_property(device, AUX_DEW_THRESHOLD_PROPERTY, NULL);
	indigo_release_property(AUX_DEW_THRESHOLD_PROPERTY);

	return indigo_aux_detach(device);
}

// --------------------------------------------------------------------------------

indigo_result indigo_aux_mgbox(indigo_driver_action action, indigo_driver_info *info) {
	static indigo_device gps_template = INDIGO_DEVICE_INITIALIZER(
		GPS_MGBOX_NAME,
		gps_attach,
		indigo_gps_enumerate_properties,
		gps_change_property,
		NULL,
		gps_detach
	);

	static indigo_device aux_template = INDIGO_DEVICE_INITIALIZER(
		WEATHER_MGBOX_NAME,
		aux_attach,
		aux_enumerate_properties,
		aux_change_property,
		NULL,
		aux_detach
	);

	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, "Astromi.ch MGBox", __FUNCTION__, DRIVER_VERSION, false, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch (action) {
	case INDIGO_DRIVER_INIT:
		last_action = action;
		private_data = malloc(sizeof(nmea_private_data));
		assert(private_data != NULL);
		memset(private_data, 0, sizeof(nmea_private_data));
		private_data->handle = -1;
		gps = malloc(sizeof(indigo_device));
		assert(gps != NULL);
		memcpy(gps, &gps_template, sizeof(indigo_device));
		gps->private_data = private_data;
		indigo_attach_device(gps);

		aux_weather = malloc(sizeof(indigo_device));
		assert(aux_weather != NULL);
		memcpy(aux_weather, &aux_template, sizeof(indigo_device));
		sprintf(aux_weather->name, "%s", WEATHER_MGBOX_NAME);
		aux_weather->private_data = private_data;
		indigo_attach_device(aux_weather);

		break;

	case INDIGO_DRIVER_SHUTDOWN:
		VERIFY_NOT_CONNECTED(gps);
		VERIFY_NOT_CONNECTED(aux_weather);
		last_action = action;
		if (gps != NULL) {
			indigo_detach_device(gps);
			free(gps);
			gps = NULL;
		}
		if (aux_weather != NULL) {
			indigo_detach_device(aux_weather);
			free(aux_weather);
			aux_weather = NULL;
		}
		if (private_data != NULL) {
			free(private_data);
			private_data = NULL;
		}
		break;

	case INDIGO_DRIVER_INFO:
		break;
	}

	return INDIGO_OK;
}
