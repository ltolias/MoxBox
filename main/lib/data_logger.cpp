#include "data_logger.h"

#include <stdio.h>
#include "SD_MMC.h"
#include <map>

#include "dirent.h"
#include "structs.h"
#include "esp_log.h"


DataLoggerDB::DataLoggerDB(const char *db_dirname){
    strncpy(this->db_dirname, db_dirname, BUFFER_SIZE_FILE_NAME);

    initSDCard();


    //find all loggers and figure out the latest stored time
    SD_MMC.mkdir(db_dirname);
    File root_dir = SD_MMC.open(db_dirname);
    if(!root_dir || !root_dir.isDirectory()) {
        //handle error
        return;
    }
    
    File data_dir = root_dir.openNextFile();
    while(data_dir){
        if(data_dir.isDirectory()) {
            File data_subdir = data_dir.openNextFile();

            while(data_subdir){
                if(data_subdir.isDirectory()){
                    int mins_per_file = atoi(data_subdir.name());

                    char path[BUFFER_SIZE_FILE_NAME];
                    snprintf(path, BUFFER_SIZE_FILE_NAME, "%.32s/%.32s/%.32s", db_dirname, data_dir.name(), data_subdir.name());

                    DataLogger *data_logger = new DataLogger(path, mins_per_file);
                    ESP_LOGI(TAG, "Loaded existing db at startup: %s latest time=%llu\n", path, data_logger->latest_time_stored_ms);

                    latest_time_stored_ms = max(latest_time_stored_ms, data_logger->latest_time_stored_ms);
                    loggers.insert(std::make_pair(data_logger->dirname, data_logger));
                }
                data_subdir = data_dir.openNextFile();
            }
        }
        data_dir = root_dir.openNextFile();
    }


}

void DataLoggerDB::initSDCard() {
    ESP_LOGI(TAG, "Mounting filesystem\n");

    #ifdef BOARD_ESP_32S3_FREENOVE
    if(!SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0)){
       ESP_LOGI(TAG, "failed to set pins for sd card\n");
    }
    #endif
    
    if(!SD_MMC.begin("/sd", true, false, 40000, (uint8_t) 16U)){ 
        ESP_LOGI(TAG, "Card Mount Failed\n");
        //todo: make an endpoint to reformat broken cards
        //note: edited sd_mmc class to make _card public...
        // esp_vfs_fat_sdcard_format("/sd", SD_MMC._card);
    }
    uint8_t cardType = SD_MMC.cardType();

    if(cardType == CARD_NONE){
        ESP_LOGI(TAG, "No SD_MMC card attached\n");
        return;
    }

    ESP_LOGI(TAG, "SD_MMC Card Type: \n");
    if(cardType == CARD_MMC){
        ESP_LOGI(TAG, "MMC\n");
    } else if(cardType == CARD_SD){
        ESP_LOGI(TAG, "SDSC\n");
    } else if(cardType == CARD_SDHC){
        ESP_LOGI(TAG, "SDHC\n");
    } else {
        ESP_LOGI(TAG, "UNKNOWN\n");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    ESP_LOGI(TAG, "SD_MMC Card Size: %lluMB\n", cardSize);
};



void DataLoggerDB::write(char * name, int64_t time_ms, double value, int logging_inteval_ms) {
    for (timings_t timing_detail: timing_details) {

        char logger_key[BUFFER_SIZE_FILE_NAME];
        snprintf(logger_key, BUFFER_SIZE_FILE_NAME, "%.32s/%.32s/%d", db_dirname, name, timing_detail.mins_per_file);

        //track stats
        
        auto it1 = decimators.find(logger_key);
        Decimator *decimator;
        //find or create decimator
        if (it1 == decimators.end()) {     
            decimator = new Decimator(logger_key, timing_detail);
            decimators.insert(std::make_pair(decimator->dirname, decimator));
        } else {
            decimator = it1->second;
        }


        if (decimator->decimate(time_ms, value)) {
            double decimated = decimator->latest();

            DataLogger * data_logger;
            //find or create logger
            auto it2 = loggers.find(logger_key);
            if (it2 == loggers.end()){       
                data_logger = initialize_logger(name, timing_detail.mins_per_file);
            } else {
                data_logger = it2->second;
            }
            data_logger->write(time_ms, decimated, timing_detail.discard_after_hrs, timing_detail.batch_writes);
            if (time_ms > latest_time_stored_ms) {
                latest_time_stored_ms = time_ms;
            }
        }
    }
};

DataLogger * DataLoggerDB::initialize_logger(const char * name, int mins_per_file) {
    char buffer[BUFFER_SIZE_FILE_NAME];

    snprintf(buffer, BUFFER_SIZE_FILE_NAME, "%.32s/%.32s", db_dirname, name);
    SD_MMC.mkdir(buffer);

    snprintf(buffer, BUFFER_SIZE_FILE_NAME, "%.32s/%.32s/%d", db_dirname, name, mins_per_file);
    SD_MMC.mkdir(buffer);

    ESP_LOGI(TAG, "Initializing new db at runtime name:%s path:%s mpf:%d\n", name, buffer, mins_per_file);
    
    DataLogger *data_logger = new DataLogger(buffer, mins_per_file);
    
    loggers.insert(std::make_pair(data_logger->dirname, data_logger));

    return data_logger;
}


