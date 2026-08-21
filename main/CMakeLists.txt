idf_component_register(
  SRCS
    "app_main.c"
    "diagnostic.c"
    "ram_disk.c"
    "release_version.c"
    "storage.c"
    "image_store.c"
    "manifest_policy.c"
    "manifest_verify.c"
    "update_client.c"
    "usb_msc.c"
    "wifi_station.c"
    "board/tdongle_s3.c"
  INCLUDE_DIRS "." "board"
  REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_app_format esp_driver_gpio esp_driver_spi esp_driver_sdmmc esp_lcd esp_http_client esp_timer fatfs mbedtls sdmmc esp_tinyusb
)
