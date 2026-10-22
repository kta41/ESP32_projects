#include <stdio.h>
#include <string.h>

#include "ble_parser.h"

// ========== AD TYPES ==========
#define AD_NAME_SHORT    0x08
#define AD_NAME_FULL     0x09
#define AD_TX_POWER      0x0A
#define AD_SERVICE_DATA  0x16
#define AD_MFG_DATA      0xFF

// IDs little-endian tal y como viajan en la trama
#define APPLE_COMPANY_LE  0x4C   // 0x004C -> bytes "4C 00"
#define EDDYSTONE_SVC_LE  0xAA   // 0xFEAA -> bytes "AA FE"

// Eddystone-URL: esquemas y sufijos comprimidos (spec Eddystone)
static const char *k_url_prefix[4] = {
    "http://www.", "https://www.", "http://", "https://"
};
static const char *k_url_suffix[14] = {
    ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
    ".com", ".org", ".edu", ".net", ".info", ".biz", ".gov"
};

// ========== HELPERS ==========
static void hex_encode(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
    size_t pos = 0;
    for (size_t k = 0; k < src_len; k++) {
        if (pos + 2 >= dst_len) break;
        pos += (size_t)snprintf(&dst[pos], dst_len - pos, "%02X", src[k]);
    }
    dst[pos] = '\0';
}

static void format_uuid(char *dst, size_t dst_len, const uint8_t *u)
{
    snprintf(dst, dst_len,
             "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
             u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

// p[0..1]=AA FE | p[2]=0x10 | p[3]=TX power | p[4]=esquema | p[5..]=URL codificada
static void parse_eddy_url(const uint8_t *p, uint8_t plen, ble_adv_info_t *info)
{
    size_t pos = 0;

    if (p[4] < 4) {
        const char *pre = k_url_prefix[p[4]];
        while (*pre && pos < sizeof(info->eddy_url) - 1) {
            info->eddy_url[pos++] = *pre++;
        }
    }
    for (uint8_t k = 5; k < plen && pos < sizeof(info->eddy_url) - 1; k++) {
        if (p[k] < 14) {
            const char *suf = k_url_suffix[p[k]];
            while (*suf && pos < sizeof(info->eddy_url) - 1) {
                info->eddy_url[pos++] = *suf++;
            }
        } else if (p[k] >= 0x20 && p[k] <= 0x7F) {
            info->eddy_url[pos++] = (char)p[k];
        } else {
            break;   // Byte invalido: truncar la URL
        }
    }
    info->eddy_url[pos] = '\0';
}

// ========== DECODIFICADOR PRINCIPAL ==========
void ble_parser_decode(const uint8_t *data, uint8_t len, ble_adv_info_t *info)
{
    memset(info, 0, sizeof(*info));

    uint8_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || (uint16_t)(i + 1 + field_len) > len) {
            break;
        }

        const uint8_t *p = &data[i + 2];
        uint8_t plen = (uint8_t)(field_len - 1);

        switch (data[i + 1]) {
        case AD_NAME_FULL:
        case AD_NAME_SHORT: {
            size_t name_len = plen;
            if (name_len >= sizeof(info->name)) {
                name_len = sizeof(info->name) - 1;
            }
            memcpy(info->name, p, name_len);
            info->name[name_len] = '\0';
            info->has_name = true;
            break;
        }

        case AD_TX_POWER:
            if (plen >= 1) {
                info->tx_power = (int8_t)p[0];
                info->has_tx_power = true;
            }
            break;

        case AD_SERVICE_DATA:
            // Eddystone: service data con UUID 0xFEAA
            if (plen >= 3 && p[0] == EDDYSTONE_SVC_LE && p[1] == 0xFE) {
                uint8_t frame = p[2];
                if (frame == 0x00 && plen >= 20) {
                    // Eddystone-UID: TX pow | namespace(10) | instance(6) | RFU
                    info->type = BLE_DEV_EDDYSTONE_UID;
                    hex_encode(info->eddy_namespace, sizeof(info->eddy_namespace),
                               &p[4], 10);
                    hex_encode(info->eddy_instance, sizeof(info->eddy_instance),
                               &p[14], 6);
                    info->tx_power = (int8_t)p[3];
                    info->has_tx_power = true;
                } else if (frame == 0x10 && plen >= 5) {
                    info->type = BLE_DEV_EDDYSTONE_URL;
                    parse_eddy_url(p, plen, info);
                } else if (frame == 0x20 && plen >= 16) {
                    // Eddystone-TLM: batt | temp 8.8 | adv_cnt | sec_cnt
                    info->type = BLE_DEV_EDDYSTONE_TLM;
                    info->tlm_batt_mv   = (uint16_t)((p[4] << 8) | p[5]);
                    info->tlm_temp_raw  = (int16_t)((p[6] << 8) | p[7]);
                    info->tlm_adv_count = ((uint32_t)p[8] << 24)  | ((uint32_t)p[9] << 16) |
                                          ((uint32_t)p[10] << 8) | (uint32_t)p[11];
                    info->tlm_uptime_s  = ((uint32_t)p[12] << 24) | ((uint32_t)p[13] << 16) |
                                          ((uint32_t)p[14] << 8) | (uint32_t)p[15];
                }
            }
            break;

        case AD_MFG_DATA:
            // Datos de fabricante Apple (0x004C)
            if (plen >= 3 && p[0] == APPLE_COMPANY_LE && p[1] == 0x00) {
                if (p[2] == 0x02 && p[3] == 0x15 && plen >= 25) {
                    // iBeacon: 4C 00 02 15 | UUID[16] | major | minor | txpow (BE)
                    info->type = BLE_DEV_IBEACON;
                    format_uuid(info->ibeacon_uuid, sizeof(info->ibeacon_uuid), &p[4]);
                    info->ibeacon_major = (uint16_t)((p[20] << 8) | p[21]);
                    info->ibeacon_minor = (uint16_t)((p[22] << 8) | p[23]);
                    info->ibeacon_tx_power = (int8_t)p[24];
                } else if (p[2] == 0x10 || p[2] == 0x12) {
                    // HEURISTICA FindMy: 0x12 = offline finding (tag separado),
                    // 0x10 = FindMy reciente/no emparejado. Los identificadores
                    // ROTAN periodicamente: NO sirven como huella estable.
                    info->type = BLE_DEV_FINDMY;
                } else {
                    info->type = BLE_DEV_APPLE_OTHER;
                }
            }
            break;

        default:
            break;
        }

        i += 1 + field_len;
    }

    if (info->type == BLE_DEV_UNKNOWN && info->has_name) {
        info->type = BLE_DEV_NAMED;
    }
}

// ========== NOMBRES ==========
const char *ble_parser_type_str(ble_dev_type_t type)
{
    switch (type) {
        case BLE_DEV_IBEACON:       return "iBeacon";
        case BLE_DEV_EDDYSTONE_UID: return "Eddystone-UID";
        case BLE_DEV_EDDYSTONE_URL: return "Eddystone-URL";
        case BLE_DEV_EDDYSTONE_TLM: return "Eddystone-TLM";
        case BLE_DEV_FINDMY:        return "FindMy?";
        case BLE_DEV_APPLE_OTHER:   return "Apple";
        case BLE_DEV_NAMED:         return "BLE";
        default:                    return "?";
    }
}
