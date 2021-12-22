// Copyright (c) 2021 CloudMakers, s. r. o.
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
// 2.0 by Peter Polakovic <peter.polakovic@cloudmakers.eu>

/** INDIGO Generic platesolver base
 \file indigo_platesolver_driver.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <indigo/indigo_agent.h>
#include <indigo/indigo_filter.h>
#include <indigo/indigo_novas.h>
#include <indigo/indigo_platesolver.h>

// -------------------------------------------------------------------------------- INDIGO agent device implementation

static bool validate_related_agent(indigo_device *device, indigo_property *info_property, int mask) {
	if ((mask & INDIGO_INTERFACE_CCD) == INDIGO_INTERFACE_CCD)
		return true;
	if (!strncmp(info_property->device, "Mount Agent", 11))
		return true;
	return false;
}

void indigo_platesolver_save_config(indigo_device *device) {
	if (pthread_mutex_trylock(&DEVICE_CONTEXT->config_mutex) == 0) {
		pthread_mutex_unlock(&DEVICE_CONTEXT->config_mutex);
		pthread_mutex_lock(&INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->mutex);
		indigo_save_property(device, NULL, AGENT_PLATESOLVER_USE_INDEX_PROPERTY);
		indigo_save_property(device, NULL, AGENT_PLATESOLVER_HINTS_PROPERTY);
		indigo_save_property(device, NULL, AGENT_PLATESOLVER_SYNC_PROPERTY);
		indigo_save_property(device, NULL, AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY);
		if (DEVICE_CONTEXT->property_save_file_handle) {
			CONFIG_PROPERTY->state = INDIGO_OK_STATE;
			close(DEVICE_CONTEXT->property_save_file_handle);
			DEVICE_CONTEXT->property_save_file_handle = 0;
		} else {
			CONFIG_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		CONFIG_SAVE_ITEM->sw.value = false;
		indigo_update_property(device, CONFIG_PROPERTY, NULL);
		pthread_mutex_unlock(&INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->mutex);
	}
}

static void set_pa_reference(indigo_device *device) {
	double lst_now = indigo_lst(NULL, INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->geo_coordinates.a * RAD2DEG);
	indigo_ra_dec_to_point(
		AGENT_PLATESOLVER_WCS_RA_ITEM->number.value,
		AGENT_PLATESOLVER_WCS_DEC_ITEM->number.value,
		lst_now,
		&INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->pa_reference
	);
	indigo_log("%s(): Polar align: Reference LST = %f, Lon = %f, Lat = %f", __FUNCTION__, lst_now,  INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->geo_coordinates.a * RAD2DEG, INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->geo_coordinates.d * RAD2DEG);
	indigo_log("%s(): Polar align: Reference HA = %f, Dec = %f", __FUNCTION__, INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->pa_reference.a * RAD2DEG / 15, INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->pa_reference.d * RAD2DEG);
}

void indigo_platesolver_sync(indigo_device *device) {
	const int QUARTER_SECOND_DELAY = ONE_SECOND_DELAY / 4;
	for (int i = 0; i < FILTER_RELATED_AGENT_LIST_PROPERTY->count; i++) {
		indigo_item *item = FILTER_RELATED_AGENT_LIST_PROPERTY->items + i;
		if (item->sw.value && !strncmp(item->name, "Mount Agent", 11)) {
			const char * fov_names[] = { AGENT_MOUNT_FOV_ANGLE_ITEM_NAME, AGENT_MOUNT_FOV_WIDTH_ITEM_NAME, AGENT_MOUNT_FOV_HEIGHT_ITEM_NAME };
			double fov_values[] = { AGENT_PLATESOLVER_WCS_ANGLE_ITEM->number.value, AGENT_PLATESOLVER_WCS_WIDTH_ITEM->number.value, AGENT_PLATESOLVER_WCS_HEIGHT_ITEM->number.value };
			const char * eq_coordinates_names[] = { MOUNT_EQUATORIAL_COORDINATES_RA_ITEM_NAME, MOUNT_EQUATORIAL_COORDINATES_DEC_ITEM_NAME };
			indigo_change_number_property(FILTER_DEVICE_CONTEXT->client, item->name, AGENT_MOUNT_FOV_PROPERTY_NAME, 3, fov_names, fov_values);
			if (
				AGENT_PLATESOLVER_SYNC_SYNC_ITEM->sw.value ||
				AGENT_PLATESOLVER_SYNC_CENTER_ITEM->sw.value ||
				AGENT_PLATESOLVER_SYNC_SET_PA_REFERENCE_AND_MOVE_ITEM->sw.value
			) {
				set_pa_reference(device);
				indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, item->name, MOUNT_ON_COORDINATES_SET_PROPERTY_NAME, MOUNT_ON_COORDINATES_SET_SYNC_ITEM_NAME, true);
				indigo_usleep(QUARTER_SECOND_DELAY);
				double sync_values[] = { AGENT_PLATESOLVER_WCS_RA_ITEM->number.value, AGENT_PLATESOLVER_WCS_DEC_ITEM->number.value };
				indigo_change_number_property(FILTER_DEVICE_CONTEXT->client, item->name, MOUNT_EQUATORIAL_COORDINATES_PROPERTY_NAME, 2, eq_coordinates_names, sync_values);
			}
			if (AGENT_PLATESOLVER_SYNC_CENTER_ITEM->sw.value) {
				/* Some mounts are slow to SYNC and ignore GOTO, this gives them some time to finish syncing */
				indigo_usleep(1.5 * ONE_SECOND_DELAY);
				indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, item->name, MOUNT_ON_COORDINATES_SET_PROPERTY_NAME, MOUNT_ON_COORDINATES_SET_TRACK_ITEM_NAME, true);
				indigo_usleep(QUARTER_SECOND_DELAY);
				double slew_values[] = { AGENT_PLATESOLVER_HINTS_RA_ITEM->number.value, AGENT_PLATESOLVER_HINTS_DEC_ITEM->number.value };
				indigo_change_number_property(FILTER_DEVICE_CONTEXT->client, item->name, MOUNT_EQUATORIAL_COORDINATES_PROPERTY_NAME, 2, eq_coordinates_names, slew_values);
			}
			if (AGENT_PLATESOLVER_SYNC_SET_PA_REFERENCE_AND_MOVE_ITEM->sw.value) {
				/* Some mounts are slow to SYNC and ignore GOTO, this gives them some time to finish syncing */
				indigo_usleep(1.5 * ONE_SECOND_DELAY);
				indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, item->name, MOUNT_ON_COORDINATES_SET_PROPERTY_NAME, MOUNT_ON_COORDINATES_SET_TRACK_ITEM_NAME, true);
				indigo_usleep(QUARTER_SECOND_DELAY);
				double slew_values[] = {
					/* subtract HA move from RA is correct */
					AGENT_PLATESOLVER_HINTS_RA_ITEM->number.value - AGENT_PLATESOLVER_PA_SETTINGS_HA_MOVE_ITEM->number.value / 15,
					AGENT_PLATESOLVER_HINTS_DEC_ITEM->number.value + AGENT_PLATESOLVER_PA_SETTINGS_DEC_MOVE_ITEM->number.value
				};
				indigo_change_number_property(FILTER_DEVICE_CONTEXT->client, item->name, MOUNT_EQUATORIAL_COORDINATES_PROPERTY_NAME, 2, eq_coordinates_names, slew_values);
			}
			if (AGENT_PLATESOLVER_SYNC_CALCULATE_PA_ERROR_ITEM->sw.value) {
				char *message = NULL;
				if (INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->pa_reference.r == 0) {
					AGENT_PLATESOLVER_PA_ERROR_PROPERTY->state = INDIGO_ALERT_STATE;
					AGENT_PLATESOLVER_PA_ERROR_AZ_ITEM->number.value =
					AGENT_PLATESOLVER_PA_ERROR_ALT_ITEM->number.value =
					AGENT_PLATESOLVER_PA_ERROR_HA_ITEM->number.value =
					AGENT_PLATESOLVER_PA_ERROR_DEC_ITEM->number.value =
					AGENT_PLATESOLVER_PA_ERROR_ITEM->number.value = 0;
					message = "The initial position for polar error calculation is not specified";
				} else {
					indigo_spherical_point_t equatorial_error = {0}, horizontal_error = {0};
					indigo_spherical_point_t position, position_observed;
					double lst_now = indigo_lst(NULL, INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->geo_coordinates.a * RAD2DEG);

					indigo_ra_dec_to_point(
						AGENT_PLATESOLVER_WCS_RA_ITEM->number.value,
						AGENT_PLATESOLVER_WCS_DEC_ITEM->number.value,
						lst_now,
						&position
					);

					indigo_ra_dec_to_point(
						INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->eq_coordinates.a * RAD2DEG / 15,
						INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->eq_coordinates.d * RAD2DEG,
						lst_now,
						&position_observed
					);

					/* // inject test data
					indigo_spherical_point_t star1 = { 3 * 15 * DEG2RAD, 48 * DEG2RAD, 1 };
					indigo_spherical_point_t star2 = { 23 * 15 * DEG2RAD, 45 * DEG2RAD, 1 };
					indigo_spherical_point_t star2_observed = {
						star2.a + (-12 / 60.0 * DEG2RAD),
						star2.d + (-21 / 60.0 * DEG2RAD),
						1
					};
					position_observed = star2_observed;
					position = star2;
					INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->pa_reference = star1;
					INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->geo_coordinates.d = (42.0 + 40 / 60.0) * DEG2RAD;
					*/

					indigo_log("%s(): Polar align: LST = %f, Telescope RA = %f, Dec = %f", __FUNCTION__, lst_now, INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->eq_coordinates.a * RAD2DEG / 15, INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->eq_coordinates.d * RAD2DEG);
					indigo_log("%s(): Polar align: Solved HA = %f,  Dec = %f", __FUNCTION__, position.a * RAD2DEG / 15, position.d * RAD2DEG);
					indigo_log("%s(): Polar align: Telescope HA = %f, Dec = %f", __FUNCTION__, position_observed.a * RAD2DEG / 15, position_observed.d * RAD2DEG);

					bool success = indigo_polar_alignment_error(
						&INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->pa_reference,
						&position,
						&position_observed,
						INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->geo_coordinates.d,
						&equatorial_error,
						&horizontal_error
					);
					if (success) {
						AGENT_PLATESOLVER_PA_ERROR_PROPERTY->state = INDIGO_OK_STATE;
					} else {
						AGENT_PLATESOLVER_PA_ERROR_PROPERTY->state = INDIGO_ALERT_STATE;
						message = "No solution for polar error is found";
					}
					AGENT_PLATESOLVER_PA_ERROR_AZ_ITEM->number.value = horizontal_error.a * RAD2DEG;
					AGENT_PLATESOLVER_PA_ERROR_ALT_ITEM->number.value = horizontal_error.d * RAD2DEG;
					AGENT_PLATESOLVER_PA_ERROR_HA_ITEM->number.value = equatorial_error.a * RAD2DEG;
					AGENT_PLATESOLVER_PA_ERROR_DEC_ITEM->number.value = equatorial_error.d * RAD2DEG;
					/* since the sperical error vectors are perpendicular and parrallel to Az and Alt the length can be simplified as follows */
					AGENT_PLATESOLVER_PA_ERROR_ITEM->number.value = sqrt(horizontal_error.a * horizontal_error.a + horizontal_error.d * horizontal_error.d) * RAD2DEG;
					double delta_az = horizontal_error.a * RAD2DEG * 60;
					double delta_alt = horizontal_error.d * RAD2DEG * 60;
					double total_delta = AGENT_PLATESOLVER_PA_ERROR_ITEM->number.value * 60;
					indigo_log("%s(): Polar align: Error deltaHA = %f', deltaDec = %f'", __FUNCTION__, equatorial_error.a * RAD2DEG * 60, equatorial_error.d * RAD2DEG * 60);
					indigo_log("%s(): Polar align: Error deltaAz = %f', deltaAlt = %f'", __FUNCTION__, delta_az, delta_alt);
					indigo_log("%s(): Polar align: Total Error = %f'", __FUNCTION__, total_delta);
					indigo_send_message(device, "Calculated polar error: %.2f'", total_delta);
					indigo_send_message(device, "Altitude correction: %+.2f', move %s (use altitude adjustment knob)", delta_alt, (delta_alt > 0) ? "Up" : "Down");
					indigo_send_message(device, "Azimuth correction: %+.2f', move %s (use azimuth adjustment knob)" , delta_az, (delta_az > 0) ? "C.W." : "C.C.W.");
				}
				indigo_update_property(device, AGENT_PLATESOLVER_PA_ERROR_PROPERTY, message);
			}
			break;
		}
	}
}

