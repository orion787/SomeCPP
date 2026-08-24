//https://chatgpt.com/share/6a8c92de-502c-83eb-ba92-76db852c1322
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/i2c_slave.h"

#include "esp_log.h"
#include "esp_err.h"


/* ============================================================
 *                      CONFIGURATION
 * ============================================================ */

#define TAG "DUAL_I2C_SLAVE"

/*
 * Одна физическая I2C-шина.
 *
 * I2C0 -> slave 0x30
 * I2C1 -> slave 0x31
 */

#define I2C_SDA_GPIO               GPIO_NUM_21
#define I2C_SCL_GPIO               GPIO_NUM_22

#define I2C_SLAVE_1_PORT           I2C_NUM_0
#define I2C_SLAVE_2_PORT           I2C_NUM_1

#define I2C_SLAVE_1_ADDRESS        0x30
#define I2C_SLAVE_2_ADDRESS        0x31

/*
 * Максимальное количество байт, которое slave может
 * подготовить для master.
 */
#define I2C_SEND_BUFFER_SIZE       32

/*
 * У каждого виртуального устройства 16 пинов.
 *
 * 16 bits = 2 bytes.
 */
#define PINS_PER_SLAVE             16
#define BYTES_PER_SLAVE            2


/*
 * GPIO уведомления внешнего master.
 *
 * ALERT1 -> slave 0x30
 * ALERT2 -> slave 0x31
 */
#define ALERT_1_GPIO               GPIO_NUM_18
#define ALERT_2_GPIO               GPIO_NUM_19

#define ALERT_PULSE_MS             5


/*
 * Hash table.
 */
#define HASH_TABLE_SIZE            16


/*
 * Максимальная длина ключа.
 *
 * Например:
 *
 * "KEY_A"
 * "KEY_VOLUME_UP"
 * "KEY_ENTER"
 */
#define MAX_KEY_LENGTH             32


/*
 * Очередь пользовательских команд.
 *
 * В будущем сюда будет писать TCP/HTTP/WebSocket
 * сервер.
 */
#define INPUT_QUEUE_LENGTH         8


/*
 * Сейчас вместо телефона используем
 * заранее подготовленные 5 команд.
 */
#define TEST_INPUT_COUNT           5


/* ============================================================
 *                       I2C SLAVE
 * ============================================================ */

typedef struct
{
    i2c_slave_dev_handle_t handle;

    uint8_t address;

    gpio_num_t alert_gpio;

    const char *name;

} i2c_slave_context_t;


static i2c_slave_context_t slave1 =
{
    .handle = NULL,
    .address = I2C_SLAVE_1_ADDRESS,
    .alert_gpio = ALERT_1_GPIO,
    .name = "SLAVE_1"
};


static i2c_slave_context_t slave2 =
{
    .handle = NULL,
    .address = I2C_SLAVE_2_ADDRESS,
    .alert_gpio = ALERT_2_GPIO,
    .name = "SLAVE_2"
};


/* ============================================================
 *                    INPUT DEFINITION
 * ============================================================ */

/*
 * Описание заранее известного пользовательского ввода.
 *
 * Например:
 *
 * "KEY_A"
 *
 * означает:
 *
 * slave 0x30
 * pin_state = 0x0080
 *
 * то есть:
 *
 * pin 7 = 1
 * остальные = 0
 *
 * И, соответственно:
 *
 * 0x0080 -> 0x80 0x00
 */

typedef struct
{
    uint8_t slave_num;

    uint16_t pin_state;

    uint8_t data[BYTES_PER_SLAVE];

    size_t data_len;

} input_definition_t;


/* ============================================================
 *                       HASH TABLE
 * ============================================================ */

typedef struct hash_node
{
    char *key;

    input_definition_t value;

    struct hash_node *next;

} hash_node_t;


typedef struct
{
    hash_node_t *buckets[HASH_TABLE_SIZE];

    size_t count;

} hash_table_t;


static hash_table_t input_table;


/* ============================================================
 *                    HASH FUNCTION
 * ============================================================ */

/*
 * djb2
 *
 * Простая и достаточно хорошая hash-функция
 * для коротких строковых ключей.
 */
static uint32_t hash_string(
    const char *key)
{
    uint32_t hash = 5381;

    while (*key != '\0')
    {
        hash =
            ((hash << 5) + hash)
            ^ (uint8_t)(*key);

        key++;
    }

    return hash;
}


/* ============================================================
 *                    HASH TABLE INIT
 * ============================================================ */

