#include <Arduino.h>
#include <SD.h>

typedef struct {
  int32_t latitude;
  int32_t longitude;
} tz_database_point_t;

typedef struct {
  uint32_t position;
} tz_database_shape_t;

typedef struct {
  int32_t from_latitude;
  int32_t from_longitude;
  int32_t to_latitude;
  int32_t to_longitude;
} tz_database_boundingbox_t;

typedef struct {
  char tz_name[64];
  char tz_value[64];
  uint32_t position;
} tz_database_entry_t;

typedef struct {
  uint8_t version;
  char signature[4];
  uint8_t precision;
  char creation_date[10];
  char filler[16];
} tz_database_header_t;

struct tz_database_s {
  esp_err_t (*init)(struct tz_database_s *tzdb);
  char *(*find_timezone)(struct tz_database_s *tzdb, float latitude, float longitude);

  // Add SD card-specific function pointers
  esp_err_t (*sd_init)(struct tz_database_s *tzdb);
  char *(*sd_find_timezone)(struct tz_database_s *tzdb, float latitude, float longitude);
};

typedef struct tz_database_conf_s {
} tz_database_conf_t;

typedef struct {
  struct tz_database_s parent;
  tz_database_conf_t config;
  File file;  // Add a field for the SD card file
} sd_t;

tz_database_s *tz_database_new_sd(const tz_database_conf_t *config) {
  // Allocate memory for the sd_t structure
  sd_t *sd = (sd_t *)malloc(sizeof(sd_t));

  if (!sd) {
    Serial.println("Failed to allocate memory for sd_t");
    return nullptr;
  }
  // Set function pointers
  sd->parent.init = sd_init;
  sd->parent.find_timezone = sd_find_timezone;

  // Return a pointer to the parent tz_database_s structure
  return &sd->parent;
}

static esp_err_t sd_init(struct tz_database_s *tzdb) {
  sd_t *sd = __containerof(tzdb, sd_t, parent);

  // Initialize SD card
  if (!SD.begin()) {
    Serial.println("Failed to initialize SD card");
    return ESP_FAIL;
  }
  Serial.println("SD Card init OK");
  return ESP_OK;
}

// Add other functions and setup/loop as before...

static int32_t float_to_int(float input, float scale, uint8_t precision);
static float int_to_float(int32_t input, float scale, uint8_t precision);
static bool check_inside_country(const esp_partition_t *partition, u_int32_t data_position, int32_t latitude_int, int32_t longitude_int);
static bool check_inside_shape(const esp_partition_t *partition, u_int32_t data_position, int32_t latitude_int, int32_t longitude_int);
static int32_t next_value(const esp_partition_t *partition, u_int32_t *position);