indigo_result indigo_platesolver_device_attach(indigo_device *device, const char* driver_name, unsigned version, indigo_device_interface device_interface) {
	assert(device != NULL);
	if (indigo_filter_device_attach(device, driver_name, version, device_interface) == INDIGO_OK) {
		// -------------------------------------------------------------------------------- FILTER_RELATED_AGENT_LIST
		FILTER_RELATED_AGENT_LIST_PROPERTY->hidden = false;
		FILTER_DEVICE_CONTEXT->validate_related_agent = validate_related_agent;
		// -------------------------------------------------------------------------------- AGENT_PLATESOLVER_USE_INDEX
		AGENT_PLATESOLVER_USE_INDEX_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_PLATESOLVER_USE_INDEX_PROPERTY_NAME, PLATESOLVER_MAIN_GROUP, "Use indexes", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE, 33);
		if (AGENT_PLATESOLVER_USE_INDEX_PROPERTY == NULL)
			return INDIGO_FAILED;
		AGENT_PLATESOLVER_USE_INDEX_PROPERTY->count = 0;
		// -------------------------------------------------------------------------------- Hints property
		AGENT_PLATESOLVER_HINTS_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_PLATESOLVER_HINTS_PROPERTY_NAME, PLATESOLVER_MAIN_GROUP, "Hints", INDIGO_OK_STATE, INDIGO_RW_PERM, 7);
		if (AGENT_PLATESOLVER_HINTS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_PLATESOLVER_HINTS_RADIUS_ITEM, AGENT_PLATESOLVER_HINTS_RADIUS_ITEM_NAME, "Search radius (°)", 0, 360, 2, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_HINTS_RA_ITEM, AGENT_PLATESOLVER_HINTS_RA_ITEM_NAME, "RA (hours)", 0, 24, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_HINTS_DEC_ITEM, AGENT_PLATESOLVER_HINTS_DEC_ITEM_NAME, "Dec (°)", -90, 90, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_HINTS_PARITY_ITEM, AGENT_PLATESOLVER_HINTS_PARITY_ITEM_NAME, "Parity (-1,0,1)", -1, 1, 1, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_HINTS_DOWNSAMPLE_ITEM, AGENT_PLATESOLVER_HINTS_DOWNSAMPLE_ITEM_NAME, "Downsample", 1, 16, 1, 2);
		indigo_init_number_item(AGENT_PLATESOLVER_HINTS_DEPTH_ITEM, AGENT_PLATESOLVER_HINTS_DEPTH_ITEM_NAME, "Depth", 0, 1000, 5, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_HINTS_CPU_LIMIT_ITEM, AGENT_PLATESOLVER_HINTS_CPU_LIMIT_ITEM_NAME, "CPU Limit (seconds)", 0, 600, 10, 180);
		strcpy(AGENT_PLATESOLVER_HINTS_RADIUS_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_HINTS_RA_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_HINTS_DEC_ITEM->number.format, "%m");
		// -------------------------------------------------------------------------------- WCS property
		AGENT_PLATESOLVER_WCS_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_PLATESOLVER_WCS_PROPERTY_NAME, PLATESOLVER_MAIN_GROUP, "WCS solution", INDIGO_OK_STATE, INDIGO_RO_PERM, 8);
		if (AGENT_PLATESOLVER_WCS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_RA_ITEM, AGENT_PLATESOLVER_WCS_RA_ITEM_NAME, "Frame center RA (hours)", 0, 24, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_DEC_ITEM, AGENT_PLATESOLVER_WCS_DEC_ITEM_NAME, "Frame center Dec (°)", 0, 360, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_ANGLE_ITEM, AGENT_PLATESOLVER_WCS_ANGLE_ITEM_NAME, "Rotation angle (° E of N)", 0, 360, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_WIDTH_ITEM, AGENT_PLATESOLVER_WCS_WIDTH_ITEM_NAME, "Frame width (°)", 0, 360, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_HEIGHT_ITEM, AGENT_PLATESOLVER_WCS_HEIGHT_ITEM_NAME, "Frame height (°)", 0, 360, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_SCALE_ITEM, AGENT_PLATESOLVER_WCS_SCALE_ITEM_NAME, "Pixel scale (°/pixel)", 0, 1000, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_PARITY_ITEM, AGENT_PLATESOLVER_WCS_PARITY_ITEM_NAME, "Parity (-1,1)", -1, 1, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_WCS_INDEX_ITEM, AGENT_PLATESOLVER_WCS_INDEX_ITEM_NAME, "Used index file", 0, 10000, 0, 0);
		strcpy(AGENT_PLATESOLVER_WCS_RA_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_WCS_DEC_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_WCS_ANGLE_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_WCS_WIDTH_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_WCS_HEIGHT_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_WCS_SCALE_ITEM->number.format, "%m");
		// -------------------------------------------------------------------------------- SYNC property
		AGENT_PLATESOLVER_SYNC_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_PLATESOLVER_SYNC_PROPERTY_NAME, PLATESOLVER_MAIN_GROUP, "Sync mode", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 5);
		if (AGENT_PLATESOLVER_SYNC_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_PLATESOLVER_SYNC_DISABLED_ITEM, AGENT_PLATESOLVER_SYNC_DISABLED_ITEM_NAME, "Disabled", true);
		indigo_init_switch_item(AGENT_PLATESOLVER_SYNC_SYNC_ITEM, AGENT_PLATESOLVER_SYNC_SYNC_ITEM_NAME, "Sync only", false);
		indigo_init_switch_item(AGENT_PLATESOLVER_SYNC_CENTER_ITEM, AGENT_PLATESOLVER_SYNC_CENTER_ITEM_NAME, "Sync and center", false);
		indigo_init_switch_item(AGENT_PLATESOLVER_SYNC_SET_PA_REFERENCE_AND_MOVE_ITEM, AGENT_PLATESOLVER_SYNC_SET_PA_REFERENCE_AND_MOVE_ITEM_NAME, "Set polar alignment reference and move", false);
		indigo_init_switch_item(AGENT_PLATESOLVER_SYNC_CALCULATE_PA_ERROR_ITEM, AGENT_PLATESOLVER_SYNC_CALCULATE_PA_ERROR_ITEM_NAME, "Calclulate polar alignment error", false);
		// -------------------------------------------------------------------------------- POLAR_ALIGNMENT_SETTINGS property
		AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY_NAME, PLATESOLVER_MAIN_GROUP, "Polar alignment settings", INDIGO_OK_STATE, INDIGO_RW_PERM, 2);
		if (AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_PLATESOLVER_PA_SETTINGS_HA_MOVE_ITEM, AGENT_PLATESOLVER_PA_SETTINGS_HA_MOVE_ITEM_NAME, "Hour angle move (°)", -180, 180, 5, 30);
		indigo_init_number_item(AGENT_PLATESOLVER_PA_SETTINGS_DEC_MOVE_ITEM, AGENT_PLATESOLVER_PA_SETTINGS_DEC_MOVE_ITEM_NAME, "Declination move (°)", -45, 45, 1, 0);
		strcpy(AGENT_PLATESOLVER_PA_SETTINGS_HA_MOVE_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_PA_SETTINGS_DEC_MOVE_ITEM->number.format, "%m");
		// -------------------------------------------------------------------------------- POLAR_ALIGNMENT_ERROR property
		AGENT_PLATESOLVER_PA_ERROR_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_PLATESOLVER_PA_ERROR_PROPERTY_NAME, PLATESOLVER_MAIN_GROUP, "Polar alignment error", INDIGO_OK_STATE, INDIGO_RO_PERM, 5);
		if (AGENT_PLATESOLVER_PA_ERROR_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_PLATESOLVER_PA_ERROR_HA_ITEM, AGENT_PLATESOLVER_PA_ERROR_HA_ITEM_NAME, "Hour angle error (°)", -45, 45, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_PA_ERROR_DEC_ITEM, AGENT_PLATESOLVER_PA_ERROR_DEC_ITEM_NAME, "Declination error (°)", -45, 45, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_PA_ERROR_ALT_ITEM, AGENT_PLATESOLVER_PA_ERROR_ALT_ITEM_NAME, "Altitude error (°)", -45, 45, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_PA_ERROR_AZ_ITEM, AGENT_PLATESOLVER_PA_ERROR_AZ_ITEM_NAME, "Azimuth error (°)", -45, 45, 0, 0);
		indigo_init_number_item(AGENT_PLATESOLVER_PA_ERROR_ITEM, AGENT_PLATESOLVER_PA_ERROR_ITEM_NAME, "Total error (°)", -45, 45, 0, 0);
		strcpy(AGENT_PLATESOLVER_PA_ERROR_HA_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_PA_ERROR_DEC_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_PA_ERROR_ALT_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_PA_ERROR_AZ_ITEM->number.format, "%m");
		strcpy(AGENT_PLATESOLVER_PA_ERROR_ITEM->number.format, "%m");
		// -------------------------------------------------------------------------------- ABORT property
		AGENT_PLATESOLVER_ABORT_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_PLATESOLVER_ABORT_PROPERTY_NAME, PLATESOLVER_MAIN_GROUP, "Abort", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE, 1);
		if (AGENT_PLATESOLVER_ABORT_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_PLATESOLVER_ABORT_ITEM, AGENT_PLATESOLVER_ABORT_ITEM_NAME, "Abort", false);
		// --------------------------------------------------------------------------------
		CONFIG_PROPERTY->hidden = true;
		PROFILE_PROPERTY->hidden = true;
		CONNECTION_PROPERTY->hidden = true;
		// --------------------------------------------------------------------------------
		INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->eq_coordinates_state = INDIGO_IDLE_STATE;
		INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->eq_coordinates_timestamp = 0;

		pthread_mutex_init(&INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->mutex, NULL);
		return INDIGO_OK;
	}
	return INDIGO_FAILED;
}

