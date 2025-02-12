#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H


#include "defines.h"
#include "structs.h"
#include "control_queue.h"
#include "WString.h"
#include "freertos/FreeRTOS.h"
#include "SPIFFS-arduino.h"


#define JSON_DOC_SIZE 512

class SPIFFSManager {
public:
    SPIFFSManager(const char * partition_label, bool format_on_fail=false);

    ~SPIFFSManager();

    void list_directory(const char *dirname, uint8_t levels);
    bool delete_file(const char *file_path);
    String read_file(const char *file_path);
    bool exists_file(const char *file_path);
    void write_file(const char *file_path, const String &content);
    void write_file(const char *file_path, char * content);
    int apply_config(const String &json_input, ControlQueue * ipc_control_queue);

    fs::SPIFFSFS *get_fs();

private:
    char *mount_path;
    fs::SPIFFSFS *fs;
    void handle_corrupted_file(const char *file_path);
    static constexpr const char * const TAG = "spiffs_mgr";
};

#endif // SPIFFS_MANAGER_H
