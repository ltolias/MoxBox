

#include "spiffs_manager.h"
#include "esp_log.h"
#include "ArduinoJson.h"
#include "SPIFFS-arduino.h"



SPIFFSManager::SPIFFSManager(const char * partition_label, bool format_on_fail) {

    fs = new fs::SPIFFSFS();
    mount_path = new char[64];
    snprintf(mount_path, 64, "/%s", partition_label);
   
    ESP_LOGI(TAG, "Mounting internal SPIFFS partition %s at %s\n", partition_label, mount_path);

    while (!fs->begin(format_on_fail, mount_path, 10, partition_label)) {  //const char *basePath = "/spiffs", uint8_t maxOpenFiles = 10, const char *partitionLabel = NULL);) {
        ESP_LOGI(TAG, "SPIFFS Mount failed, this can happen on first-run initialisation\n");
        ESP_LOGI(TAG, "If it happens repeatedly, check if a SPIFFS partition is present for your board?\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "Retrying...\n");
    }

    list_directory("/", 0);
}

SPIFFSManager::~SPIFFSManager() {
    fs->end();
    delete fs;
    delete mount_path;
}


void SPIFFSManager::list_directory(const char *dirname, uint8_t levels) {
    ESP_LOGI(TAG, "Listing SPIFFS directory: %s\r\n", dirname);

    File root = fs->open(dirname);
    if (!root) {
        ESP_LOGI(TAG, "- failed to open directory\n");
        return;
    }
    if (!root.isDirectory()) {
        ESP_LOGI(TAG, "- not a directory\n");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            ESP_LOGI(TAG, "  [DIR] : %s\n", file.name());
            if (levels > 0) {
                list_directory(file.name(), levels - 1);
            }
        } else {
            ESP_LOGI(TAG, "  [FILE] %s (Size: %d bytes)\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}


fs::SPIFFSFS *SPIFFSManager::get_fs() {
    return fs;
}

bool SPIFFSManager::delete_file(const char *file_path) {
    if (fs->exists(file_path)) {
        ESP_LOGI(TAG, "Removing %s\r\n", file_path);
        if (!fs->remove(file_path)) {
            ESP_LOGI(TAG, "Error removing file\n");
            return false;
        }
    } else {
        ESP_LOGI(TAG, "File does not exist - skip removal\n");
        return false;
    }
    return true;
}
bool SPIFFSManager::exists_file(const char *file_path) {
    return fs->exists(file_path);
}

String SPIFFSManager::read_file(const char *file_path) {
    String content;
    if (fs->exists(file_path)) {
        ESP_LOGI(TAG, "Reading from file %s\r\n", file_path);
        File file = fs->open(file_path, FILE_READ);
        if (!file) {
            handle_corrupted_file(file_path);
            return content;
        }
        size_t size = file.size();
        if (size > MAX_FILE_SIZE) {
            handle_corrupted_file(file_path);
            return content;
        }
        while (file.available()) {
            content += char(file.read());
            if (content.length() > size) {
                handle_corrupted_file(file_path);
                return content;
            }
        }
        file.close();
        return content;
    } else {
        ESP_LOGI(TAG, "File %s not found, returning empty string.\r\n", file_path);
        return content;
    }
}

void SPIFFSManager::write_file(const char *file_path, const String &content) {
    if (fs->exists(file_path)) {
        ESP_LOGI(TAG, "Updating %s\r\n", file_path);
    } else {
        ESP_LOGI(TAG, "Creating %s\r\n", file_path);
    }
    File file = fs->open(file_path, FILE_WRITE);
    file.print(content);
    file.close();
}
void SPIFFSManager::write_file(const char *file_path, char * content) {
    if (fs->exists(file_path)) {
        ESP_LOGI(TAG, "Updating %s\r\n", file_path);
    } else {
        ESP_LOGI(TAG, "Creating %s\r\n", file_path);
    }
    File file = fs->open(file_path, FILE_WRITE);
    file.print(content);
    file.close();
}

int SPIFFSManager::apply_config(const String &json_input, ControlQueue *ipc_control_queue) {
    DynamicJsonDocument doc(JSON_DOC_SIZE);
    deserializeJson(doc, json_input);

    int outlier_setting = doc["outlier_setting"].as<int>();
    int logging_enabled = doc["logging_enabled"].as<int>();

    ipc_control_queue->send(outlier_setting, 0, UPDATE_SETTINGS);
    return logging_enabled;
}

void SPIFFSManager::handle_corrupted_file(const char *file_path) {
    ESP_LOGI(TAG, "File appears to be corrupt, skipping...\n");
}