indigo_result indigo_platesolver_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property) {
	if (indigo_property_match(AGENT_PLATESOLVER_USE_INDEX_PROPERTY, property))
		indigo_define_property(device, AGENT_PLATESOLVER_USE_INDEX_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PLATESOLVER_HINTS_PROPERTY, property))
		indigo_define_property(device, AGENT_PLATESOLVER_HINTS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PLATESOLVER_WCS_PROPERTY, property))
		indigo_define_property(device, AGENT_PLATESOLVER_WCS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PLATESOLVER_SYNC_PROPERTY, property))
		indigo_define_property(device, AGENT_PLATESOLVER_SYNC_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY, property))
		indigo_define_property(device, AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PLATESOLVER_PA_ERROR_PROPERTY, property))
		indigo_define_property(device, AGENT_PLATESOLVER_PA_ERROR_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PLATESOLVER_ABORT_PROPERTY, property))
		indigo_define_property(device, AGENT_PLATESOLVER_ABORT_PROPERTY, NULL);
	return indigo_filter_enumerate_properties(device, client, property);
}

indigo_result indigo_platesolver_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);
	if (client == FILTER_DEVICE_CONTEXT->client)
		return INDIGO_OK;
	if (indigo_property_match(AGENT_PLATESOLVER_USE_INDEX_PROPERTY, property)) {
	// -------------------------------------------------------------------------------- AGENT_PLATESOLVER_USE_INDEX
		indigo_property_copy_values(AGENT_PLATESOLVER_USE_INDEX_PROPERTY, property, false);
		AGENT_PLATESOLVER_USE_INDEX_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_PLATESOLVER_USE_INDEX_PROPERTY, NULL);
		INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->save_config(device);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_PLATESOLVER_HINTS_PROPERTY, property)) {
	// -------------------------------------------------------------------------------- AGENT_PLATESOLVER_HINTS
		indigo_property_copy_values(AGENT_PLATESOLVER_HINTS_PROPERTY, property, false);
		AGENT_PLATESOLVER_HINTS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_PLATESOLVER_HINTS_PROPERTY, NULL);
		INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->save_config(device);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY, property)) {
	// -------------------------------------------------------------------------------- AGENT_PLATESOLVER_PA_SETTINGS
		indigo_property_copy_values(AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY, property, false);
		AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY, NULL);
		INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->save_config(device);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_PLATESOLVER_SYNC_PROPERTY, property)) {
	// -------------------------------------------------------------------------------- AGENT_PLATESOLVER_SYNC
		indigo_property_copy_values(AGENT_PLATESOLVER_SYNC_PROPERTY, property, false);
		AGENT_PLATESOLVER_SYNC_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_PLATESOLVER_SYNC_PROPERTY, NULL);
		INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->save_config(device);
		return INDIGO_OK;
	}
	return indigo_filter_change_property(device, client, property);
}