static void hash_table_init(
    hash_table_t *table)
{
    if (table == NULL)
    {
        return;
    }

    memset(
        table,
        0,
        sizeof(*table)
    );
}


/* ============================================================
 *                     HASH INSERT
 * ============================================================ */

static esp_err_t hash_table_insert(
    hash_table_t *table,
    const char *key,
    const input_definition_t *value)
{
    if (table == NULL ||
        key == NULL ||
        value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    size_t key_length =
        strlen(key);


    if (key_length == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (key_length >= MAX_KEY_LENGTH)
    {
        ESP_LOGE(
            TAG,
            "Key is too long: %s",
            key
        );

        return ESP_ERR_INVALID_SIZE;
    }


    uint32_t hash =
        hash_string(key);


    size_t bucket =
        hash % HASH_TABLE_SIZE;


    /*
     * Проверяем, существует ли уже такой ключ.
     *
     * Это важно: наша таблица должна вести себя
     * как dictionary:
     *
     * key -> value
     *
     * а не как список одинаковых ключей.
     */
    hash_node_t *node =
        table->buckets[bucket];


    while (node != NULL)
    {
        if (strcmp(node->key, key) == 0)
        {
            /*
             * Такой ключ уже существует.
             *
             * Обновляем его значение.
             */
            node->value = *value;

            ESP_LOGI(
                TAG,
                "Updated input definition: %s",
                key
            );

            return ESP_OK;
        }

        node = node->next;
    }


    /*
     * Новый элемент.
     */
    node =
        calloc(
            1,
            sizeof(hash_node_t)
        );


    if (node == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to allocate hash node"
        );

        return ESP_ERR_NO_MEM;
    }


    node->key =
        strdup(key);


    if (node->key == NULL)
    {
        free(node);

        ESP_LOGE(
            TAG,
            "Failed to allocate key"
        );

        return ESP_ERR_NO_MEM;
    }


    node->value = *value;


    /*
     * Chaining.
     */
    node->next =
        table->buckets[bucket];

    table->buckets[bucket] =
        node;

    table->count++;


    ESP_LOGI(
        TAG,
        "Added input definition: %s -> bucket %zu",
        key,
        bucket
    );


    return ESP_OK;
}


/* ============================================================
 *                       HASH FIND
 * ============================================================ */

static const input_definition_t *
hash_table_find(
    const hash_table_t *table,
    const char *key)
{
    if (table == NULL ||
        key == NULL)
    {
        return NULL;
    }


    uint32_t hash =
        hash_string(key);


    size_t bucket =
        hash % HASH_TABLE_SIZE;


    hash_node_t *node =
        table->buckets[bucket];


    while (node != NULL)
    {
        if (strcmp(node->key, key) == 0)
        {
            return &node->value;
        }

        node = node->next;
    }


    return NULL;
}


/* ============================================================
 *                     HASH TABLE DESTROY
 * ============================================================ */

static void hash_table_destroy(
    hash_table_t *table)
{
    if (table == NULL)
    {
        return;
    }


    for (size_t i = 0;
         i < HASH_TABLE_SIZE;
         i++)
    {
        hash_node_t *node =
            table->buckets[i];


        while (node != NULL)
        {
            hash_node_t *next =
                node->next;

            free(node->key);
            free(node);

            node = next;
        }


        table->buckets[i] = NULL;
    }


    table->count = 0;
}


/* ============================================================
 *              CONVERT PIN STATE TO I2C DATA
 * ============================================================ */

static void pin_state_to_bytes(
    uint16_t pin_state,
    uint8_t *data)
{
    if (data == NULL)
    {
        return;
    }


    /*
     * Little-endian.
     *
     * 0x0080 -> 80 00
     * 0x0088 -> 88 00
     * 0x0101 -> 01 01
     * 0x8083 -> 83 80
     */

    data[0] =
        (uint8_t)(pin_state & 0xFF);

    data[1] =
        (uint8_t)((pin_state >> 8) & 0xFF);
}


/* ============================================================
 *                     PRINT I2C DATA
 * ============================================================ */

static void print_data(
    const uint8_t *data,
    size_t len)
{
    if (data == NULL)
    {
        return;
    }


    for (size_t i = 0;
         i < len;
         i++)
    {
        printf(
            "0x%02X",
            data[i]
        );

        if (i + 1 < len)
        {
            printf(" ");
        }
    }

    printf("\n");
}


/* ============================================================
 *                       ALERT GPIO
 * ============================================================ */

static void init_alert_gpio(
    gpio_num_t gpio)
{
    gpio_config_t config =
    {
        .pin_bit_mask =
            (1ULL << gpio),

        .mode =
            GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };


    ESP_ERROR_CHECK(
        gpio_config(&config)
    );


    ESP_ERROR_CHECK(
        gpio_set_level(
            gpio,
            0
        )
    );
}


/* ============================================================
 *                     SEND ALERT
 * ============================================================ */

static void send_alert(
    i2c_slave_context_t *slave)
{
    ESP_LOGI(
        TAG,
        "%s: ALERT",
        slave->name
    );


    gpio_set_level(
        slave->alert_gpio,
        1
    );


    vTaskDelay(
        pdMS_TO_TICKS(
            ALERT_PULSE_MS
        )
    );


    gpio_set_level(
        slave->alert_gpio,
        0
    );
}


/* ============================================================
 *                       I2C INIT
 * ============================================================ */

static esp_err_t init_i2c_slave(
    i2c_slave_context_t *slave,
    i2c_port_num_t port)
{
    i2c_slave_config_t config =
    {
        .i2c_port =
            port,

        .sda_io_num =
            I2C_SDA_GPIO,

        .scl_io_num =
            I2C_SCL_GPIO,

        .clk_source =
            I2C_CLK_SRC_DEFAULT,

        .send_buf_depth =
            I2C_SEND_BUFFER_SIZE,

        .receive_buf_depth =
            16,

        .slave_addr =
            slave->address,

        .addr_bit_len =
            I2C_ADDR_BIT_LEN_7,

        .intr_priority =
            0,

        .allow_pd =
            false,

        .flags =
        {
            /*
             * Используем внешние pull-up.
             */
            .enable_internal_pullup =
                false
        }
    };


    esp_err_t err =
        i2c_new_slave_device(
            &config,
            &slave->handle
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "%s: I2C initialization failed: %s",
            slave->name,
            esp_err_to_name(err)
        );

        return err;
    }


    ESP_LOGI(
        TAG,
        "%s initialized: address=0x%02X",
        slave->name,
        slave->address
    );


    return ESP_OK;
}


