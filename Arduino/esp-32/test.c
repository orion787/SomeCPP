#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/i2c_slave.h"

#include "esp_log.h"
#include "esp_err.h"

/* ============================================================
 *                       CONFIGURATION
 * ============================================================ */

/*
 * Одна физическая I2C шина.
 *
 * Оба I2C контроллера ESP32 подключаются к одним SDA/SCL.
 *
 * I2C0 -> slave 0x30
 * I2C1 -> slave 0x31
 */

#define I2C_SDA_GPIO                GPIO_NUM_21
#define I2C_SCL_GPIO                GPIO_NUM_22

#define I2C_SLAVE_1_PORT            I2C_NUM_0
#define I2C_SLAVE_2_PORT            I2C_NUM_1

#define I2C_SLAVE_1_ADDRESS         0x30
#define I2C_SLAVE_2_ADDRESS         0x31

#define I2C_SEND_BUFFER_SIZE        32

/*
 * Каждому виртуальному slave соответствует 16 входов.
 *
 * 16 bits = 2 bytes.
 */
#define PINS_PER_SLAVE              16
#define BYTES_PER_SLAVE             2

/*
 * Линии уведомления внешнего master.
 *
 * ALERT_1 = данные появились у slave 0x30
 * ALERT_2 = данные появились у slave 0x31
 */
#define ALERT_1_GPIO                GPIO_NUM_18
#define ALERT_2_GPIO                GPIO_NUM_19

#define ALERT_PULSE_MS              5

/*
 * Количество заранее подготовленных пользовательских событий.
 */
#define TEST_INPUT_COUNT            5

#define TAG "DUAL_I2C_SLAVE"


/* ============================================================
 *                         DATA TYPES
 * ============================================================ */

typedef struct
{
    uint8_t data[BYTES_PER_SLAVE];
    size_t data_len;

    /*
     * Маска 16 виртуальных входов.
     *
     * bit 0  -> pin 0
     * bit 1  -> pin 1
     * ...
     * bit 15 -> pin 15
     */
    uint16_t pin_state;

} slave_data_t;


typedef struct
{
    uint8_t slave_num;

    uint16_t pin_state;

    uint8_t data[BYTES_PER_SLAVE];

    size_t data_len;

} input_event_t;


typedef struct
{
    i2c_slave_dev_handle_t handle;

    uint8_t address;

    gpio_num_t alert_gpio;

    const char *name;

} i2c_slave_context_t;


/* ============================================================
 *                     GLOBAL SLAVE CONTEXT
 * ============================================================ */

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
 *                     PREPARED TEST INPUT
 * ============================================================ */

/*
 * Здесь сейчас вместо реального пользователя находятся
 * заранее подготовленные события.
 *
 * Каждый элемент:
 *
 *   slave_num
 *   pin_state
 *
 * Например:
 *
 * 0x0080 =
 *
 * bit 7 = 1
 *
 * то есть активирован виртуальный pin 7.
 *
 * После этого pin_state превращается в два байта:
 *
 * low byte
 * high byte
 *
 * Например:
 *
 * 0x0080 -> 0x80 0x00
 *
 * Если master читает slave 0x30, он получит эти два байта.
 */

static const input_event_t test_inputs[TEST_INPUT_COUNT] =
{
    /*
     * Event 1
     *
     * Slave 0x30
     * pin 7 active
     *
     * data = 80 00
     */
    {
        .slave_num = 1,
        .pin_state = 0x0080,
        .data = { 0x80, 0x00 },
        .data_len = 2
    },

    /*
     * Event 2
     *
     * Slave 0x31
     *
     * pins 3 and 7 active
     *
     * 0x0088 = 0000 0000 1000 1000
     *
     * data = 88 00
     */
    {
        .slave_num = 2,
        .pin_state = 0x0088,
        .data = { 0x88, 0x00 },
        .data_len = 2
    },

    /*
     * Event 3
     *
     * Slave 0x30
     *
     * pins 0 and 8
     *
     * 0x0101
     */
    {
        .slave_num = 1,
        .pin_state = 0x0101,
        .data = { 0x01, 0x01 },
        .data_len = 2
    },

    /*
     * Event 4
     *
     * Slave 0x31
     *
     * pins 4, 5 and 12
     *
     * 0x1030
     */
    {
        .slave_num = 2,
        .pin_state = 0x1030,
        .data = { 0x30, 0x10 },
        .data_len = 2
    },

    /*
     * Event 5
     *
     * Slave 0x30
     *
     * pins 0, 1, 7 and 15
     *
     * 0x8083
     */
    {
        .slave_num = 1,
        .pin_state = 0x8083,
        .data = { 0x83, 0x80 },
        .data_len = 2
    }
};


