#include <stdio.h>
#include <time.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <driver/gpio.h>

#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_vfs.h>

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_gatt_common_api.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

const char* token = "BGv0J1pTjJCoi06NhEpra6JQokCe1ubhUmKnzO5nTA";

uint8_t q_unlock = 0;
uint8_t f_unlock = 0;
uint8_t f_done = 0;
uint16_t f_air = 0;
uint8_t f_off = 0;

static uint8_t adv_service_uuid128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

struct connection_t {
    uint16_t conn_id;
    uint8_t connected;
    uint8_t auth;
    time_t at;
};

struct connection_t connected[3] = {
    {.connected = false}, {.connected = false}, {.connected = false}
};

static struct {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
} gatts_profile[1] = {
    {
        .gatts_if = ESP_GATT_IF_NONE
    }
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00C2,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    //.min_interval = 0x0006,
    //.max_interval = 0x0010,
    .appearance = 0x00C2,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static uint8_t char1_str[] = {'v','a','l', 'u', 'e'};

static esp_attr_value_t gatts_demo_char1_val =
{
    .attr_max_len = 200,
    .attr_len     = sizeof(char1_str),
    .attr_value   = char1_str,
};

struct split_res {
    char* data;
    int len;
};

void wifi_unlock(void* event_handler_arg, const char* event_base, int32_t event, void* data);
void unlock(void*);
static void esp_ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
void esp_gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t itf, esp_ble_gatts_cb_param_t* param);
uint8_t connected_count();
void auth_task(void*);
void authorize(uint16_t conn_id);
uint8_t get_auth(uint16_t conn_id);
void add_conn(esp_ble_gatts_cb_param_t* conn_p);
void remove_conn(uint16_t conn_id);

void show_bluetooth_addr(esp_bd_addr_t addr) {
    for(int i = 0; i < 6; i++) {
        printf("%02x", addr[i]); 
        if(i < 5) printf(":");
    };
    printf("\n");
}

int split(unsigned char* text, char delim, int size, struct split_res* out) {
    int len = 0;
    int wtok = 0;
    char* tok = malloc(0);
    for(int i = 0; i < size; i++) {
        if(text[i] != delim) {
            tok = realloc(tok, ++len);
            tok[len-1] = text[i];
        } else {
            out[wtok].data = tok;
            out[wtok++].len = len;
            len = 0;
            tok = malloc(0);
        };
    };
    if(len) {
        out[wtok].data = tok;
        out[wtok++].len = len;
    };
    return wtok;
};

void app_main(void)
{
    printf("Central Lock Controller started\n");
 
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }


    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    esp_bt_controller_init(&bt_cfg);

    esp_bt_controller_enable(ESP_BT_MODE_BLE);


    gpio_set_direction(2, GPIO_MODE_OUTPUT);
    gpio_set_level(2, 0);
    
    esp_event_loop_create_default();

    if(!esp_bluedroid_init()) printf("Bluedroid initialized\n");
    else printf("Bluedroid initialization fail\n");

    wifi_init_config_t wifi_conf = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_main_conf = {
        .ap = {
            .ssid = "Central-Lock",
            .password = "Cybertron@9237",
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 1,
            .pmf_cfg.required = true
        },
    };

    esp_event_handler_instance_register("WIFI_EVENT", WIFI_EVENT_AP_STACONNECTED, wifi_unlock, NULL, NULL);

    esp_wifi_init(&wifi_conf);
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_main_conf);
    esp_wifi_start();

    if (!esp_bluedroid_enable()) {
        printf("Central Lock Bluetooth started\n");

        esp_ble_gap_register_callback(esp_ble_gap_cb);
        esp_ble_gatts_register_callback(esp_gatts_cb);

        /*
        esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
        esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
        esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
        esp_bt_pin_code_t pin_code = {};
        esp_bt_gap_set_pin(pin_type, 0, pin_code);
        */
    
        esp_ble_gatts_app_register(0);

        esp_ble_gatt_set_local_mtu(200);

        /*
        esp_bt_l2cap_init();

        esp_bt_l2cap_vfs_register();
        esp_bt_l2cap_start_srv(ESP_BT_L2CAP_SEC_AUTHENTICATE, 0x1001);
        */
        

    } else printf("Bluetooth fail\n");

    xTaskCreate(unlock, "unlock", 1024, NULL, 5, NULL);
    xTaskCreate(auth_task, "auth", 1024, NULL, 5, NULL);
}

