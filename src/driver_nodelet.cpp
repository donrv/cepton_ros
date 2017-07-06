#include "driver_nodelet.hpp"

#include <cstdint>

#include <pluginlib/class_list_macros.h>

PLUGINLIB_EXPORT_CLASS(cepton_ros::DriverNodelet, nodelet::Nodelet);

namespace {

std::string get_sensor_name(const CeptonSensorInformation &sensor_information) {
  return std::to_string(sensor_information.serial_number);
}
}  // namespace

namespace cepton_ros {

DriverNodelet::~DriverNodelet() {
  auto &driver = cepton_ros::Driver::get_instance();
  driver.deinitialize();
}

void DriverNodelet::onInit() {
  this->node_handle = getNodeHandle();
  this->private_node_handle = getPrivateNodeHandle();

  // Get parameters
  std::string capture_path = "";
  private_node_handle.param("capture_path", capture_path, capture_path);
  private_node_handle.param("combine_sensors", combine_sensors,
                            combine_sensors);
  private_node_handle.param("output_namespace", output_namespace,
                            output_namespace);

  const std::string sensor_information_topic_id =
      output_namespace + "_sensor_information";
  sensor_information_publisher =
      node_handle.advertise<SensorInformation>(sensor_information_topic_id, 2);

  if (combine_sensors) {
    combined_points_publisher = node_handle.advertise<sensor_msgs::PointCloud2>(
        get_sensor_points_topic_id(""), 2);
  }

  int error_code;

  // Initialize driver
  auto on_receive_callback =
      [this](int error_code, CeptonSensorHandle sensor_handle,
             std::size_t n_points, CeptonSensorPoint const *points) {
        return on_receive(error_code, sensor_handle, n_points, points);
      };
  auto on_event_callback =
      [this](int error_code, CeptonSensorHandle sensor_handle,
             CeptonSensorInformation const *sensor_information_ptr,
             int sensor_event) {
        return on_event(error_code, sensor_handle, sensor_information_ptr,
                        sensor_event);
      };
  auto &driver = cepton_ros::Driver::get_instance();
  if (!driver.initialize(on_receive_callback, on_event_callback)) {
    return;
  }

  // Start capture replay
  if (!capture_path.empty()) {
    error_code = cepton_sdk_capture_replay_open(capture_path.c_str());
    if (error_code != CEPTON_SUCCESS) {
      NODELET_FATAL("opening capture replay failed: %s!", error_code);
      return;
    }
    error_code = cepton_sdk_capture_replay_resume();
    if (error_code != CEPTON_SUCCESS) {
      NODELET_FATAL("resuming capture replay failed: %s!", error_code);
      return;
    }
  }
}

std::string DriverNodelet::get_sensor_points_topic_id(
    const std::string &sensor_name) const {
  if (combine_sensors) {
    return (output_namespace + "_points");
  } else {
    return (output_namespace + "_points_" + sensor_name);
  }
}

std::string DriverNodelet::get_sensor_frame_id(
    const std::string &sensor_name) const {
  if (combine_sensors) {
    return output_namespace;
  } else {
    return (output_namespace + "_" + sensor_name);
  }
}

ros::Publisher &DriverNodelet::get_sensor_points_publisher(
    const std::string &sensor_name) {
  if (combine_sensors) {
    return combined_points_publisher;
  } else {
    if (!sensor_points_publishers.count(sensor_name)) {
      sensor_points_publishers[sensor_name] =
          node_handle.advertise<sensor_msgs::PointCloud2>(
              get_sensor_points_topic_id(sensor_name), 10);
    }
    return sensor_points_publishers.at(sensor_name);
  }
}

void DriverNodelet::publish_sensor_information(
    const CeptonSensorInformation &sensor_information) {
  cepton_ros::SensorInformation msg;
  msg.handle = sensor_information.handle;
  msg.serial_number = sensor_information.serial_number;
  msg.model_name = sensor_information.model_name;
  msg.firmware_version = sensor_information.firmware_version;
  sensor_information_publisher.publish(msg);
}

void DriverNodelet::on_receive(int error_code, CeptonSensorHandle sensor_handle,
                               std::size_t n_points,
                               CeptonSensorPoint const *points) {
  if (error_code < 0) {
    NODELET_WARN("on_receive failed: %i", error_code);
  }

  CeptonSensorInformation const *sensor_information_ptr =
      cepton_sdk_get_sensor_information(sensor_handle);
  std::string sensor_name = get_sensor_name(*sensor_information_ptr);
  publish_sensor_information(*sensor_information_ptr);

  // Convert to point cloud
  uint64_t message_timestamp = 0;
  for (std::size_t i = 0; i < n_points; ++i) {
    message_timestamp = std::max(message_timestamp, points[i].timestamp);
  }
  CeptonPointCloud::Ptr point_cloud_ptr(new CeptonPointCloud());
  point_cloud_ptr->reserve(n_points);
  point_cloud_ptr->header.stamp = message_timestamp;
  point_cloud_ptr->header.frame_id = get_sensor_frame_id(sensor_name);
  point_cloud_ptr->height = 1;

  for (std::size_t i_point = 0; i_point < n_points; ++i_point) {
    const CeptonSensorPoint &cepton_point = points[i_point];
    CeptonPoint pcl_point;
    pcl_point.timestamp = cepton_point.timestamp;
    pcl_point.x = cepton_point.x;
    pcl_point.y = cepton_point.y;
    pcl_point.z = cepton_point.z;
    pcl_point.intensity = cepton_point.intensity;
    point_cloud_ptr->points.push_back(pcl_point);
    ++(point_cloud_ptr->width);
  }

  get_sensor_points_publisher(sensor_name).publish(point_cloud_ptr);
}

void DriverNodelet::on_event(
    int error_code, CeptonSensorHandle sensor_handle,
    CeptonSensorInformation const *sensor_information_ptr, int sensor_event) {
  if (error_code < 0) {
    NODELET_WARN("on_event failed: %i", error_code);
    return;
  }

  std::string sensor_name = get_sensor_name(*sensor_information_ptr);

  switch (sensor_event) {
    case CEPTON_EVENT_ATTACH:
      NODELET_INFO("sensor connected: %s", sensor_name.c_str());
      break;
    case CEPTON_EVENT_DETACH:
      NODELET_INFO("sensor disconnected: %s", sensor_name.c_str());
      break;
    case CEPTON_EVENT_FRAME:
      break;
  }
}
}  // namespace cepton_ros
