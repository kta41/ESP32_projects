#ifndef BLE_PARSER_H
#define BLE_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#define BLE_PARSER_MAX_NAME   33
#define BLE_PARSER_MAX_URL    32
#define BLE_PARSER_UUID_STR   37   // 36 chars + NUL
#define BLE_PARSER_NS_STR     21   // 10 bytes hex + NUL
#define BLE_PARSER_INST_STR   13   // 6 bytes hex + NUL

// Clasificacion del dispositivo a partir de sus datos ADV
typedef enum {
    BLE_DEV_UNKNOWN = 0,
    BLE_DEV_IBEACON,          // Mfg Apple 0x004C, tipo 0x0215
    BLE_DEV_EDDYSTONE_UID,    // Service data 0xFEAA, frame 0x00
    BLE_DEV_EDDYSTONE_URL,    // Service data 0xFEAA, frame 0x10
    BLE_DEV_EDDYSTONE_TLM,    // Service data 0xFEAA, frame 0x20
    BLE_DEV_FINDMY,           // HEURISTICA Apple offline-finding (ID rotativo)
    BLE_DEV_APPLE_OTHER,      // Mfg Apple sin tipo reconocido
    BLE_DEV_NAMED,            // Con nombre, sin formato conocido
} ble_dev_type_t;

typedef struct {
    ble_dev_type_t type;
    bool has_name;
    char name[BLE_PARSER_MAX_NAME];

    // iBeacon
    char ibeacon_uuid[BLE_PARSER_UUID_STR];
    uint16_t ibeacon_major;
    uint16_t ibeacon_minor;
    int8_t ibeacon_tx_power;   // dBm calibrado a 1 m

    // Eddystone-UID
    char eddy_namespace[BLE_PARSER_NS_STR];
    char eddy_instance[BLE_PARSER_INST_STR];

    // Eddystone-URL
    char eddy_url[BLE_PARSER_MAX_URL];

    // Eddystone-TLM
    uint16_t tlm_batt_mv;
    int16_t tlm_temp_raw;      // 8.8 fijo: temp_C = raw / 256
    uint32_t tlm_adv_count;
    uint32_t tlm_uptime_s;

    // Generico
    bool has_tx_power;
    int8_t tx_power;
} ble_adv_info_t;

// Decodifica los datos ADV crudos en la estructura info (todo Big-Endian
// segun especificacion de cada formato beacon).
void ble_parser_decode(const uint8_t *data, uint8_t len, ble_adv_info_t *info);

// Nombre legible de la clasificacion
const char *ble_parser_type_str(ble_dev_type_t type);

#endif // BLE_PARSER_H
