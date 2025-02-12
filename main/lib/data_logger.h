#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H


#include <stdio.h>
#include "SD_MMC.h"

#include <map>

#include "dirent.h"

#include "structs.h"

#include "defines.h"

#define WARN_SUPPRESS_VOLATILE(val) (volatile typeof(val)){(val)}

#define BUFFER_N_BYTES 1024

#define BUFFER_SIZE_FILE_NAME 128

#define BUFER_SIZE_FILE_READ 128

#define BUFER_SIZE_FILE_WRITE 128

#define TIME_BASE_MS 1710165000000

#define ENABLE_WRITING true


struct cmp_c_str {
   bool operator()(char const *a, char const *b) const {
      return strcmp(a, b) < 0;
   }
};


class DataLogger {  
public:
    DataLogger(const char * dirname, int min_per_file);
    void flush_buffer(char *fname);
    void write(int64_t time_ms, double val, int discard_after_hrs, bool batch_writes); 
    int64_t time_ms_to_index(int64_t time_ms);    

    int64_t latest_time_stored_ms = 0;  
    char dirname[BUFFER_SIZE_FILE_NAME];
    int min_per_file;
private:
    int64_t latest_index_stored = 0;
    char write_buffer[BUFFER_N_BYTES] = {0};
    int buffer_counter = 0;
    char file_name_buffered[BUFFER_SIZE_FILE_NAME];

    static constexpr const char * const TAG = "data_logger";
};


class Decimator {
public:
    Decimator(const char *dirname, timings_t timing_detail_in);
    bool decimate(int64_t time_ms, double value);
    double latest();
    
    char dirname[BUFFER_SIZE_FILE_NAME];
private:
    timings_t timing_detail;
    double val_accumulator = 0;
    int64_t interval_start_ms = 0;
    int samples_aggregated = 0;
    double latest_decimated = 0;

    static constexpr const char * const TAG = "decimator";
};


class DataLoggerDB {
public:  
    DataLoggerDB(const char * db_dirname);
    void write(char * name, int64_t time_ms, double value, int logging_inteval_ms);
    void list_dir(char * path, int current_depth = 1);
    void delete_dir(char * path, int current_depth = 1);

    timings_t timing_details[3] = {
        {.ms_per_row=1000, .mins_per_file=5, .discard_after_hrs=1, .batch_writes=true}, 
        {.ms_per_row=60000, .mins_per_file=240, .discard_after_hrs=72, .batch_writes=false}, 
        {.ms_per_row=900000, .mins_per_file=4320, .discard_after_hrs=12720, .batch_writes=false}
    };

    /*
    1s for 1h by 5min = 12ind, 300rpf, 3600rt 
    1min for 3 days by 4 hours = 18ind, 240rpf, 4320rt  
    15min for 900 days by 72 hours = 300ind, 288rpf, 86400rt for 2.5yrs,            
    */

    char db_dirname[BUFFER_SIZE_FILE_NAME];
    std::map<const char *, DataLogger *, cmp_c_str> loggers;
    int64_t latest_time_stored_ms = 0;
    
private:
    void initSDCard();
    DataLogger * initialize_logger(const char * name, int mins_per_file);

    std::map<const char *, Decimator *, cmp_c_str> decimators;

    static constexpr const char * const TAG = "data_logger_db";
};


class DirWalker {
public:
    DirWalker(char *path);
    ~DirWalker();
    bool exists();
    bool next(char *buffer, bool *is_directory, int64_t *size=nullptr, char *fname=nullptr);
private:
    char path[BUFFER_SIZE_FILE_NAME];
    DIR *dir;
    static constexpr const char * const TAG = "dir_walker";
};




#endif