indigo_result indigo_platesolver_device_detach(indigo_device *device) {
	assert(device != NULL);
	indigo_release_property(AGENT_PLATESOLVER_USE_INDEX_PROPERTY);
	indigo_release_property(AGENT_PLATESOLVER_HINTS_PROPERTY);
	indigo_release_property(AGENT_PLATESOLVER_WCS_PROPERTY);
	indigo_release_property(AGENT_PLATESOLVER_SYNC_PROPERTY);
	indigo_release_property(AGENT_PLATESOLVER_PA_SETTINGS_PROPERTY);
	indigo_release_property(AGENT_PLATESOLVER_PA_ERROR_PROPERTY);
	indigo_release_property(AGENT_PLATESOLVER_ABORT_PROPERTY);
	pthread_mutex_destroy(&INDIGO_PLATESOLVER_DEVICE_PRIVATE_DATA->mutex);
	return indigo_filter_device_detach(device);
}

// -------------------------------------------------------------------------------- INDIGO agent client implementation

static void indigo_platesolver_handle_property(indigo_client *client, indigo_device *device, indigo_property *property, const char *message) {
	{ char *device_name = property->device;
		indigo_device *device = FILTER_CLIENT_CONTEXT->device;
		if (!strcmp(property->name, MOUNT_EQUATORIAL_COORDINATES_PROPERTY_NAME)) {
			indigo_property *agents = FILTER_CLIENT_CONTEXT->filter_related_agent_list_property;
			for (int j = 0; j < agents->count; j++) {
				indigo_item *item = agents->items + j;
				if (item->sw.value && !strcmp(item->name, device_name)) {
					bool update = false;
					INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->eq_coordinates_state = property->state;
					INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->eq_coordinates_timestamp = time(NULL);
					for (int i = 0; i < property->count; i++) {
						indigo_item *item = property->items + i;
						if (!strcmp(item->name, MOUNT_EQUATORIAL_COORDINATES_RA_ITEM_NAME)) {
							double value = item->number.value;
							if (AGENT_PLATESOLVER_HINTS_RA_ITEM->number.value != value) {
								AGENT_PLATESOLVER_HINTS_RA_ITEM->number.value = AGENT_PLATESOLVER_HINTS_RA_ITEM->number.target = value;
								INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->eq_coordinates.a = 15 * DEG2RAD * value;
								update = property->state == INDIGO_OK_STATE;
							}
						} else if (!strcmp(item->name, MOUNT_EQUATORIAL_COORDINATES_DEC_ITEM_NAME)) {
							double value = item->number.value;
							if (AGENT_PLATESOLVER_HINTS_DEC_ITEM->number.value != value) {
								AGENT_PLATESOLVER_HINTS_DEC_ITEM->number.value = AGENT_PLATESOLVER_HINTS_DEC_ITEM->number.target = value;
								INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->eq_coordinates.d = DEG2RAD * value;
								update = property->state == INDIGO_OK_STATE;
							}
						}
					}
					if (update) {
						AGENT_PLATESOLVER_HINTS_PROPERTY->state = INDIGO_OK_STATE;
						indigo_update_property(device, AGENT_PLATESOLVER_HINTS_PROPERTY, NULL);
					}
					break;
				}
			}
		} else if (!strcmp(property->name, MOUNT_GEOGRAPHIC_COORDINATES_PROPERTY_NAME)) {
			indigo_property *agents = FILTER_CLIENT_CONTEXT->filter_related_agent_list_property;
			for (int j = 0; j < agents->count; j++) {
				indigo_item *item = agents->items + j;
				if (item->sw.value && !strcmp(item->name, device_name)) {
					double lat = 0, lon = 0;
					bool update = false;
					INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->geo_coordinates_state = property->state;
					INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->geo_coordinates_timestamp = time(NULL);
					INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->geo_coordinates.r = 1;
					for (int i = 0; i < property->count; i++) {
						indigo_item *item = property->items + i;
						if (!strcmp(item->name, MOUNT_GEOGRAPHIC_COORDINATES_LATITUDE_ITEM_NAME)) {
							INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->geo_coordinates.d = DEG2RAD * item->number.value;
						} else if (!strcmp(item->name, MOUNT_GEOGRAPHIC_COORDINATES_LONGITUDE_ITEM_NAME)) {
							INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->geo_coordinates.a = DEG2RAD * item->number.value;
						}
					}
					break;
				}
			}
		}
	}
}

