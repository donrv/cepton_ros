/*
  Copyright Cepton Technologies Inc. 2017, All rights reserved.

  Cepton Sensor SDK %VERSION_STRING%.
*/
#ifndef CEPTON_SDK_H
#define CEPTON_SDK_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef IS_MSVC
#define EXPORT __declspec(dllexport)
#elif IS_GCC
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: CEPTON_SDK_VERSION value is used to enforce API compatibility
#define CEPTON_SDK_VERSION 10

//------------------------------------------------------------------------------
// Errors
//------------------------------------------------------------------------------
/**
  Error code returned by most sdk functions.

  Most getter functions do not return an error, because they cannot fail.
*/
enum CeptonSensorErrorCode {
  CEPTON_SUCCESS = 0,
  CEPTON_ERROR_GENERIC = -1,
  CEPTON_ERROR_OUT_OF_MEMORY = -2,
  CEPTON_ERROR_SENSOR_NOT_FOUND = -4,
  CEPTON_ERROR_SDK_VERSION_MISMATCH = -5,
  CEPTON_ERROR_COMMUNICATION = -6,  ///< Networking error
  CEPTON_ERROR_TOO_MANY_CALLBACKS = -7,
  CEPTON_ERROR_INVALID_ARGUMENTS = -8,
  CEPTON_ERROR_ALREADY_INITIALIZED = -9,
  CEPTON_ERROR_NOT_INITIALIZED = -10,
  CEPTON_ERROR_INVALID_FILE_TYPE = -11,
  CEPTON_ERROR_FILE_IO = -12,
  CEPTON_ERROR_CORRUPT_FILE = -13,
  CEPTON_ERROR_NOT_OPEN = -14,
  CEPTON_ERROR_EOF = -15,

  CEPTON_FAULT_INTERNAL = -1000,  ///< Internal parameter out of range
  CEPTON_FAULT_EXTREME_TEMPERATURE = -1001,  ///< Reading exceed spec
  CEPTON_FAULT_EXTREME_HUMIDITY = -1002,     ///< Reading exceeds spec
  CEPTON_FAULT_EXTREME_ACCELERATION = -1003,
  CEPTON_FAULT_ABNORMAL_FOV = -1004,
  CEPTON_FAULT_ABNORMAL_FRAME_RATE = -1005,
  CEPTON_FAULT_MOTOR_MALFUNCTION = -1006,
  CEPTON_FAULT_LASER_MALFUNCTION = -1007,
  CEPTON_FAULT_DETECTOR_MALFUNCTION = -1008,
};

/**
  Returns string name of error code.
  Returns empty string if error code is invalid.
*/
EXPORT const char *cepton_get_error_code_name(int error_code);

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

/**
  Sensor identifier.
*/
typedef uint64_t CeptonSensorHandle;
static const CeptonSensorHandle CEPTON_NULL_HANDLE = 0LL;

/**
  Indicates that handle was generated by capture replay.
*/
static const uint64_t CEPTON_SENSOR_HANDLE_FLAG_MOCK = 0x100000000LL;

enum CeptonSensorModel {
  HR80T = 1,
  HR80M = 2,
  HR80W = 3,
  SORA_200 = 4,
  VISTA_860 = 5,
};

struct EXPORT CeptonSensorInformation {
  CeptonSensorHandle handle;
  uint64_t serial_number;
  char model_name[28];
  uint32_t model;  ///< CeptonSensorModel
  char firmware_version[32];

  float last_reported_temperature;  ///< [celsius]
  float last_reported_humidity;     ///< [%]
  float last_reported_age;          ///< [hours]

  // Note: GPS timestamp is GMT time
  uint8_t gps_ts_year;   ///< 0-99 (2017 -> 17)
  uint8_t gps_ts_month;  ///< 1-12
  uint8_t gps_ts_day;    ///< 1-31
  uint8_t gps_ts_hour;   ///< 0-23
  uint8_t gps_ts_min;    ///< 0-59
  uint8_t gps_ts_sec;    ///< 0-59

  uint8_t return_count;
  uint8_t padding;

