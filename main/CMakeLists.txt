idf_component_register(SRCS
                            "main.c"
                            "gfx.c"
                            "font5x7.c"
                            "board_i2c.c"
                            "touch.c"
                            "storage.c"
                            "sensor.c"
                            "outdoor.c"
                            "statusled.c"
                            "testscreen.c"
                            "bigdigit.c"
                            "infotile.c"
                            "clock.c"
                            "wifi.c"
                            "netcfg.c"
                            "configmode.c"
                            "webserver.c"
                            "discovery.c"
                            "watchdog.c"
                            "textutil.c"
                            "sun.c"
                            "tjpgd/tjpgd.c"
                            "ttf.c"
                            "wallpaper.c"
                       INCLUDE_DIRS "." "tjpgd"
                       # Open Sans Regular is linked into the application image
                       # (rodata) and read directly from flash at runtime.
                       EMBED_FILES "fonts/OpenSans-Regular.ttf"
                       # The project is built with MINIMAL_BUILD, so every
                       # component used here has to be listed explicitly.
                       REQUIRES esp_lcd
                                esp_driver_spi
                                esp_driver_gpio
                                esp_driver_ledc
                                esp_driver_i2c
                                esp_driver_rmt
                                esp_timer
                                esp_event
                                esp_netif
                                esp_wifi
                                lwip
                                nvs_flash
                                esp_http_server
                                mdns
                                esp_http_client
                                esp-tls
                                mbedtls
                                espressif__esp_lcd_ili9341
                                espressif__led_strip
                                joltwallet__littlefs)

# Build the littlefs image for the "storage" partition from data/ and write it
# together with `idf.py flash`.  Everything below data/ keeps its relative path:
# data/backgrounds/spring/day/1.jpg -> /littlefs/backgrounds/spring/day/1.jpg
littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)
