#include "player.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "wav_decoder.h"
#include "fatfs_sd.h"

#define AUDIO_BUFFER_SIZE  32768
extern I2S_HandleTypeDef hi2s2;
extern SPI_HandleTypeDef hspi1;
//extern const Decoder wav_decoder;


static Song playlist[MAX_SONGS]; 
int song_count = 0;
static FIL current_file;     
static int current_track = -1; 
static uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
static volatile bool refill_half = false;       // Флаг: пора заполнять 1-ю половину
static volatile bool refill_full = false;       // Флаг: пора заполнять 2-ю половину
static bool is_playing = false;                 // плеер активен?

static bool is_paused = false;
static uint8_t current_volume = 50; // Громкость по умолчанию 50%

static const Decoder* current_decoder = NULL;

// --- Переменные для Hot-Swap ---
extern FATFS fs; // из main.c
static bool sd_is_mounted = true;
static uint32_t last_sd_check = 0;

void Player_Init(void) {
    song_count = 0;
    LOG_Printf("\r\n[Player] Scanning SD card for audio files...\r\n");

    
    DIR dir;
    FILINFO fno;
    if (f_findfirst(&dir, &fno, "", "*.wav") == FR_OK){
        while (fno.fname[0]) {
            if (!(fno.fattrib & AM_DIR) && (song_count < MAX_SONGS)) {
                strcpy(playlist[song_count].name, fno.fname);
                song_count++;
            }
            if (f_findnext(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        }
        f_closedir(&dir);
    }
    LOG_Printf("[Player] Scan complete. Found %d tracks.\r\n", song_count);
}

void Player_GetDiskInfo(void) {
    FATFS *fs_ptr;
    DWORD fre_clust;
    uint32_t total, free_space;

    f_getfree("", &fre_clust, &fs_ptr);

    total = (uint32_t)((fs_ptr->n_fatent - 2) * fs_ptr->csize * 0.5f);
    free_space = (uint32_t)(fre_clust * fs_ptr->csize * 0.5f);

    LOG_Printf("\r\n--- Disk Info ---\r\n");
    LOG_Printf("Total: %lu MB\r\n", total/1024);
    LOG_Printf("Free:  %lu MB\r\n", free_space/1024);
    LOG_Printf("-----------------\r\n");
}

void Player_ListFiles(void) {
    LOG_Printf("\r\n--- Track List ---\r\n");
    if (song_count == 0) {
        LOG_Printf("No tracks found.\r\n");
    } else {
        for (int i = 0; i < song_count; i++) {
            LOG_Printf("[%d] %s\r\n", i, playlist[i].name);
        }
    }
    LOG_Printf("------------------\r\n");
}


static void ApplyVolume(uint8_t* buffer, uint32_t bytes, uint8_t volume_percent) {
    if (volume_percent > 100) volume_percent = 100;

    // Превращаем массив байт в массив 16-битных чисел (сэмплов)
    int16_t* samples = (int16_t*)buffer;
    uint32_t count = bytes / 2; // 2 байта на один сэмпл

    for (uint32_t i = 0; i < count; i++) {
        // Умножаем сэмпл на процент и делим на 100
        // Используем int32_t для промежуточного расчета, чтобы не было переполнения
        int32_t temp = (int32_t)samples[i] * volume_percent / 100;
        samples[i] = (int16_t)temp;
    }
}

int Player_Play(int track_index) {
    if (track_index < 0 || track_index >= song_count) {
        LOG_Printf("[Player] Error: Invalid track number %d.\r\n", track_index);
        return -1;
    }

    Player_Stop();

    current_track = track_index;
    const char* filename = playlist[current_track].name;

    FRESULT res = f_open(&current_file, filename, FA_READ);
    if (res != FR_OK) {
        LOG_Printf("[Player] Error: Could not open file '%s'. FRESULT: %d\r\n", filename, res);
        current_track = -1;
        // Если ошибка говорит о том, что флешку вытащили прямо во время воспроизведения:
        if (res == FR_NOT_READY || res == FR_DISK_ERR || res == FR_NO_FILESYSTEM) {
		   LOG_Printf("\r\n[SYSTEM] SD Card REMOVED during playback!\r\n");
		   Player_Stop();
		   f_mount(NULL, "", 0);
		   //SD_Eject();
		   sd_is_mounted = false;
		   song_count = 0;
        }
        return -1;
    }

		
		current_decoder = &wav_decoder;


    if (!current_decoder->can_handle(filename)) {
        LOG_Printf("[Player] Error: Unsupported format.\r\n");
        f_close(&current_file);
        current_track = -1;
        return -1;
    }

    AudioInfo info;
    if (current_decoder->open(&current_file, &info, filename) != 0) {
        LOG_Printf("[Player] Error: Decoder failed.\r\n");
        Player_Stop();
        return -1;
    }

    // Проверка частоты (важно, так как PLL настроен жестко)
    if (info.sample_rate != 44100) {
        LOG_Printf("[Player] Warning: File sample rate is %lu Hz (Expected 44100). Speed may vary.\r\n", info.sample_rate);
    }

    LOG_Printf("[Player] Buffering...\r\n");

    // ПРЕДЗАГРУЗКА БУФЕРА
    // Читаем сразу полный буфер данных перед стартом
    uint32_t bytes_read = current_decoder->decode(audio_buffer, AUDIO_BUFFER_SIZE);

    // Если файл слишком короткий (меньше буфера), зануляем остаток
    if (bytes_read < AUDIO_BUFFER_SIZE) {
        memset(audio_buffer + bytes_read, 0, AUDIO_BUFFER_SIZE - bytes_read);
    }

    ApplyVolume(audio_buffer, AUDIO_BUFFER_SIZE, current_volume); //громкость

    // --- ЗАПУСК DMA ---
    HAL_StatusTypeDef status = HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t*)audio_buffer, AUDIO_BUFFER_SIZE / 2);

    if (status != HAL_OK) {
         LOG_Printf("[Player] DMA Start Failed! Error: %d\r\n", status);
         Player_Stop();
         return -1;
    }

    is_playing = true;
    is_paused = false;
    refill_half = false;
    refill_full = false;
    LOG_Printf("[Player] Playback started.\r\n");

    return 0;
}