  // Flags
  uint32_t is_mocked : 1;          ///< Created by capture replay
  uint32_t is_pps_connected : 1;   ///< GPS PPS is available
  uint32_t is_nmea_connected : 1;  ///< GPS NMEA is available
  uint32_t is_calibrated : 1;
};
EXPORT extern const size_t cepton_sensor_information_size;

/**
  Point in image coordinates (focal length = 1).
  To convert to 3d point, refer to `cepton_sdk_util.hpp`.
*/
struct EXPORT CeptonSensorImagePoint {
  uint64_t timestamp;  ///< unix time [microseconds]
  float image_x;       ///< x image coordinate
  float distance;      ///< distance [meters]
  float image_z;       ///< z image coordinate
  float intensity;     ///< 0-1 scaled intensity
  uint8_t return_number;
  uint8_t valid;  ///< 1=valid; 0=clipped/invalid
};
EXPORT extern const size_t cepton_sensor_image_point_size;

//------------------------------------------------------------------------------
// Pre-Initialization SDK Setup
//------------------------------------------------------------------------------
EXPORT size_t cepton_sdk_get_n_ports();

EXPORT void cepton_sdk_get_ports(uint16_t *const ports);

/**
  Network listen ports.

  Default: [8808].

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_set_ports(const uint16_t *const ports, size_t n_ports);

/**
  Rate at which points are returned from sensor.
  Used for rate limiting callbacks for high level languages, such as python.
  If 0, points are returned immediately.

  Default: 0.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_set_frame_length(float frame_length);

EXPORT float cepton_sdk_get_frame_length();

//------------------------------------------------------------------------------
// SDK Setup
//------------------------------------------------------------------------------
enum CeptonSDKControl {
  /**
    Disable networking operations.
    Useful for running multiple instances of sdk in different processes.
    Must pass packets manually to 'cepton_sdk_mock_network_receive'.
  */
  CEPTON_SDK_CONTROL_DISABLE_NETWORK = 1 << 1,
  /**
    Disable marking clipped points as invalid.
    Does not affect number of points returned.
  */
  CEPTON_SDK_CONTROL_DISABLE_IMAGE_CLIP = 1 << 2,
  CEPTON_SDK_CONTROL_DISABLE_DISTANCE_CLIP = 1 << 3,
  /**
    Enable multiple returns.
    When set, return_count in CeptonSensorInformation will indicate the
    number of returns per laser.
    Can only be set at sdk initialization.
  */
  CEPTON_SDK_CONTROL_ENABLE_MULTIPLE_RETURNS = 1 << 4,
};

/**
  Callback for receiving sdk and sensor errors.
  Currently, error data is not set.
*/
typedef void (*FpCeptonSensorErrorCallback)(
    CeptonSensorHandle handle, int error_code, const char *error_msg,
    const void *error_data, size_t error_data_size, void *user_data);

EXPORT int cepton_sdk_is_initialized();

/**
  Sets control flags. Resets sensors. Starts networking.
  Does not modify listeners and other sdk settings.

  Returns error if sdk is already initialized.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_initialize(int ver, uint32_t control_flags,
                                 FpCeptonSensorErrorCallback cb,
                                 void *const user_data);

/**
  Optional. Called automatically on program exit.
  Clears sensors. Stops networking.
  Does not modify listeners and other sdk settings.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_deinitialize();

/**
  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_set_control_flags(uint32_t mask, uint32_t flags);

EXPORT uint32_t cepton_sdk_get_control_flags();

EXPORT int cepton_sdk_has_control_flag(uint32_t flag);

/**
  Reset frame caches. Used when seeking in a capture file.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_clear_cache();

//------------------------------------------------------------------------------
// Points
//------------------------------------------------------------------------------
/**
  Callback for receiving image points.
  Set the frame length to control the callback rate.
*/
typedef void (*FpCeptonSensorImageDataCallback)(
    CeptonSensorHandle handle, size_t n_points,
    const struct CeptonSensorImagePoint *p_points, void *user_data);