void DataLoggerDB::list_dir(char * path, int current_depth) {
    DirWalker dir_lister = DirWalker(path);
    char buffer[BUFFER_SIZE_FILE_NAME] = {0};
    char name_buffer[BUFFER_SIZE_FILE_NAME] = {0};
    char prefix[16] = {0};
    for (int i = 0; i < current_depth; i++) {
        prefix[i] = '-';
    };
    prefix[current_depth] = '\0';
    bool is_directory = false;
    if (dir_lister.exists()) {
        int64_t size = 0;
        int64_t num_files = 0;
        int64_t total_size = 0;
        while (dir_lister.next(buffer, &is_directory, &size, name_buffer)) {
            if (!is_directory) {
                num_files += 1U;
                total_size += size;
            } else {
                ESP_LOGI(TAG, "%s [%s]\n", prefix, name_buffer);
                list_dir(buffer, current_depth+1);
            }
        }
        ESP_LOGI(TAG, "%s file_count=%lli, total_size=%lli\n", prefix, num_files, total_size);

    }
}


void DataLoggerDB::delete_dir(char * path, int current_depth) {
    DirWalker dir_lister = DirWalker(path);
    char buffer[BUFFER_SIZE_FILE_NAME] = {0};
    
    bool is_directory = false;
    if (dir_lister.exists()) {
        while (dir_lister.next(buffer, &is_directory)) {
            if (is_directory) {
                delete_dir(buffer, current_depth+1);
            }
            ESP_LOGI(TAG, "Deleting %s\n", buffer);
            remove(buffer);
        }

    }
}

DirWalker::DirWalker(char *path) {
    strncpy(this->path, path, BUFFER_SIZE_FILE_NAME);
    dir = opendir(path);  
}
DirWalker::~DirWalker() {
    closedir(dir);
}

bool DirWalker::exists() {
    return dir != nullptr;
}

bool DirWalker::next(char *buffer, bool *is_directory, int64_t *size, char *fname) {         
    buffer[0] = '\0';
    struct dirent *file = readdir(dir);
    if (file == NULL) {
        return false;
    } else {
        strcat(buffer, path);
        if (path[strlen(path)-1] != '/' && file->d_name[0] != '/') {
            strcat(buffer, "/");
        }
        strcat(buffer, file->d_name);
        if (fname) {
            strcpy(fname, file->d_name);
        }
        *is_directory = false;
        if (file->d_type == DT_DIR) {
            *is_directory = true;
            return true;
        }
        if (file->d_type == DT_REG) {
            if (size) {
                struct stat st; 
                if (stat(buffer, &st) == 0)
                    *size = st.st_size;
                else 
                    *size = -1;
            }
            return true;
        }
    }
    return false;
};


DataLogger::DataLogger(const char * logger_dirname, int min_per_file) { // Constructor with parameters
    
    this->min_per_file = min_per_file;

    strncpy(this->dirname, logger_dirname, BUFFER_SIZE_FILE_NAME);

    File root = SD_MMC.open(this->dirname);

    char buffer[BUFFER_SIZE_FILE_NAME] = {0};
    bool is_directory = false;

    snprintf(buffer, BUFFER_SIZE_FILE_NAME,  "%.32s%.64s", "/sd", this->dirname);

    //scan to the last file alphabetially (theyre number names)
    DirWalker dir_lister = DirWalker(buffer);
    char buffer_save[BUFFER_SIZE_FILE_NAME] = {0};
    if (dir_lister.exists()) {
        while (dir_lister.next(buffer, &is_directory)) {
            if (!is_directory) {
                strcpy(buffer_save, buffer);
            }
        }

    }
    //open up the last file to check the end of log time
    if (strlen(buffer_save) > 0) {
        // + 3 cuts off the /sd from the beginning of path (arduino libs don't want this but esp libs do
        File file = SD_MMC.open(buffer_save+3); 
        if(file){
            char buffer[BUFER_SIZE_FILE_READ];
            while(file.available()){
                file.readBytesUntil('\n', buffer, BUFER_SIZE_FILE_READ - 1);
            }
            
            char *ptr = strchr(buffer, ',');
            
            if(ptr) {
                int index = ptr - buffer;
                *ptr = '\0';
                latest_time_stored_ms = strtoull(buffer, NULL, 0);
                latest_index_stored = time_ms_to_index(latest_time_stored_ms);
            }
        }
    }
        
}