int match_key(const char* key, char* recv_key, int osize, int rsize) {
    if(rsize != osize) return 0;
    
    for(int i = 0; i < rsize; i++) {
        if(key[i] != recv_key[i]) return 0;
    };
    return 1;
};

void unlock(void*) {
    while (1) {
        if(f_unlock && !f_done) {
            gpio_set_level(2, 1);
            f_done = 1;
        };
        if(q_unlock && !f_unlock) {
            f_done = 0;
            gpio_set_level(2, 1);
            vTaskDelay(1000 * 5 / portTICK_PERIOD_MS);
            gpio_set_level(2, 0);
            q_unlock = 0;
        }
    };
};

void wifi_unlock(void* event_handler_arg, const char* event_base, int32_t event, void* data) {
    f_unlock = 0;
    f_done = 0;
    q_unlock = 1;
};

static void esp_ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;

    default:
        break;
    }
};

void esp_gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t itf, esp_ble_gatts_cb_param_t* param) {
    switch (event)
    {
    case ESP_GATTS_REG_EVT: {
        gatts_profile[0].gatts_if = itf;
        gatts_profile[0].app_id = param->reg.app_id;
        gatts_profile[0].service_id.id.inst_id = 0;
        gatts_profile[0].service_id.is_primary = 1;
        gatts_profile[0].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gatts_profile[0].service_id.id.uuid.uuid.uuid16 = 0x00ff;

        esp_ble_gap_set_device_name("CentralLock");

        esp_ble_gatts_create_service(itf, &gatts_profile->service_id, 4);

        esp_ble_gap_config_adv_data(&adv_data);

        esp_ble_gap_config_adv_data(&scan_rsp_data);

        break;
    };


    case ESP_GATTS_WRITE_EVT: {
        printf("\nTOKEN: ");
        for(int i = 0; i < param->write.len; i++) {
            printf("%c", *(param->write.value + i));
        };
        printf("\nEND\n");

        struct split_res res[10];
        printf("\nis_prep: %d\n", param->write.is_prep);
        int size = split(param->write.value, ' ', param->write.len, res);

        if(!get_auth(param->write.conn_id)) {
            if(size == 2) {
                if(match_key(token, res[0].data, 42, res[0].len)) {
                    if(match_key("auth", res[1].data, 4, res[1].len)) {
                        authorize(param->write.conn_id);
                    } else {
                        esp_ble_gatts_close(itf, param->write.conn_id);
                        remove_conn(param->write.conn_id);
                    }
                } else {
                    esp_ble_gatts_close(itf, param->write.conn_id);
                    remove_conn(param->write.conn_id);
                }
            } else {
                esp_ble_gatts_close(itf, param->write.conn_id);
                remove_conn(param->write.conn_id);
            }
            break;
        };

        if(size) {
            if(match_key(token, res[0].data, 42, res[0].len)) {
                if(size == 1) {
                    q_unlock = 1;
                    f_unlock = 0;
                }
                if(size == 2) {
                    if(match_key("forever", res[1].data, 7, res[1].len)) {
                        f_done = 0;
                        f_unlock = 1;
                    } else if (match_key("lock", res[1].data, 4, res[1].len)) {
                        if(size == 3) {
                            if(match_key("air", res[2].data, 3, res[2].len)) {
                                if(f_air && !f_off) {
                                    gpio_set_level(2, 0);
                                    f_unlock = 0;
                                    f_done = 0;
                                }
                                f_air = 0;
                            }
                        } else {
                            if(!f_air) {
                                gpio_set_level(2, 0);
                                f_unlock = 0;
                                f_done = 0;
                            }
                            if(f_unlock) f_off = 0;
                        };
                    }
                } else if(size == 1) {
                    if(!f_air) {
                        q_unlock = 1;
                        f_unlock = 0;
                    }
                }
                
            };
        }

        for(int i = 0; i < size; i++) free(res[i].data);

        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        gatts_profile[0].service_handle = param->create.service_handle;
        gatts_profile[0].char_uuid.len = ESP_UUID_LEN_16;
        gatts_profile[0].char_uuid.uuid.uuid16 = 0xff01;

        esp_ble_gatts_start_service(gatts_profile[0].service_handle);

        esp_ble_gatts_add_char(
            gatts_profile[0].service_handle, 
            &gatts_profile->char_uuid, 
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            &gatts_demo_char1_val,
            NULL        
        );
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        gatts_profile[0].char_handle = param->add_char.attr_handle;
        break;
    }

    case ESP_GATTS_CONNECT_EVT: {
        esp_ble_conn_update_params_t conn_params = {0};
        if(connected_count() < 3) esp_ble_gap_start_advertising(&adv_params);
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
        gatts_profile[0].conn_id = param->connect.conn_id;
        //start sent the update connection parameters to the peer device.
        show_bluetooth_addr(param->connect.remote_bda);
        esp_ble_gap_update_conn_params(&conn_params);
        add_conn(param);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        remove_conn(param->disconnect.conn_id);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    }

    default:
        break;
    }
};

