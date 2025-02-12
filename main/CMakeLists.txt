
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

MACRO(HEADER_DIRECTORIES return_list)
    FILE(GLOB_RECURSE new_list *.h)
    SET(dir_list "")
    FOREACH(file_path ${new_list})
        GET_FILENAME_COMPONENT(dir_path ${file_path} PATH)
        SET(dir_list ${dir_list} ${dir_path})
    ENDFOREACH()
    LIST(REMOVE_DUPLICATES dir_list)
    SET(${return_list} ${dir_list})
ENDMACRO()

HEADER_DIRECTORIES(header_list)


function(make_includable input_file output_file)
    file(READ ${input_file} content)
    set(delim "for_c++_include")
    set(content "R\"${delim}(${content})${delim}\"")
    file(WRITE ${output_file} "${content}")
endfunction(make_includable)

# todo: this can just get loaded from spiffs assets partition along with everything else
make_includable(../frontend/public/certificates.html generated/certificates.html)

idf_component_register(
    SRCS ${app_sources} 
    INCLUDE_DIRS 
        ${header_list}
    REQUIRES
        "nvs_flash"
        "spi_flash"
        "esp_psram"
        "esp_eth"
        "esp_wifi"
        "esp_adc"
        "mdns"
        "wifi_provisioning"
        "wpa_supplicant"
        "esp_https_server" 
        "esp_http_client"
        "app_update"
        "esp_https_ota"
        "spiffs"
        "fatfs"
        "esp32-camera"
)

# todo: psychichttp async seems to be broken for POST requests
#    "-DENABLE_ASYNC=1" 

component_compile_options(
    "-DBOARD_HAS_PSRAM"
    "-mfix-esp32-psram-cache-issue"
    "-DCONFIG_ARDUINO_UDP_TASK_PRIORITY=10"
    "-DCONFIG_ARDUINO_UDP_RUNNING_CORE=0"
    "-DCONFIG_ARDUINO_EVENT_RUNNING_CORE=0"
    "-DCONFIG_ARDUINO_RUNNING_CORE=0"
    "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1"
)

# Create a FAT filesystem image from the contents of /data,
# The image will be flashed into the 'assets' partition when 'idf.py flash' is used.
spiffs_create_partition_image(assets ../data FLASH_IN_PROJECT) # SPIFFS_IMAGE_FLASH_IN_PROJECT FLASH_IN_PROJECT


# Uncomment to bundle in rootca certs from /certs.
# filenames should be root_ca.crt and root_ca.key
spiffs_create_partition_image(certs ../certs FLASH_IN_PROJECT)