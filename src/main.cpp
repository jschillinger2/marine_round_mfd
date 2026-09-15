#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "sensesp_app.h"
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_value_listener.h"
#include "sensesp/transforms/linear.h"

using namespace sensesp;

// --- Hardware Pins & Expander ---
#define I2C_SCL 10
#define I2C_SDA 11
#define TCA9554_ADDR 0x20

// Global board reference (v1.x style)
esp_panel::board::Board *board = nullptr;

// LVGL variables
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_disp_drv_t disp_drv;

// UI elements
static lv_obj_t *tv; // Tileview
static lv_obj_t *tile1; // Navigation Tile
static lv_obj_t *tile2; // Wind Tile
static lv_obj_t *tile3; // Propulsion Tile
static lv_obj_t *tile4; // System Tile

// Navigation Tile elements
static lv_obj_t *sog_arc;
static lv_obj_t *sog_label;
static lv_obj_t *hdg_label;

// Wind Tile elements
static lv_obj_t *wind_meter;
static lv_meter_indicator_t *wind_needle;
static lv_obj_t *aws_label;

// Propulsion Tile elements
static lv_obj_t *rpm_meter;
static lv_meter_indicator_t *rpm_needle;
static lv_obj_t *rpm_label;
static lv_obj_t *eng_temp_bar;
static lv_obj_t *eng_temp_label;
static lv_obj_t *alt_temp_bar;
static lv_obj_t *alt_temp_label;

// System Tile elements
static lv_obj_t *clock_label;
static lv_obj_t *wifi_status_label;
static lv_obj_t *sk_status_label;

// IO Expander Setup
void init_io_expander() {
    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    
    // Set P0 (LCD_RST), P1 (TP_RST), and P2 (LCD_BL) as OUTPUT
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(0x03); // Config Register
    Wire.write(0xF8); // 1111 1000 (P0, P1, P2 outputs)
    Wire.endTransmission();

    // Reset LCD and Touch
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(0x01); // Output Register
    Wire.write(0x00); // Pull low
    Wire.endTransmission();
    delay(100);

    // Bring high (enables LCD/Touch reset and Backlight)
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(0x01); 
    Wire.write(0x07); // P0, P1, P2 High
    Wire.endTransmission();
    delay(100);
}

// LVGL Display Flush Callback (v1.x style)
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    auto *b = (esp_panel::board::Board *)disp_drv->user_data;
    uint16_t width = (area->x2 - area->x1) + 1;
    uint16_t height = (area->y2 - area->y1) + 1;
    b->getLCD()->drawBitmap(area->x1, area->y1, width, height, (const uint8_t *)color_p);
    lv_disp_flush_ready(disp_drv);
}

// SPD2010 Rounder callback (requires coordinates divisible by 4)
void rounder_cb(lv_disp_drv_t * disp_drv, lv_area_t * area) {
    area->x1 = area->x1 & ~0x3;
    area->x2 = (area->x2 & ~0x3) + 3;
}