/**
  Returns error if callback already set.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_listen_image_frames(FpCeptonSensorImageDataCallback cb,
                                          void *const user_data);
EXPORT int cepton_sdk_unlisten_image_frames();

//------------------------------------------------------------------------------
// Sensors
//------------------------------------------------------------------------------
/**
  Get number of sensors attached. Use to check for new sensors.
  Sensors are not deleted until deinitialization.
*/
EXPORT size_t cepton_sdk_get_n_sensors();

/**
  Returns error if sensor not found.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_get_sensor_handle_by_serial_number(
    uint64_t serial_number, CeptonSensorHandle *const handle);

/**
  Valid indices are in range [0, n_sensors). Returns error if index invalid.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_get_sensor_information_by_index(
    size_t idx, struct CeptonSensorInformation *const info);

/**
  Returns error if sensor not found.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_get_sensor_information(
    CeptonSensorHandle handle, struct CeptonSensorInformation *const info);

//------------------------------------------------------------------------------
// Networking
//------------------------------------------------------------------------------
/**
  Callback for receiving network packets.

  \param handle Unique sensor identifier (e.g. IP address).

  \return Returns error if callback already set.
*/
typedef void (*FpCeptonNetworkReceiveCallback)(uint64_t handle,
                                               uint8_t const *buffer,
                                               size_t buffer_size,
                                               void *user_data);

/**
  Returns error if callback already set.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_listen_network_packet(FpCeptonNetworkReceiveCallback cb,
                                            void *const user_data);

/**
  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_unlisten_network_packet();

/**
  Sets current sdk time used for mocked sensors (unix time [microseconds]).
  If 0, uses pc clock.
  Set automatically by capture replay.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_set_mock_time_base(uint64_t time_base);

/**
  Manually pass packets to sdk. Blocks while processing, and calls listener
  callbacks synchronously before returning.
  Returns error if sdk is not initialized.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_mock_network_receive(uint64_t ipv4_address,
                                           const uint8_t *const buffer,
                                           size_t buffer_size);

//------------------------------------------------------------------------------
// Capture Replay
//------------------------------------------------------------------------------
EXPORT int cepton_sdk_capture_replay_is_open();

/**
  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_open(const char *const path);

/**
  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_close();

/**
  Returns capture start timestamp (unix time [microseconds]).
*/
EXPORT uint64_t cepton_sdk_capture_replay_get_start_time();

/**
  Get capture file position (seconds relative to start of file).
*/
EXPORT float cepton_sdk_capture_replay_get_position();

/**
  Get capture file length in seconds.
*/
EXPORT float cepton_sdk_capture_replay_get_length();

/**
  Returns true if at end of capture file.
  This is only relevant when using resume_blocking methods.
*/
EXPORT int cepton_sdk_capture_replay_is_end();

/**
  Seek to start of capture file.

  Returns error if replay is not open.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_rewind();

/**
  Seek to capture file position.
  Position must be in range [0.0, capture length).

  Returns error if replay is not open.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_seek(float sec);

/**
  If loop enabled, replay will automatically rewind at end.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_set_enable_loop(int enable_loop);

EXPORT int cepton_sdk_capture_replay_get_enable_loop();

/**
  Replay speed multiplier for asynchronous replay.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_set_speed(float speed);

EXPORT float cepton_sdk_capture_replay_get_speed();

/**
  Replay next packet in current thread without sleeping.
  Pauses replay thread if it is running.

  Returns error if replay is not open or sdk is not initialized.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_resume_blocking_once();

/**
  Replay multiple packets in current thread without sleeping between packets.
  Resume duration must be non-negative. Pauses replay thread if it is running.

  Returns error if replay is not open or sdk is not initialized.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_resume_blocking(float sec);

/**
  Returns true if replay thread is resumed.
*/
EXPORT int cepton_sdk_capture_replay_is_running();

/**
  Resume asynchronous replay thread. Packets are replayed in realtime.
  Replay thread sleeps in between packets.

  Returns error if replay is not open or sdk is not initialized.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_resume();

/**
  Pause asynchronous replay thread.

  Returns error if replay is not open or sdk is not initialized.

  \return CeptonSensorErrorCode
*/
EXPORT int cepton_sdk_capture_replay_pause();

#ifdef __cplusplus
}  // extern "C"
#endif

#undef EXPORT

#endif /** CEPTON_SDK_H */
