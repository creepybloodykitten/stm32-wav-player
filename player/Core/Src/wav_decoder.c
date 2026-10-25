#include "wav_decoder.h"
#include "logger.h"
#include <string.h> 

typedef struct {
    // RIFF Chunk
    char     RIFF_ID[4];        
    uint32_t RIFF_Size;         
    char     WAVE_ID[4];        

    // fmt Sub-chunk
    char     fmt_ID[4];         
    uint32_t fmt_Size;          
    uint16_t AudioFormat;       
    uint16_t NumChannels;       
    uint32_t SampleRate;        
    uint32_t ByteRate;          
    uint16_t BlockAlign;        
    uint16_t BitsPerSample;     

    // data Sub-chunk
    char     data_ID[4];        
    uint32_t data_Size;         

} WAV_Header_t;


static FIL* current_file = NULL;
static AudioInfo audio_info;



static int wav_can_handle(const char* filename) 
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".wav") == 0);
}



static int wav_open(FIL* file, AudioInfo* info, const char* filename) {
    UINT bytes_read;
    char chunk_id[4];
    uint32_t chunk_size;

    info->title[0] = '\0';
    info->artist[0] = '\0';

    f_lseek(file, 0); 
    char riff_header[12];
    f_read(file, riff_header, 12, &bytes_read);
    if (bytes_read < 12 || strncmp(riff_header, "RIFF", 4) != 0 || strncmp(riff_header + 8, "WAVE", 4) != 0) {
        LOG_Printf("[WAV] Error: Not a valid RIFF/WAVE file.\r\n");
        return -1;
    }

    long data_chunk_pos = -1;       
    uint32_t data_chunk_size = 0;  
    int fmt_chunk_found = 0;        


    while (1) {
        long chunk_start_pos = f_tell(file);

        if (f_read(file, chunk_id, 4, &bytes_read) != FR_OK || bytes_read < 4) break; 
        if (f_read(file, &chunk_size, 4, &bytes_read) != FR_OK || bytes_read < 4) break; 

        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_format;
            f_read(file, &audio_format, 2, &bytes_read); 
            f_read(file, &info->num_channels, 2, &bytes_read);
            f_read(file, &info->sample_rate, 4, &bytes_read);
            f_lseek(file, f_tell(file) + 6);
            f_read(file, &info->bits_per_sample, 2, &bytes_read);
            fmt_chunk_found = 1;

            f_lseek(file, chunk_start_pos + 8 + chunk_size);
        }
        else if (strncmp(chunk_id, "data", 4) == 0) {
            data_chunk_size = chunk_size;
            data_chunk_pos = f_tell(file); 

            f_lseek(file, data_chunk_pos + data_chunk_size);
        }
        else if (strncmp(chunk_id, "LIST", 4) == 0) {
            char list_type[4];
            f_read(file, list_type, 4, &bytes_read);
            if (strncmp(list_type, "INFO", 4) == 0) {
                long list_end_pos = f_tell(file) + chunk_size - 4;
                while(f_tell(file) < list_end_pos) {
                    char sub_chunk_id[4];
                    uint32_t sub_chunk_size;
                    
                    if (f_read(file, sub_chunk_id, 4, &bytes_read) != FR_OK || bytes_read < 4) break;
                    if (f_read(file, &sub_chunk_size, 4, &bytes_read) != FR_OK || bytes_read < 4) break;

                    uint32_t seek_size = sub_chunk_size;
                    if (seek_size % 2 != 0) {
                        seek_size++;
                    }

                    if (strncmp(sub_chunk_id, "INAM", 4) == 0) { // INAM = Title
                        f_read(file, info->title, sizeof(info->title) - 1, &bytes_read);
                        info->title[bytes_read] = '\0'; 
                    } 
                    else if (strncmp(sub_chunk_id, "IART", 4) == 0) { // IART = Artist
                        f_read(file, info->artist, sizeof(info->artist) - 1, &bytes_read);
                        info->artist[bytes_read] = '\0';
                    }
                    

                    f_lseek(file, f_tell(file) + seek_size - sub_chunk_size);
                }
            }

            f_lseek(file, chunk_start_pos + 8 + chunk_size);
        }
        else {

            f_lseek(file, f_tell(file) + chunk_size);
        }

        if (chunk_size % 2 != 0) {
            f_lseek(file, f_tell(file) + 1);
        }

        if(fmt_chunk_found && data_chunk_pos != -1) {
            break;
        }
    }

    if (!fmt_chunk_found || data_chunk_pos == -1) {
        if(data_chunk_pos == -1) LOG_Printf("[WAV] Error: 'data' chunk not found.\r\n");
        else LOG_Printf("[WAV] Error: 'fmt ' chunk not found.\r\n");
        return -1;
    }
    

    uint32_t byte_rate = info->sample_rate * info->num_channels * (info->bits_per_sample / 8);
    uint32_t duration_seconds = 0;
    if (byte_rate > 0) {
        duration_seconds = data_chunk_size / byte_rate;
    }
    uint32_t min = duration_seconds / 60;
    uint32_t sec = duration_seconds % 60;

    LOG_Printf("\r\n--- Track Info ---\r\n");
    LOG_Printf("  File:     %s\r\n", filename);
    if(info->artist[0] != '\0') LOG_Printf("  Artist:   %s\r\n", info->artist);
    if(info->title[0] != '\0')  LOG_Printf("  Title:    %s\r\n", info->title);
    LOG_Printf("  Format:   WAV (PCM)\r\n");
    LOG_Printf("  Sample:   %lu Hz, %u bit, %u ch\r\n", info->sample_rate, info->bits_per_sample, info->num_channels);
    LOG_Printf("  Duration: %02lu:%02lu\r\n", min, sec);
    LOG_Printf("------------------\r\n");

    f_lseek(file, data_chunk_pos);
    
    current_file = file; 
    return 0; 
}




static uint32_t wav_decode(void* pcm_buffer, uint32_t bytes_to_decode) {
    UINT bytes_read = 0;
    if (!current_file) return 0;
    f_read(current_file, pcm_buffer, bytes_to_decode, &bytes_read);
    return bytes_read;
}

static void wav_close(void) {

    current_file = NULL;
}

const Decoder wav_decoder = {
    .name = "WAV Decoder",
    .can_handle = wav_can_handle,
    .open = wav_open,
    .decode = wav_decode,
    .close = wav_close
};
