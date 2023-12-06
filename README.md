# Arduino-offline-timezone-finder
Find time zone offset with just gps coordinates, without internet connection.

This is a port of [ESP32-Timezone-Finder-Component](https://github.com/HarryVienna/ESP32-Timezone-Finder-Component) with database from [ESP32-Timezone-Database-Generator](https://github.com/HarryVienna/ESP32-Timezone-Database-Generator)

Tested with ESP32-S3 powered esp32-8048s070 display module. 

# Usage

1. Upload timezone_database.bin to SD card
2. Upload sketch using Arduino IDE
3. See the result with Serial Monitor

# Making use of

Insert your own coordinates here:
```
char *timezone = tzdb->find_timezone(tzdb, 31.774373690865584, 35.22144645447546);
```