/* ============================================================
 *                  DATA CONVERSION HELPERS
 * ============================================================ */

static void pin_state_to_bytes(
    uint16_t pin_state,
    uint8_t *data)
{
    /*
     * Little-endian representation.
     *
     * 0x0080 -> 80 00
     * 0x0101 -> 01 01
     * 0x8083 -> 83 80
     */

    data[0] = (uint8_t)(pin_state & 0xFF);
    data[1] = (uint8_t)((pin_state >> 8) & 0xFF);
}


static void print_data(
    const uint8_t *data,
    size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        printf("0x%02X", data[i]);

        if (i + 1 < len)
        {
            printf(" ");
        }
    }

    printf("\n");
}


/* ============================================================
 *                         GPIO ALERT
 * ============================================================ */

static void init_alert_gpio(
    gpio_num_t gpio)
{
    gpio_config_t config =
    {
        .pin_bit_mask = (1ULL << gpio),

        .mode = GPIO_MODE_OUTPUT,

        .pull_up_en = GPIO_PULLUP_DISABLE,

        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(
        gpio_config(&config)
    );

    /*
     * ALERT inactive.
     */
    ESP_ERROR_CHECK(
        gpio_set_level(gpio, 0)
    );
}


static void send_alert(
    i2c_slave_context_t *slave)
{
    ESP_LOGI(
        TAG,
        "%s: ALERT -> GPIO%d",
        slave->name,
        slave->alert_gpio
    );

    /*
     * Master должен увидеть этот импульс
     * и после него выполнить I2C READ.
     */

    gpio_set_level(
        slave->alert_gpio,
        1
    );

    vTaskDelay(
        pdMS_TO_TICKS(ALERT_PULSE_MS)
    );

    gpio_set_level(
        slave->alert_gpio,
        0
    );
}


/* ============================================================
 *                       I2C SLAVE INIT
 * ============================================================ */

static esp_err_t init_i2c_slave(
    i2c_slave_context_t *slave,
    i2c_port_num_t port)
{
    ESP_LOGI(
        TAG,
        "Initializing %s: port=%d address=0x%02X",
        slave->name,
        port,
        slave->address
    );

    i2c_slave_config_t config =
    {
        .i2c_port = port,

        .sda_io_num = I2C_SDA_GPIO,

        .scl_io_num = I2C_SCL_GPIO,

        .clk_source = I2C_CLK_SRC_DEFAULT,

        .send_buf_depth = I2C_SEND_BUFFER_SIZE,

        /*
         * Мы сейчас не используем запись master -> slave,
         * поэтому receive buffer можно не делать большим.
         */
        .receive_buf_depth = 16,

        .slave_addr = slave->address,

        .addr_bit_len = I2C_ADDR_BIT_LEN_7,

        .intr_priority = 0,

        .allow_pd = false,

        .flags =
        {
            .enable_internal_pullup = false
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
            "%s: failed to initialize I2C slave: %s",
            slave->name,
            esp_err_to_name(err)
        );

        return err;
    }


    ESP_LOGI(
        TAG,
        "%s initialized successfully at address 0x%02X",
        slave->name,
        slave->address
    );

    return ESP_OK;
}


/* ============================================================
 *                 PREPARE DATA FOR MASTER
 * ============================================================ */

static esp_err_t prepare_slave_data(
    i2c_slave_context_t *slave,
    const uint8_t *data,
    size_t len)
{
    if (slave == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (slave->handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > I2C_SEND_BUFFER_SIZE)
    {
        ESP_LOGE(
            TAG,
            "%s: data too large: %zu",
            slave->name,
            len
        );

        return ESP_ERR_INVALID_SIZE;
    }


    ESP_LOGI(
        TAG,
        "%s: preparing %zu bytes for master",
        slave->name,
        len
    );

    printf(
        "%s data: ",
        slave->name
    );

    print_data(
        data,
        len
    );


    /*
     * ВАЖНО:
     *
     * Здесь ESP32 НЕ отправляет данные по I2C.
     *
     * i2c_slave_transmit() только помещает данные
     * в TX buffer slave.
     *
     * Реальная передача произойдет позже,
     * когда внешний MASTER выполнит READ.
     */

    esp_err_t err =
        i2c_slave_transmit(
            slave->handle,
            data,
            len,
            100
        );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "%s: failed to queue I2C data: %s",
            slave->name,
            esp_err_to_name(err)
        );

        return err;
    }


    ESP_LOGI(
        TAG,
        "%s: data is ready for master",
        slave->name
    );

    return ESP_OK;
}


/* ============================================================
 *                     PROCESS USER INPUT
 * ============================================================ */