indigo_result indigo_platesolver_update_property(indigo_client *client, indigo_device *device, indigo_property *property, const char *message) {
	{ char *device_name = property->device;
		indigo_device *device = FILTER_CLIENT_CONTEXT->device;
		if (property->state == INDIGO_OK_STATE && !strcmp(property->name, CCD_IMAGE_PROPERTY_NAME)) {
			indigo_property *related_agents = FILTER_CLIENT_CONTEXT->filter_related_agent_list_property;
			for (int j = 0; j < related_agents->count; j++) {
				indigo_item *item = related_agents->items + j;
				if (item->sw.value && !strcmp(item->name, device_name)) {
					for (int i = 0; i < property->count; i++) {
						indigo_item *item = property->items + i;
						if (!strcmp(item->name, CCD_IMAGE_ITEM_NAME)) {
							indigo_platesolver_task *task = indigo_safe_malloc(sizeof(indigo_platesolver_task));
							task->device = FILTER_CLIENT_CONTEXT->device;
							task->image = indigo_safe_malloc_copy(task->size = item->blob.size, item->blob.value);
							indigo_async((void *(*)(void *))INDIGO_PLATESOLVER_CLIENT_PRIVATE_DATA->solve, task);
						}
					}
					break;
				}
			}
		}
	}
	indigo_platesolver_handle_property(client, device, property, message);
	return indigo_filter_update_property(client, device, property, message);
}

indigo_result indigo_platesolver_define_property(indigo_client *client, indigo_device *device, indigo_property *property, const char *message) {
	indigo_platesolver_handle_property(client, device, property, message);
	return indigo_filter_define_property(client, device, property, message);
}