static char *sd_find_timezone(tz_database_s *tzdb, float latitude, float longitude) {
  sd_t *sd = __containerof(tzdb, sd_t, parent);
  u_int32_t partition_pos = 36;
  File file = SD.open("/timezone_database.bin", FILE_READ);

  if (!file) {
    Serial.println("Error opening timezone database file");
    return "";
  }

  tz_database_header_t header;

  file.read((uint8_t *)&header, sizeof(tz_database_header_t));
  Serial.printf("Timezone Database info: Version: %d   Signature: %.4s   Precision: %d   Creation Date %.10s\n",
                header.version, header.signature, header.precision, header.creation_date);

  int32_t latitude_int = float_to_int(latitude, 90, header.precision);
  int32_t longitude_int = float_to_int(longitude, 180, header.precision);
  Serial.printf("Search Latitude: %f %d\n", latitude, latitude_int);
  Serial.printf("       Longitude: %f %d\n", longitude, longitude_int);

  u_int32_t entries;
  file.read((uint8_t *)&entries, sizeof(u_int32_t));
  Serial.printf("Entries in TOC: %d\n", entries);

  static tz_database_entry_t entry;

  for (int i = 0; i < entries; i++) {
    Serial.print(i);
    file.seek(partition_pos);
    Serial.printf(" file.position %d ", file.position());
    file.read((uint8_t *)&entry, sizeof(tz_database_entry_t));

    partition_pos += sizeof(tz_database_entry_t);

    Serial.printf("Name: %.64s ", entry.tz_name);
    Serial.printf("Value: %.64s ", entry.tz_value);
    Serial.printf("entry.position %d:", entry.position);

    u_int32_t data_position = entry.position;

    bool is_inside = check_inside_country_sd(file, data_position, latitude_int, longitude_int);

    if (is_inside) {
      Serial.printf("Inside timezone: %.64s\n", entry.tz_name);
      return (char *)&entry.tz_value;
    }
  }

  file.close();
  return "";
}
bool check_inside_country_sd(File &file, uint32_t data_position, int32_t latitude_int, int32_t longitude_int) {
  tz_database_boundingbox_t boundingbox;

  // Seek to the data position in the file
  file.seek(data_position);

  // Read the bounding box from the file
  file.read((uint8_t *)&boundingbox, sizeof(tz_database_boundingbox_t));

  // Uncomment if you want to print bounding box values
  Serial.printf("DP: %d ", data_position);
  Serial.printf("from_latitude: %d ", boundingbox.from_latitude);
  Serial.printf("from_longitude: %d ", boundingbox.from_longitude);
  Serial.printf("to_latitude: %d ", boundingbox.to_latitude);
  Serial.printf("to_longitude: %d ", boundingbox.to_longitude);
  Serial.println("");
  if (latitude_int < boundingbox.from_latitude || latitude_int > boundingbox.to_latitude || longitude_int < boundingbox.from_longitude || longitude_int > boundingbox.to_longitude) {
    return false;
  } else {
    // Read the number of shapes from the file
    uint32_t shapes;
    file.read((uint8_t *)&shapes, sizeof(u_int32_t));

    Serial.printf("Inside Bounding Box, Shapes: %d\n", shapes);

    tz_database_shape_t shape;

    for (int j = 0; j < shapes; j++) {
      // Read each shape from the file
      file.read((uint8_t *)&shape, sizeof(tz_database_shape_t));

      // Uncomment if you want to print shape position
      Serial.printf("Shape Position: %d\n", shape.position);

      // Call the check_inside_shape_sd function to check if the point is inside the shape
      bool is_inside = check_inside_shape_sd(file, shape.position, latitude_int, longitude_int);

      if (is_inside) {
        return true;
      }
    }
    return false;
  }
}
// Function to check inside shape for SD card
bool check_inside_shape_sd(File &file, uint32_t shape_position, int32_t latitude_int, int32_t longitude_int) {
  Serial.print(__func__);
  tz_database_point_t start_point;

  // Seek to the shape position in the file
  file.seek(shape_position);

  // Read the start point from the file
  file.read((uint8_t *)&start_point, sizeof(tz_database_point_t));

  // Uncomment if you want to print the start point
  Serial.printf("Start: %d %d\n", start_point.latitude, start_point.longitude);

  u_int32_t deltas;
  file.read((uint8_t *)&deltas, sizeof(u_int32_t));

  // Uncomment if you want to print the number of deltas
  Serial.printf("deltas: %d\n", deltas);

  tz_database_point_t p1;
  tz_database_point_t p2;

  p1.latitude = start_point.latitude;
  p1.longitude = start_point.longitude;

  p2.latitude = start_point.latitude;
  p2.longitude = start_point.longitude;

  double x_inters;
  bool odd = false;

  for (int k = 0; k < deltas; k++) {
    p2.latitude += next_value_sd(file);
    p2.longitude += next_value_sd(file);

    // Uncomment if you want to print the points
    Serial.printf("k P2: %d %f %f\n", k, int_to_float(p2.latitude, 90, 24), int_to_float(p2.longitude, 180, 24));
    Serial.printf("P1: %d %d   P2: %d %d\n", p1.latitude, p1.longitude, p2.latitude, p2.longitude);

    // y must be between the min and max of the line
    if (latitude_int > min(p1.latitude, p2.latitude) && latitude_int <= max(p1.latitude, p2.latitude)) {
      // x must be less than or equal to the larger x value of the line
      if (longitude_int <= max(p1.longitude, p2.longitude)) {
        // Horizontal line is ignored since it is already counted at the endpoint of another line
        if (p1.latitude != p2.latitude) {
          // Solve the linear equation for x https://www.mathematik-oberstufe.de/analysis/lin/gerade2d-2punkte.html
          x_inters = (latitude_int - p1.latitude) * (p2.longitude - p1.longitude) / (p2.latitude - p1.latitude) + p1.longitude;
          if (longitude_int <= x_inters) {
            odd = !odd;
            // Uncomment if you want to print the crossed line
            Serial.printf("Crossed line P1: %f %f   P2: %f %f\n", int_to_float(p1.latitude, 90, 24), int_to_float(p1.longitude, 180, 24), int_to_float(p2.latitude, 90, 24), int_to_float(p2.longitude, 180, 24));
          }
        }
      }
    }
    p1 = p2;
  }

  return odd;
}

// Function to read the next value from SD card file
int32_t next_value_sd(File &file) {
  //Serial.print(__func__);
  int32_t value;
  file.read((uint8_t *)&value, sizeof(int32_t));
  return value;
}
int32_t next_value_sd(File &file, uint32_t &position) {
  //Serial.print(__func__);
  uint8_t marker;
  int32_t value;

  // Read and test if it is the marker. Don't change the position
  file.seek(position);
  file.read((uint8_t *)&marker, sizeof(int8_t));

  if (marker != 0x80 && marker != 0x7F) {  // Not the marker. Read again as an 8-bit value
    file.read((uint8_t *)&value, sizeof(int8_t));
    position += sizeof(int8_t);
    value = (int8_t)value;
  } else if (marker == 0x80) {  // Marker for a 16-bit value
    position += sizeof(int8_t);
    file.read((uint8_t *)&value, sizeof(int16_t));
    position += sizeof(int16_t);
    value = (int16_t)value;
  } else if (marker == 0x7F) {  // Marker for a 32-bit value
    position += sizeof(int8_t);
    file.read((uint8_t *)&value, sizeof(int32_t));
    position += sizeof(int32_t);
    value = (int32_t)value;
  } else {
    Serial.println("Error");
  }

  return value;
}


static int32_t float_to_int(float input, float scale, uint8_t precision) {
  const float inputScaled = input / scale;
  return (int32_t)(inputScaled * (float)(1 << (precision - 1)));
}

static float int_to_float(int32_t input, float scale, uint8_t precision) {
  const float value = (float)input / (float)(1 << (precision - 1));
  return value * scale;
}

void setup() {
  Serial.begin(115200);
  Serial.println("---START---");
  if (!SD.begin()) {
    Serial.println("Card Mount Failed");
    return;
  } else {
    Serial.println("Card Mounted Success");
  }
  static tz_database_conf_t tz_database_conf = {};

  tz_database_s *tzdb = tz_database_new_sd(&tz_database_conf);
  tzdb->init(tzdb);

  char *timezone = tzdb->find_timezone(tzdb, 31.774373690865584, 35.22144645447546);
  Serial.println("Timezone: " + String(timezone));
}

void loop() {
  // put your main code here, to run repeatedly:
}