void Player_ExecuteCommand(const char* cmd) {
    LOG_Printf("\r\n> Command received: %s\r\n", cmd);

    if (strcmp(cmd, "help") == 0) {
        LOG_Printf("Available commands:\r\n");
        LOG_Printf("  help   - Show this message\r\n");
        LOG_Printf("  info   - Show disk info\r\n");
        LOG_Printf("  list   - List all tracks\r\n");
        LOG_Printf("  play X - Select track number X (e.g., 'play 0')\r\n");
        LOG_Printf("  next   - Play next track\r\n");
        LOG_Printf("  prev   - Play previous track\r\n");
        LOG_Printf("  pp     - Pause/Play playback\r\n");
        LOG_Printf("  vol X  - Set volume to X% (e.g., 'vol 50')\r\n");
    }
    else if (strcmp(cmd, "info") == 0) {
        Player_GetDiskInfo();
    }
    else if (strcmp(cmd, "list") == 0) {
        Player_ListFiles();
    }
    else if (strncmp(cmd, "play ", 5) == 0) {

        int track_num = atoi(cmd + 5);
        Player_Play(track_num);
    }
    else if (strcmp(cmd, "next") == 0) {
        Player_Next();
    }
    else if (strcmp(cmd, "prev") == 0) {
        Player_Prev();
    }
    else if (strncmp(cmd, "vol ", 4) == 0) {
        int vol = atoi(cmd + 4);

        if (vol < 0) vol = 0;
        if (vol > 100) vol = 100;

        current_volume = vol;
        LOG_Printf("Volume set to: %d%%\r\n", current_volume);
    }
    else if (strcmp(cmd, "pp") == 0) {
        Player_TogglePause();
    }
    else {
        LOG_Printf("Unknown command. Type 'help' for a list of commands.\r\n");
    }
}




void Player_Stop(void) {
    if (is_playing) {
        HAL_I2S_DMAStop(&hi2s2); // Остановить DMA
        is_playing = false;
    }

    if (current_decoder) {
        current_decoder->close(); // Закрыть файл внутри декодера
        current_decoder = NULL;
    }

    // На всякий случай закрываем файл fatfs
    f_close(&current_file);
    current_track = -1;
    LOG_Printf("[Player] Stopped.\r\n");
}