void DataLogger::flush_buffer(char *fname) {
    ESP_LOGI(TAG, "Persisting %i bytes to %s\n", buffer_counter, fname);
    File file = SD_MMC.open(fname, FILE_APPEND);
    if(!file || !file.print(write_buffer)){
        ESP_LOGI(TAG, "error writing to sd");
    }     
    write_buffer[0] = '\0'; 
    buffer_counter = 0;  
}
int64_t DataLogger::time_ms_to_index(int64_t time_ms) {
    return (time_ms - TIME_BASE_MS) / (1000 * 60 * min_per_file);
}
void DataLogger::write(int64_t time_ms, double val, int discard_after_hrs, bool batch_writes) {
    if (time_ms > latest_time_stored_ms) {
        int64_t file_idx = time_ms_to_index(time_ms);

        char file_name[BUFFER_SIZE_FILE_NAME];
        snprintf(file_name, BUFFER_SIZE_FILE_NAME, "%.64s/%lli", dirname, file_idx); //64 is to suppress truncation warning error


        char text_to_write[BUFER_SIZE_FILE_WRITE];
        snprintf(text_to_write, BUFER_SIZE_FILE_WRITE, "%lld,%f\n", time_ms, val);
        int write_size = strlen(text_to_write);

            
        if (batch_writes) {
            //new file we need to write to is not the same as before
            if (strncmp(file_name, file_name_buffered, BUFFER_SIZE_FILE_NAME) != 0) { 
                //and there's something in the buffer
                if (strlen(file_name_buffered) > 0 && buffer_counter > 0) { 
                    //write out to the previous file we were buffering and clean buffer
                    flush_buffer(file_name_buffered);  
                }
                //set our new buffered filename
                strncpy(file_name_buffered, file_name, BUFFER_SIZE_FILE_NAME); 
            }
            //also flush buffer if we're going to overflow
            if (buffer_counter + write_size > BUFFER_N_BYTES - 1) { 
                if (strlen(file_name_buffered) > 0) {
                    flush_buffer(file_name_buffered);
                }
            }
            //now write new data into buffer
            strncat(write_buffer, text_to_write, BUFER_SIZE_FILE_WRITE);
            buffer_counter += write_size;
        } else {
            //otherwise write directly to file
            ESP_LOGI(TAG, "Persisting %i bytes to %s\n", write_size, file_name);
            File file = SD_MMC.open(file_name, FILE_APPEND);
            if(!file || !file.print(text_to_write)){
                ESP_LOGI(TAG, "error writing to sd");
            }
        }
        //try to delete an expired file every time we begin writing to a new file
        //todo: make this guaranteed - it can miss currently but not a huge issue as the files are small
        if (latest_index_stored != file_idx) {
            // -1 on the end here is because we want to keep one extra file, as the most recent one will be incomplete for a while
            int64_t file_idx_to_delete = time_ms_to_index(time_ms - (discard_after_hrs * 60 * 60 * 1000)) - 1;
            if (file_idx_to_delete > 0) {
                char expired_file_name[BUFFER_SIZE_FILE_NAME];
                snprintf(expired_file_name, BUFFER_SIZE_FILE_NAME, "%.64s/%lli", dirname, file_idx_to_delete); //64 is to suppress truncation warning error
                if (SD_MMC.exists(expired_file_name)) {
                    ESP_LOGI(TAG, "Deleting expired file %s\n", expired_file_name);
                    SD_MMC.remove(expired_file_name);
                } else {
                    ESP_LOGI(TAG, "Expired file %s not found, skipping delete\n", expired_file_name);
                }
            }
        }
        latest_index_stored = file_idx;
        latest_time_stored_ms = time_ms; 
    } else {
        ESP_LOGI(TAG, "Back in time on path:%s time:%lld latest recorded time:%lld\n", dirname, time_ms, latest_time_stored_ms);
    }      
}

        

/*
1s for 1h by 5min = 12ind, 300rpf, 3600rt 
1min for 3 days by 4 hours = 18ind, 240rpf, 4320rt  
15min for 900 days by 72 hours = 300ind, 288rpf, 86400rt for 2.5yrs,            
*/

Decimator::Decimator(const char * dirname, timings_t timing_detail) {
    this->timing_detail = timing_detail;
    strncpy(this->dirname, dirname, BUFFER_SIZE_FILE_NAME);
};


bool Decimator::decimate(int64_t time_ms, double value) {
    if (interval_start_ms == 0) {
        interval_start_ms = time_ms; 
    }
    val_accumulator += value;
    samples_aggregated++;
    
    if (time_ms < interval_start_ms + timing_detail.ms_per_row) {
        return false;
    } else {
        latest_decimated = val_accumulator / samples_aggregated;
        // ESP_LOGI(TAG, "agged %d samples, %lld, %lld\n", samples_aggregated, interval_start_ms, time_ms);
        val_accumulator = 0;
        samples_aggregated = 0;
        interval_start_ms = time_ms;
        return true;
    }
};
double Decimator::latest() {
    return latest_decimated;
};