/* ============================================================
 *                  PREPARE SLAVE RESPONSE
 * ============================================================ */

static esp_err_t prepare_slave_response(
    i2c_slave_context_t *slave,
    const input_definition_t *definition)
{
    if (slave == NULL ||
        definition == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (slave->handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }


    if (definition->data_len == 0 ||
        definition->data_len >
            I2C_SEND_BUFFER_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    ESP_LOGI(
        TAG,
        "%s: preparing response",
        slave->name
    );


    ESP_LOGI(
        TAG,
        "Pin state = 0x%04X",
        definition->pin_state
    );


    ESP_LOGI(
        TAG,
        "I2C data:"
    );


    printf("    ");
    print_data(
        definition->data,
        definition->data_len
    );


    /*
     * ВАЖНО:
     *
     * ESP32 сейчас НЕ делает I2C WRITE.
     *
     * Мы помещаем данные в TX buffer slave.
     *
     * Внешний MASTER впоследствии выполнит READ.
     */
    esp_err_t err =
        i2c_slave_transmit(
            slave->handle,
            definition->data,
            definition->data_len,
            100
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "%s: failed to prepare response: %s",
            slave->name,
            esp_err_to_name(err)
        );

        return err;
    }


    return ESP_OK;
}


/* ============================================================
 *                  PROCESS STRING INPUT
 * ============================================================ */

/*
 * ЭТО ГЛАВНАЯ ФУНКЦИЯ АРХИТЕКТУРЫ.
 *
 * В будущем сервер сможет просто сделать:
 *
 *     process_user_input("KEY_A");
 *
 * или:
 *
 *     process_user_input("KEY_ENTER");
 *
 * Ему не нужно знать:
 *
 *     0x30
 *     0x31
 *     pin_state
 *     I2C
 *     GPIO
 *     ALERT
 */
static esp_err_t process_user_input(
    const char *key)
{
    if (key == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    ESP_LOGI(
        TAG,
        "============================================"
    );

    ESP_LOGI(
        TAG,
        "USER INPUT: \"%s\"",
        key
    );


    /*
     * Быстрый поиск заранее вычисленного
     * пользовательского ввода.
     */
    const input_definition_t *definition =
        hash_table_find(
            &input_table,
            key
        );


    if (definition == NULL)
    {
        ESP_LOGW(
            TAG,
            "Unknown user input: \"%s\"",
            key
        );

        return ESP_ERR_NOT_FOUND;
    }


    ESP_LOGI(
        TAG,
        "Input found in hash table"
    );


    i2c_slave_context_t *slave;


    if (definition->slave_num == 1)
    {
        slave = &slave1;
    }
    else if (definition->slave_num == 2)
    {
        slave = &slave2;
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Invalid slave number: %u",
            definition->slave_num
        );

        return ESP_ERR_INVALID_ARG;
    }


    /*
     * 1.
     *
     * Кладем заранее вычисленный ответ
     * в TX buffer нужного slave.
     */
    esp_err_t err =
        prepare_slave_response(
            slave,
            definition
        );


    if (err != ESP_OK)
    {
        return err;
    }


    /*
     * 2.
     *
     * Только после подготовки данных
     * уведомляем внешний master.
     */
    send_alert(slave);


    ESP_LOGI(
        TAG,
        "Slave 0x%02X is waiting for MASTER READ",
        slave->address
    );


    return ESP_OK;
}


/* ============================================================
 *                   BUILD INPUT HASH TABLE
 * ============================================================ */

/*
 * Все возможные пользовательские вводы.
 *
 * Это единственное место, которое в будущем нужно
 * расширять при добавлении новых команд.
 */
static void build_input_table(void)
{
    hash_table_init(
        &input_table
    );


    /*
     * KEY_A
     *
     * Slave 0x30
     * pin 7
     *
     * 0x0080
     *
     * data = 80 00
     */
    input_definition_t key_a =
    {
        .slave_num = 1,

        .pin_state = 0x0080,

        .data = { 0x80, 0x00 },

        .data_len = 2
    };


    /*
     * KEY_B
     *
     * Slave 0x31
     *
     * pins 3 + 7
     *
     * 0x0088
     *
     * data = 88 00
     */
    input_definition_t key_b =
    {
        .slave_num = 2,

        .pin_state = 0x0088,

        .data = { 0x88, 0x00 },

        .data_len = 2
    };


    /*
     * KEY_C
     *
     * Slave 0x30
     *
     * pins 0 + 8
     *
     * 0x0101
     */
    input_definition_t key_c =
    {
        .slave_num = 1,

        .pin_state = 0x0101,

        .data = { 0x01, 0x01 },

        .data_len = 2
    };


    /*
     * KEY_D
     *
     * Slave 0x31
     *
     * pins 4 + 5 + 12
     *
     * 0x1030
     */
    input_definition_t key_d =
    {
        .slave_num = 2,

        .pin_state = 0x1030,

        .data = { 0x30, 0x10 },

        .data_len = 2
    };


    /*
     * KEY_E
     *
     * Slave 0x30
     *
     * pins 0 + 1 + 7 + 15
     *
     * 0x8083
     */
    input_definition_t key_e =
    {
        .slave_num = 1,

        .pin_state = 0x8083,

        .data = { 0x83, 0x80 },

        .data_len = 2
    };


    ESP_ERROR_CHECK(
        hash_table_insert(
            &input_table,
            "KEY_A",
            &key_a
        )
    );


    ESP_ERROR_CHECK(
        hash_table_insert(
            &input_table,
            "KEY_B",
            &key_b
        )
    );


    ESP_ERROR_CHECK(
        hash_table_insert(
            &input_table,
            "KEY_C",
            &key_c
        )
    );


    ESP_ERROR_CHECK(
        hash_table_insert(
            &input_table,
            "KEY_D",
            &key_d
        )
    );


    ESP_ERROR_CHECK(
        hash_table_insert(
            &input_table,
            "KEY_E",
            &key_e
        )
    );


    ESP_LOGI(
        TAG,
        "Input hash table initialized: %zu entries",
        input_table.count
    );
}


/* ============================================================
 *                    INPUT QUEUE
 * ============================================================ */

static QueueHandle_t input_queue;


/*
 * Максимальная длина команды, которую может
 * прислать будущий сервер.
 */
typedef struct
{
    char key[MAX_KEY_LENGTH];

} input_message_t;


/* ============================================================
 *                   INPUT PROCESSING TASK
 * ============================================================ */

static void input_processing_task(
    void *arg)
{
    (void)arg;


    input_message_t message;


    while (true)
    {
        /*
         * Ожидаем строку.
         *
         * В будущем сюда будет попадать:
         *
         * "KEY_A"
         * "KEY_B"
         * "KEY_ENTER"
         * ...
         */
        if (xQueueReceive(
                input_queue,
                &message,
                portMAX_DELAY) == pdTRUE)
        {
            process_user_input(
                message.key
            );
        }
    }
}


/* ============================================================
 *                  SIMULATED USER TASK
 * ============================================================ */

/*
 * Сейчас эта задача изображает телефон/пользователя.
 *
 * В будущем ее можно полностью удалить,
 * а input_queue будет заполняться сервером.
 */
static void simulated_user_task(
    void *arg)
{
    (void)arg;


    static const char *test_inputs[
        TEST_INPUT_COUNT
    ] =
    {
        "KEY_A",
        "KEY_B",
        "KEY_C",
        "KEY_D",
        "KEY_E"
    };


    for (int i = 0;
         i < TEST_INPUT_COUNT;
         i++)
    {
        input_message_t message;


        memset(
            &message,
            0,
            sizeof(message)
        );


        strncpy(
            message.key,
            test_inputs[i],
            MAX_KEY_LENGTH - 1
        );


        ESP_LOGI(
            TAG,
            "Simulated user -> \"%s\"",
            message.key
        );


        /*
         * Вместо непосредственного вызова
         * process_user_input()
         *
         * отправляем команду в очередь.
         *
         * Это очень важно для будущего сервера.
         */
        if (xQueueSend(
                input_queue,
                &message,
                pdMS_TO_TICKS(100)
            ) != pdTRUE)
        {
            ESP_LOGE(
                TAG,
                "Input queue is full"
            );
        }


        /*
         * Просто пауза между тестовыми
         * пользовательскими вводами.
         */
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }


    ESP_LOGI(
        TAG,
        "All simulated inputs sent"
    );


    while (true)
    {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}


/* ============================================================
 *                           APP MAIN
 * ============================================================ */

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "============================================"
    );

    ESP_LOGI(
        TAG,
        "Dual I2C Slave Emulator"
    );

    ESP_LOGI(
        TAG,
        "String-based input architecture"
    );

    ESP_LOGI(
        TAG,
        "============================================"
    );


    /*
     * --------------------------------------------------------
     * 1. Создаем таблицу заранее известных вводов.
     * --------------------------------------------------------
     */
    build_input_table();


    /*
     * --------------------------------------------------------
     * 2. ALERT GPIO
     * --------------------------------------------------------
     */
    init_alert_gpio(
        ALERT_1_GPIO
    );

    init_alert_gpio(
        ALERT_2_GPIO
    );


    /*
     * --------------------------------------------------------
     * 3. Реальный I2C slave 0x30.
     * --------------------------------------------------------
     */
    ESP_ERROR_CHECK(
        init_i2c_slave(
            &slave1,
            I2C_SLAVE_1_PORT
        )
    );


    /*
     * --------------------------------------------------------
     * 4. Реальный I2C slave 0x31.
     * --------------------------------------------------------
     */
    ESP_ERROR_CHECK(
        init_i2c_slave(
            &slave2,
            I2C_SLAVE_2_PORT
        )
    );


    /*
     * --------------------------------------------------------
     * 5. Queue между сервером/источником ввода
     *    и обработчиком.
     * --------------------------------------------------------
     */
    input_queue =
        xQueueCreate(
            INPUT_QUEUE_LENGTH,
            sizeof(input_message_t)
        );


    if (input_queue == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to create input queue"
        );

        abort();
    }


    /*
     * --------------------------------------------------------
     * 6. Task обработки команд.
     * --------------------------------------------------------
     */
    BaseType_t result =
        xTaskCreate(
            input_processing_task,
            "input_processor",
            4096,
            NULL,
            6,
            NULL
        );


    if (result != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "Failed to create input processor task"
        );

        abort();
    }


    /*
     * --------------------------------------------------------
     * 7. Пока вместо телефона запускаем тестовый источник.
     * --------------------------------------------------------
     */
    result =
        xTaskCreate(
            simulated_user_task,
            "simulated_user",
            4096,
            NULL,
            5,
            NULL
        );


    if (result != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "Failed to create simulated user task"
        );

        abort();
    }


    ESP_LOGI(
        TAG,
        "============================================"
    );

    ESP_LOGI(
        TAG,
        "System ready"
    );

    ESP_LOGI(
        TAG,
        "Slave 0x30 -> ALERT GPIO%d",
        ALERT_1_GPIO
    );

    ESP_LOGI(
        TAG,
        "Slave 0x31 -> ALERT GPIO%d",
        ALERT_2_GPIO
    );

    ESP_LOGI(
        TAG,
        "============================================"
    );
}