static esp_err_t process_input_event(
    const input_event_t *event)
{
    if (event == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    i2c_slave_context_t *slave = NULL;


    if (event->slave_num == 1)
    {
        slave = &slave1;
    }
    else if (event->slave_num == 2)
    {
        slave = &slave2;
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Invalid slave number: %u",
            event->slave_num
        );

        return ESP_ERR_INVALID_ARG;
    }


    ESP_LOGI(
        TAG,
        "============================================"
    );

    ESP_LOGI(
        TAG,
        "USER INPUT"
    );

    ESP_LOGI(
        TAG,
        "Target slave: %s",
        slave->name
    );

    ESP_LOGI(
        TAG,
        "I2C address: 0x%02X",
        slave->address
    );

    ESP_LOGI(
        TAG,
        "16-bit pin state: 0x%04X",
        event->pin_state
    );


    /*
     * Формируем данные из состояния 16 пинов.
     */
    uint8_t data[BYTES_PER_SLAVE];

    pin_state_to_bytes(
        event->pin_state,
        data
    );


    /*
     * 1. Сначала кладем данные в slave TX FIFO.
     */
    esp_err_t err =
        prepare_slave_data(
            slave,
            data,
            sizeof(data)
        );

    if (err != ESP_OK)
    {
        return err;
    }


    /*
     * 2. И только после этого сообщаем master,
     *    что данные готовы.
     */
    send_alert(slave);


    ESP_LOGI(
        TAG,
        "Waiting for external MASTER to read "
        "slave 0x%02X...",
        slave->address
    );

    return ESP_OK;
}


/* ============================================================
 *                    SIMULATED USER TASK
 * ============================================================ */

static void simulated_user_task(
    void *arg)
{
    (void)arg;

    ESP_LOGI(
        TAG,
        "Starting simulated user input"
    );


    for (int i = 0; i < TEST_INPUT_COUNT; i++)
    {
        ESP_LOGI(
            TAG,
            "--------------------------------------------"
        );

        ESP_LOGI(
            TAG,
            "Test input %d/%d",
            i + 1,
            TEST_INPUT_COUNT
        );


        esp_err_t err =
            process_input_event(
                &test_inputs[i]
            );


        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Input processing failed: %s",
                esp_err_to_name(err)
            );
        }


        /*
         * В реальном устройстве здесь НЕ будет
         * такого искусственного delay.
         *
         * Здесь он нужен только для демонстрации
         * пяти заранее подготовленных событий.
         */
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }


    ESP_LOGI(
        TAG,
        "============================================"
    );

    ESP_LOGI(
        TAG,
        "All %d simulated inputs processed",
        TEST_INPUT_COUNT
    );


    /*
     * После завершения теста задача остается живой,
     * чтобы ESP32 продолжал работать как slave.
     */
    while (true)
    {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}


/* ============================================================
 *                           MAIN
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
        "ESP32 has two real I2C slave controllers"
    );

    ESP_LOGI(
        TAG,
        "SDA = GPIO%d",
        I2C_SDA_GPIO
    );

    ESP_LOGI(
        TAG,
        "SCL = GPIO%d",
        I2C_SCL_GPIO
    );

    ESP_LOGI(
        TAG,
        "============================================"
    );


    /*
     * ALERT lines.
     */
    init_alert_gpio(
        ALERT_1_GPIO
    );

    init_alert_gpio(
        ALERT_2_GPIO
    );


    /*
     * Создаем НАСТОЯЩИЙ I2C slave 0x30.
     */
    ESP_ERROR_CHECK(
        init_i2c_slave(
            &slave1,
            I2C_SLAVE_1_PORT
        )
    );


    /*
     * Создаем НАСТОЯЩИЙ I2C slave 0x31.
     */
    ESP_ERROR_CHECK(
        init_i2c_slave(
            &slave2,
            I2C_SLAVE_2_PORT
        )
    );


    ESP_LOGI(
        TAG,
        "============================================"
    );

    ESP_LOGI(
        TAG,
        "Both I2C slaves are ready"
    );

    ESP_LOGI(
        TAG,
        "Slave 1: address 0x%02X, ALERT GPIO%d",
        slave1.address,
        slave1.alert_gpio
    );

    ESP_LOGI(
        TAG,
        "Slave 2: address 0x%02X, ALERT GPIO%d",
        slave2.address,
        slave2.alert_gpio
    );

    ESP_LOGI(
        TAG,
        "============================================"
    );


    /*
     * Запускаем имитацию пользователя.
     */
    xTaskCreate(
        simulated_user_task,
        "simulated_user",
        4096,
        NULL,
        5,
        NULL
    );
}
