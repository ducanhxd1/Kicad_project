idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_nvs app_spi board_guard fault_history i2c
                                ota_update pump_controller pump_display pump_output
                                pump_rtc pump_schedule pump_stats pump_storage
                                runtime_diag st7735 time_manager time_service
                                web_dashboard)