// Touch read callback (v1.x style)
void my_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    auto *b = (esp_panel::board::Board *)indev_drv->user_data;
    auto *tp = b->getTouch();
    if (!tp) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    ESP_PanelTouchPoint point;
    int read_touch_result = tp->readPoints(&point, 1);

    if (read_touch_result > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void setup() {
    SetupLogging(); // Replaces SetupSerialDebug() in SensESP v3

    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting Marine MFD Setup...");

    // 1. Hardware Init (I2C IO Expander)
    init_io_expander();

    // 2. Display Init (ESP32_Display_Panel v1.x)
    board = new esp_panel::board::Board();
    if (!board->init()) {
        Serial.println("Board configuration init failed!");
    }
    if (!board->begin()) {
        Serial.println("Board hardware startup failed!");
    }

    // 3. LVGL Init
    lv_init();
    size_t buffer_size = 412 * 412 / 10;
    buf = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, buffer_size);

    // Register display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 412;
    disp_drv.ver_res = 412;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.rounder_cb = rounder_cb; // Critical alignment for SPD2010
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = board;
    lv_disp_drv_register(&disp_drv);

    // Register touch input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touch_read;
    indev_drv.user_data = board;
    lv_indev_drv_register(&indev_drv);

    // 4. Build Smartwatch UI (Dark theme)
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);

    // Create Tileview for horizontal swiping (Smartwatch layout)
    tv = lv_tileview_create(lv_scr_act());
    lv_obj_set_style_bg_color(tv, lv_color_black(), LV_PART_MAIN);

    // Page 1: Navigation Tile
    tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    
    // ARC for SOG
    sog_arc = lv_arc_create(tile1);
    lv_obj_set_size(sog_arc, 300, 300);
    lv_obj_center(sog_arc);
    lv_arc_set_range(sog_arc, 0, 30); // 0 to 30 Knots
    lv_arc_set_value(sog_arc, 0);
    lv_obj_set_style_arc_color(sog_arc, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sog_arc, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sog_arc, 15, LV_PART_INDICATOR);

    sog_label = lv_label_create(tile1);
    lv_label_set_text(sog_label, "0.0 KT");
    lv_obj_set_style_text_font(sog_label, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(sog_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(sog_label);

    lv_obj_t *sog_title = lv_label_create(tile1);
    lv_label_set_text(sog_title, "SPEED OVER GROUND");
    lv_obj_set_style_text_font(sog_title, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(sog_title, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_align(sog_title, LV_ALIGN_CENTER, 0, -35);

    hdg_label = lv_label_create(tile1);
    lv_label_set_text(hdg_label, "HDG: ---°");
    lv_obj_set_style_text_font(hdg_label, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(hdg_label, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_MAIN);
    lv_obj_align(hdg_label, LV_ALIGN_TOP_MID, 0, 45);


    // Page 2: Wind Tile
    tile2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);

    wind_meter = lv_meter_create(tile2);
    lv_obj_set_size(wind_meter, 300, 300);
    lv_obj_center(wind_meter);
    lv_obj_set_style_bg_color(wind_meter, lv_color_black(), LV_PART_MAIN);
    
    lv_meter_scale_t *wind_scale = lv_meter_add_scale(wind_meter);
    lv_meter_set_scale_ticks(wind_meter, wind_scale, 37, 2, 10, lv_color_hex(0x808080));
    lv_meter_set_scale_range(wind_meter, wind_scale, 0, 360, 360, 270); // Full circle, starting top

    wind_needle = lv_meter_add_needle_line(wind_meter, wind_scale, 4, lv_palette_main(LV_PALETTE_RED), -10);
    lv_meter_set_indicator_value(wind_meter, wind_needle, 0);

    aws_label = lv_label_create(tile2);
    lv_label_set_text(aws_label, "0.0 KT");
    lv_obj_set_style_text_font(aws_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(aws_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(aws_label);

    lv_obj_t *wind_title = lv_label_create(tile2);
    lv_label_set_text(wind_title, "WIND");
    lv_obj_set_style_text_font(wind_title, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(wind_title, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_align(wind_title, LV_ALIGN_CENTER, 0, -30);


    // Page 3: Propulsion Tile
    tile3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);

    rpm_meter = lv_meter_create(tile3);
    lv_obj_set_size(rpm_meter, 250, 250);
    lv_obj_center(rpm_meter);
    lv_obj_set_style_bg_color(rpm_meter, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(rpm_meter, LV_ALIGN_CENTER, 0, -25);

    lv_meter_scale_t *rpm_scale = lv_meter_add_scale(rpm_meter);
    lv_meter_set_scale_ticks(rpm_meter, rpm_scale, 41, 2, 8, lv_color_hex(0x808080));
    lv_meter_set_scale_range(rpm_meter, rpm_scale, 0, 5000, 270, 135);

    rpm_needle = lv_meter_add_needle_line(rpm_meter, rpm_scale, 4, lv_palette_main(LV_PALETTE_ORANGE), -10);
    lv_meter_set_indicator_value(rpm_meter, rpm_needle, 0);

    rpm_label = lv_label_create(tile3);
    lv_label_set_text(rpm_label, "0 RPM");
    lv_obj_set_style_text_font(rpm_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(rpm_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(rpm_label, LV_ALIGN_CENTER, 0, -10);

    // Engine Temp Bar & Label
    eng_temp_bar = lv_bar_create(tile3);
    lv_obj_set_size(eng_temp_bar, 100, 10);
    lv_obj_align(eng_temp_bar, LV_ALIGN_BOTTOM_LEFT, 50, -45);
    lv_bar_set_range(eng_temp_bar, 0, 120);
    lv_bar_set_value(eng_temp_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(eng_temp_bar, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_style_img_recolor(eng_temp_bar, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);

    eng_temp_label = lv_label_create(tile3);
    lv_label_set_text(eng_temp_label, "ENG: --°C");
    lv_obj_set_style_text_font(eng_temp_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(eng_temp_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align_to(eng_temp_label, eng_temp_bar, LV_ALIGN_OUT_TOP_MID, 0, -4);

    // Alternator Temp Bar & Label
    alt_temp_bar = lv_bar_create(tile3);
    lv_obj_set_size(alt_temp_bar, 100, 10);
    lv_obj_align(alt_temp_bar, LV_ALIGN_BOTTOM_RIGHT, -50, -45);
    lv_bar_set_range(alt_temp_bar, 0, 120);
    lv_bar_set_value(alt_temp_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(alt_temp_bar, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_style_img_recolor(alt_temp_bar, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_INDICATOR);

    alt_temp_label = lv_label_create(tile3);
    lv_label_set_text(alt_temp_label, "ALT: --°C");
    lv_obj_set_style_text_font(alt_temp_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(alt_temp_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align_to(alt_temp_label, alt_temp_bar, LV_ALIGN_OUT_TOP_MID, 0, -4);


    // Page 4: System/Status Tile
    tile4 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);

    clock_label = lv_label_create(tile4);
    lv_label_set_text(clock_label, "00:00:00");
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(clock_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(clock_label);

    wifi_status_label = lv_label_create(tile4);
    lv_label_set_text(wifi_status_label, "WiFi: Offline");
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(wifi_status_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 65);

    sk_status_label = lv_label_create(tile4);
    lv_label_set_text(sk_status_label, "Signal K: Offline");
    lv_obj_set_style_text_font(sk_status_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(sk_status_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_align(sk_status_label, LV_ALIGN_BOTTOM_MID, 0, -65);


    // 5. SensESP App Init
    SensESPAppBuilder app_builder;
    auto sensesp_app = (&app_builder)
                    ->set_hostname("marine-mfd")
                    ->get_app();

    // 6. Connect listeners to update corresponding LVGL GUI components
    
    // Navigation: SOG (Speed Over Ground)
    auto* sog_listener = new FloatSKListener("navigation.speedOverGround", 1000, "/SignalK/SOG/Path");
    auto* ms_to_knots = new Linear(1.94384, 0.0, "/Transforms/SOG_to_Knots");
    sog_listener->connect_to(ms_to_knots)->connect_to(
        new LambdaConsumer<float>([](float knots) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "%.1f KT", knots);
            lv_label_set_text(sog_label, val_str);
            lv_arc_set_value(sog_arc, (int16_t)knots);
        })
    );

    // Navigation: Heading (Magnetic)
    auto* hdg_listener = new FloatSKListener("navigation.headingMagnetic", 1000, "/SignalK/Heading/Path");
    auto* rad_to_deg_hdg = new Linear(57.2958, 0.0, "/Transforms/Heading_to_Deg");
    hdg_listener->connect_to(rad_to_deg_hdg)->connect_to(
        new LambdaConsumer<float>([](float degrees) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "HDG: %03d°", (int)degrees % 360);
            lv_label_set_text(hdg_label, val_str);
        })
    );

    // Wind: Apparent Wind Angle (AWA)
    auto* awa_listener = new FloatSKListener("environment.wind.angleApparent", 1000, "/SignalK/WindAngle/Path");
    auto* rad_to_deg_awa = new Linear(57.2958, 0.0, "/Transforms/AWA_to_Deg");
    awa_listener->connect_to(rad_to_deg_awa)->connect_to(
        new LambdaConsumer<float>([](float degrees) {
            int rounded_deg = (int)degrees % 360;
            if (rounded_deg < 0) rounded_deg += 360;
            lv_meter_set_indicator_value(wind_meter, wind_needle, rounded_deg);
        })
    );

    // Wind: Apparent Wind Speed (AWS)
    auto* aws_listener = new FloatSKListener("environment.wind.speedApparent", 1000, "/SignalK/WindSpeed/Path");
    auto* ms_to_knots_aws = new Linear(1.94384, 0.0, "/Transforms/AWS_to_Knots");
    aws_listener->connect_to(ms_to_knots_aws)->connect_to(
        new LambdaConsumer<float>([](float knots) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "%.1f KT", knots);
            lv_label_set_text(aws_label, val_str);
        })
    );

    // Propulsion: RPM (Engine Revolutions)
    auto* rpm_listener = new FloatSKListener("propulsion.engine.revolutions", 1000, "/SignalK/RPM/Path");
    auto* hz_to_rpm = new Linear(60.0, 0.0, "/Transforms/Hz_to_RPM");
    rpm_listener->connect_to(hz_to_rpm)->connect_to(
        new LambdaConsumer<float>([](float rpm) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "%d RPM", (int)rpm);
            lv_label_set_text(rpm_label, val_str);
            lv_meter_set_indicator_value(rpm_meter, rpm_needle, (int)rpm);
        })
    );

    // Propulsion: Engine Temp
    auto* eng_temp_listener = new FloatSKListener("propulsion.engine.temperature", 1000, "/SignalK/EngineTemp/Path");
    auto* k_to_c_eng = new Linear(1.0, -273.15, "/Transforms/K_to_C_Engine");
    eng_temp_listener->connect_to(k_to_c_eng)->connect_to(
        new LambdaConsumer<float>([](float celsius) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "ENG: %.0f°C", celsius);
            lv_label_set_text(eng_temp_label, val_str);
            lv_bar_set_value(eng_temp_bar, (int16_t)celsius, LV_ANIM_OFF);
        })
    );

    // Propulsion: Alternator Temp
    auto* alt_temp_listener = new FloatSKListener("electrical.alternators.alternator.temperature", 1000, "/SignalK/AltTemp/Path");
    auto* k_to_c_alt = new Linear(1.0, -273.15, "/Transforms/K_to_C_Alt");
    alt_temp_listener->connect_to(k_to_c_alt)->connect_to(
        new LambdaConsumer<float>([](float celsius) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "ALT: %.0f°C", celsius);
            lv_label_set_text(alt_temp_label, val_str);
            lv_bar_set_value(alt_temp_bar, (int16_t)celsius, LV_ANIM_OFF);
        })
    );


    // 7. Subscribe to general network state changes (WiFi state)
    sensesp_app->get_network_state_producer()->connect_to(
        new LambdaConsumer<NetworkState>([](NetworkState state) {
            if (state == kNetworkNoConnection) {
                lv_label_set_text(wifi_status_label, "WiFi: Unconfigured");
                lv_obj_set_style_text_color(wifi_status_label, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
            } else if (state == kNetworkDisconnected) {
                lv_label_set_text(wifi_status_label, "WiFi: Disconnected");
                lv_obj_set_style_text_color(wifi_status_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
            } else if (state == kNetworkConnected) {
                lv_label_set_text(wifi_status_label, "WiFi: Connected");
                lv_obj_set_style_text_color(wifi_status_label, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
            } else if (state == kNetworkAPMode) {
                lv_label_set_text(wifi_status_label, "WiFi: AP Setup Mode");
                lv_obj_set_style_text_color(wifi_status_label, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
            } else if (state == kNetworkProvisioning) {
                lv_label_set_text(wifi_status_label, "WiFi: Provisioning...");
                lv_obj_set_style_text_color(wifi_status_label, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
            } else {
                lv_label_set_text(wifi_status_label, "WiFi: Unknown");
                lv_obj_set_style_text_color(wifi_status_label, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
            }
        })
    );


    // 8. Register periodic ReactESP timer to update Digital Clock & Signal K WS Connection Status
    event_loop()->onRepeat(1000, []() {
        // Update Digital Clock (NTP or ESP32 local clock)
        time_t raw_time = time(nullptr);
        struct tm time_info;
        localtime_r(&raw_time, &time_info);
        
        char clock_str[16];
        snprintf(clock_str, sizeof(clock_str), "%02d:%02d:%02d", time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
        lv_label_set_text(clock_label, clock_str);

        // Update Signal K WS Client status
        bool sk_connected = false;
        auto ws_client = sensesp::SensESPApp::get()->get_ws_client();
        if (ws_client) {
            sk_connected = ws_client->is_connected();
        }

        if (sk_connected) {
            lv_label_set_text(sk_status_label, "Signal K: Online");
            lv_obj_set_style_text_color(sk_status_label, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        } else {
            lv_label_set_text(sk_status_label, "Signal K: Offline");
            lv_obj_set_style_text_color(sk_status_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        }
    });

    Serial.println("Marine MFD initialized successfully!");
}

void loop() {
    event_loop()->tick();
    lv_timer_handler(); // Let LVGL do its graphics processing
    delay(5);
}