uint8_t connected_count() {
    uint8_t count = 0;
    for(int i = 0; i < 3; i++) {
        if(connected[i].connected) count++;
    };  
    return count;
}

void authorize(uint16_t conn_id) {
    for(int i = 0; i < 3; i++) {
        if(connected[i].conn_id == conn_id) connected[i].auth = true;
    };    
}

uint8_t get_auth(uint16_t conn_id) {
    for(int i = 0; i < 3; i++) {
        if(connected[i].conn_id == conn_id) {
            return connected[i].auth;
        };
    };
    return 0;
}

void add_conn(esp_ble_gatts_cb_param_t* conn_p) {
    for(int i = 0; i < 3; i++) {
        if(!connected[i].connected) {
            connected[i].connected = true;
            connected[i].conn_id = conn_p->connect.conn_id;
            connected[i].auth = false;
            connected[i].at = time(NULL);
            break;
        };
    };
};

void remove_conn(uint16_t conn_id) {
    for(int i = 0; i < 3; i++) {
        if(connected[i].conn_id == conn_id) {
            connected[i].connected = false;
            break;
        };
    };
};

void auth_task(void*) {
    while(1) {
        for(int i = 0; i < 3; i++) {
            if(connected[i].connected && !connected[i].auth && (time(NULL) - connected[i].at) > 8) {
                esp_ble_gatts_close(gatts_profile[0].gatts_if, connected[i].conn_id);
                connected[i].connected = false;
            };
        };
    }
}

/*
void bluetooth_recv(void* param) {
    esp_bt_l2cap_cb_param_t* all_param = param;
    int fd = all_param->open.fd;
    uint8_t* addr = all_param->open.rem_bda;

    char* data = malloc(100);

    while (1)
    {
        int size = read(fd, data, 100);
        if(size < 0) {
            break;
        } else if (size) {
            for(int i = 0; i < size; i++) printf("%c", data[i]);
            printf("\n");
            if (match_key(token, data, 10, size)) {
                q_unlock = 1;
                write(fd, "1", 9);
            } else {
                close(fd);
            };
        };
    };
};

void bluetooth_event(esp_bt_l2cap_cb_event_t event, esp_bt_l2cap_cb_param_t* param) {
    switch (event)
    {
    case ESP_BT_L2CAP_START_EVT:
        printf("Bluetooth L2CAP server started\n");
        break;
    case ESP_BT_L2CAP_OPEN_EVT: {
        printf("Client @ ");
        show_bluetooth_addr(param->open.rem_bda);
        printf(" connected\n");   
        xTaskCreate(bluetooth_recv, "recv_task", 2048, (void*)param, 5, NULL);
        break;
    }
    default:
        break;
    };
};
*/