void Player_Process(void) {
    if (!is_playing || current_decoder == NULL) return;

    uint32_t bytes_read = 0;

    //ЗАПОЛНЕНИЕ ПЕРВОЙ ПОЛОВИНЫ
    if (refill_half) {
        refill_half = false; // Сбрасываем флаг

        // Читаем в начало массива (адрес &audio_buffer[0])
        // Размер половины = AUDIO_BUFFER_SIZE / 2
        bytes_read = current_decoder->decode(&audio_buffer[0], AUDIO_BUFFER_SIZE / 2);
        ApplyVolume(&audio_buffer[0], AUDIO_BUFFER_SIZE / 2, current_volume);

        // Если файл кончился (прочитали 0 или меньше чем просили)
        if (bytes_read < AUDIO_BUFFER_SIZE / 2) {
             // Заполняем остаток нулями (тишиной), чтобы не щелкало
             memset(&audio_buffer[bytes_read], 0, (AUDIO_BUFFER_SIZE / 2) - bytes_read);

             if (bytes_read == 0) {
                 LOG_Printf("[Player] Track finished.\r\n");
                 //Player_Stop();
                 Player_Next();
                 return;
             }
        }
    }

    // --- ЗАПОЛНЕНИЕ ВТОРОЙ ПОЛОВИНЫ ---
    if (refill_full) {
        refill_full = false; // Сбрасываем флаг

        // Читаем в середину массива (адрес &audio_buffer[AUDIO_BUFFER_SIZE / 2])
        bytes_read = current_decoder->decode(&audio_buffer[AUDIO_BUFFER_SIZE / 2], AUDIO_BUFFER_SIZE / 2);
        ApplyVolume(&audio_buffer[AUDIO_BUFFER_SIZE / 2], AUDIO_BUFFER_SIZE / 2, current_volume);

        if (bytes_read < AUDIO_BUFFER_SIZE / 2) {
             memset(&audio_buffer[(AUDIO_BUFFER_SIZE / 2) + bytes_read], 0, (AUDIO_BUFFER_SIZE / 2) - bytes_read);

             if (bytes_read == 0) {
                 LOG_Printf("[Player] Track finished.\r\n");
                 //Player_Stop();
                 Player_Next();
                 return;
             }

        }
    }
}


void Player_TogglePause(void) {
    if (!is_playing) return;

    if (!is_paused) {
        HAL_I2S_DMAPause(&hi2s2);
        is_paused = true;
        LOG_Printf("[Player] Paused.\r\n");
    } else {
        HAL_I2S_DMAResume(&hi2s2);
        is_paused = false;
        LOG_Printf("[Player] Resumed.\r\n");
    }
}

void Player_Next(void) {
    if (song_count == 0) return;
    int next_track = current_track + 1;
    if (next_track >= song_count) {
        next_track = 0; // Начинаем сначала, если песни кончились
    }
    Player_Play(next_track);
}

void Player_Prev(void) {
    if (song_count == 0) return;
    int prev_track = current_track - 1;
    if (prev_track < 0) {
        prev_track = song_count - 1; // Возврат на последнюю
    }
    Player_Play(prev_track);
}


void Button_Process(void) {
    static GPIO_PinState last_state = GPIO_PIN_SET;
    static uint32_t btn_press_time = 0;   // Когда нажали кнопку
    static uint32_t last_release_time = 0; // Когда отпустили кнопку
    static uint32_t last_vol_tick = 0;    // Для плавного изменения громкости
    static uint8_t click_count = 0;       // Счетчик накопленных кликов
    static bool hold_executed = false;    // Флаг, что удержание уже сработало

    GPIO_PinState current_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);
    uint32_t now = HAL_GetTick();

    //событие:нажатие кнопки
    if (current_state == GPIO_PIN_RESET && last_state == GPIO_PIN_SET) {
        btn_press_time = now;
        hold_executed = false;
        // Если между прошлым отпусканием и этим нажатием прошло мало времени - это серия
        if (now - last_release_time > 400) {
            click_count = 0; // Слишком долго ждали, сбрасываем счетчик кликов
        }
    }

    // событие: удержание
    if (current_state == GPIO_PIN_RESET) {
        if (now - btn_press_time > 400) { // Если держим дольше 0.4 сек
            if (now - last_vol_tick > 150) { // Шаг изменения громкости каждые 150мс
                last_vol_tick = now;
                hold_executed = true; // Помечаем, что это не просто клик

                if (click_count == 0) {
                    // Обычное удержание -> Громкость ВВЕРХ
                    if (current_volume < 100) current_volume += 5;
                    LOG_Printf("Volume UP: %d%%\r\n", current_volume);
                }
                else if (click_count == 1) {
                    // Клик + Удержание -> Громкость ВНИЗ
                    if (current_volume > 0) current_volume -= 5;
                    LOG_Printf("Volume DOWN: %d%%\r\n", current_volume);
                }
            }
        }
    }

    // событие: отпускание кнопки
    if (current_state == GPIO_PIN_SET && last_state == GPIO_PIN_RESET) {
        last_release_time = now;

        if (!hold_executed) {
            // Если не удерживали кнопку для громкости, значит это просто короткий клик
            if (now - btn_press_time > 20) { // Защита от дребезга
                click_count++;
            }
        } else {
            // Если закончили изменять громкость, обнуляем серию кликов
            click_count = 0;
        }
    }

    // обработка коротких кликов (ждем паузу после отпускания)
    // Если кнопка отпущена и прошло 350мс, а удержания не было - выполняем команду
    if (current_state == GPIO_PIN_SET && click_count > 0 && (now - last_release_time > 350)) {
        if (click_count == 1) {
            Player_TogglePause();
        } else if (click_count == 2) {
            Player_Next();
        } else if (click_count >= 3) {
            Player_Prev();
        }
        click_count = 0; // Сброс после выполнения
    }

    last_state = current_state;
}


void Player_HandleHotSwap(void) {
    uint32_t now = HAL_GetTick();

    // Проверяем статус раз в 1 секунду
    if (now - last_sd_check > 1000) {
        last_sd_check = now;

        if (sd_is_mounted) {
            // Если играет музыка, ошибки выявит f_read. Но если пауза/стоп:
            if (!is_playing || is_paused) {
                DIR dir;
                // Пытаемся открыть корень. Если не вышло - карту вытащили
                if (f_opendir(&dir, "/") != FR_OK) {
                    LOG_Printf("\r\n[SYSTEM] SD Card REMOVED!\r\n");
                    Player_Stop();
                    f_mount(NULL, "", 0); // Отмонтируем
                    //SD_Eject();
                    sd_is_mounted = false;
                    song_count = 0;

                    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
                } else {
                    f_closedir(&dir);
                }
            }
        } else {
        	HAL_Delay(1000);

			LOG_Printf("\r\n[SYSTEM] Attempting to mount...\r\n");

			// 2. Официальный и самый чистый сброс SPI библиотеки HAL
			HAL_SPI_DeInit(&hspi1);
			HAL_Delay(10);
			HAL_SPI_Init(&hspi1);

			// 3. Забываем старую карту (скидываем статус в fatfs_sd.c)
			SD_Eject();

			// 4. "Массаж" флешки перед запуском
			// Поднимаем CS и отправляем 200 тактов. Это будит любой зависший контроллер внутри SD.
			HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
			uint8_t dummy = 0xFF;
			for(int i = 0; i < 25; i++) {
				HAL_SPI_Transmit(&hspi1, &dummy, 1, 10);
			}

			// 5. Пытаемся примонтировать!
			FRESULT res = f_mount(&fs, "", 1);

			if (res == FR_OK) {
				LOG_Printf("[SYSTEM] SD Card INSERTED successfully!\r\n");
				sd_is_mounted = true;
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);

				Player_Init();
				if (song_count > 0) {
					Player_Play(0);
					Player_TogglePause();
				}
			} else {

				LOG_Printf("[SYSTEM] Mount failed! FRESULT code: %d\r\n", res);
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);

				//  сбросим таймер
				last_sd_check = HAL_GetTick();
			}
        }
    }
}

// Вызывается, когда проиграла ПЕРВАЯ половина буфера
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == SPI2) {
        refill_half = true; // 1-я половина свободна
    }
}

// Вызывается, когда проиграла ВТОРАЯ половина буфера (конец)
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == SPI2) {
        refill_full = true; //  2-я половина свободна
    }
}



