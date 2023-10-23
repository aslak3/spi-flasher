#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"

#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096

#define FLASH_CMD_READ_DEVICE_IDENTIFICATION 0x9f
#define FLASH_CMD_WRITE_EN 0x06
#define FLASH_CMD_WRITE_BYTES 0x02
#define FLASH_CMD_READ_BYTES 0x03
#define FLASH_CMD_FAST_READ_BYTES 0x0b
#define FLASH_CMD_ERASE_BULK 0xc7
#define FLASH_CMD_READ_STATUS 0x05

#define FLASH_STATUS_BUSY_MASK 0x01

typedef struct {
    uint8_t identificaiton_code;
    char *name;
    uint32_t capacity_bytes;
} flash_dev_t;

size_t my_read(uint8_t *buf, size_t len);
size_t my_write(uint8_t *buf, size_t len);
void send_command(uint8_t command, bool raise_cs);
flash_dev_t* get_device_identification(void);
void wait_until_not_busy(void);
void bulk_erase(void);
void write_bytes(uint8_t *buffer, size_t len, uint32_t start_address);
void read_bytes(uint8_t *buffer, size_t len,  uint32_t start_address);
int reprogram_flash(flash_dev_t *flash_device);

size_t my_read(uint8_t *buf, size_t len)
{
    for (int c = 0; c < len; c++) {
        size_t r = fread(&buf[c], 1, 1, stdin);
    }
    return len;
}

size_t my_write(uint8_t *buf, size_t len)
{
    for (int c = 0; c < len; c++) {
        size_t r = fwrite(&buf[c], 1, 1, stdout);
    }
    return len;
}

void send_command(uint8_t command, bool raise_cs)
{
    uint8_t buffer[1];

    buffer[0] = command;

    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, false);

    spi_write_blocking(spi_default, buffer, 1);

    if (raise_cs)
        gpio_put(PICO_DEFAULT_SPI_CSN_PIN, true);
}

flash_dev_t flash_devices[] = {
    {
        0b00010011,
        "EPCQ4A",
        (4 * 1024 * 1024) / 8,
    },
    {
        0b00010110,
        "EPCQ16A",
        (16 * 1024 * 1024) / 8,
    },
    {
        0b00010110,
        "EPCQ32A",
        (32 * 1024 * 1024) / 8,
    },
    {
        0b00010111,
        "EPCQ64A",
        (64 * 1024 * 1024) / 8,
    },
    {
        0b00011000,
        "EPCQ128A",
       (128 * 1024 *  1024) / 8,
    },
    {
        0,
        NULL,
        0
    },
};

flash_dev_t* get_device_identification(void)
{
    send_command(FLASH_CMD_READ_DEVICE_IDENTIFICATION, false);

    uint8_t dummy[2];
    spi_read_blocking(spi_default, 0, dummy, 2);

    uint8_t response[1];
    spi_read_blocking(spi_default, 0, response, 1);

    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, true);

    for (int i = 0; flash_devices[i].identificaiton_code; i++) {
        if (response[0] == flash_devices[i].identificaiton_code) {
            return &flash_devices[i];
        }
    }

    return NULL;
}

void wait_until_not_busy(void)
{
    uint8_t response[1];

    send_command(FLASH_CMD_READ_STATUS, false);

    while (true) {
        spi_read_blocking(spi_default, 0, response, 1);

        if (!(response[0] & FLASH_STATUS_BUSY_MASK))
            break;
    }

    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, true);
}

void bulk_erase(void)
{
    send_command(FLASH_CMD_WRITE_EN, true);

    send_command(FLASH_CMD_ERASE_BULK, true);

    wait_until_not_busy();
}

void write_bytes(uint8_t *buffer, size_t len, uint32_t start_address)
{
    send_command(FLASH_CMD_WRITE_EN, true);

    send_command(FLASH_CMD_WRITE_BYTES, false);

    uint8_t address[3];
    address[0] = (start_address & 0xff0000) >> 16;
    address[1] = (start_address & 0x00ff00) >> 8;
    address[2] = (start_address & 0x0000ff) >> 0;

    spi_write_blocking(spi_default, address, sizeof(address));

    spi_write_blocking(spi_default, buffer, len);

    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, true);

    wait_until_not_busy();
}

void read_bytes(uint8_t *buffer, size_t len, uint32_t start_address)
{
    send_command(FLASH_CMD_FAST_READ_BYTES, false);

    uint8_t address[3];
    address[0] = (start_address & 0xff0000) >> 16;
    address[1] = (start_address & 0x00ff00) >> 8;
    address[2] = (start_address & 0x0000ff) >> 0;
    spi_write_blocking(spi_default, address, sizeof(address));

    uint8_t dummy[1] = { 0 };
    spi_write_blocking(spi_default, dummy, 1);

    spi_read_blocking(spi_default, 0, buffer, len);

    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, true);
}

int reprogram_flash(flash_dev_t *flash_device)
{
    my_write("+++\n", 4);

    uint32_t page_count;
    my_read((uint8_t *) &page_count, sizeof(uint32_t));

    bulk_erase();

    uint8_t read_write_buffer[FLASH_PAGE_SIZE];

    for (uint32_t start_address = 0;
        start_address < page_count * FLASH_PAGE_SIZE;
        start_address += FLASH_PAGE_SIZE)
    {
        my_read(read_write_buffer, FLASH_PAGE_SIZE);

        write_bytes(read_write_buffer, FLASH_PAGE_SIZE, start_address);

        my_write("#", 1);
    }

    for (uint32_t start_address = 0;
        start_address < page_count * FLASH_PAGE_SIZE;
        start_address += FLASH_PAGE_SIZE)
    {
        read_bytes(read_write_buffer, FLASH_PAGE_SIZE, start_address);

        my_write(read_write_buffer, FLASH_PAGE_SIZE);
    }

    return 0;
}

int main()
{
    // Enable SPI 0 at 1 MHz and connect to GPIOs
    spi_init(spi_default, 1 * 1000 * 1000);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_init(PICO_DEFAULT_SPI_CSN_PIN);
    gpio_set_dir(PICO_DEFAULT_SPI_CSN_PIN, true);
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, true);

    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);
    setvbuf(stdout, NULL, _IONBF, 0);

    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    flash_dev_t *flash_device = get_device_identification();
    if (!flash_device) {
        return 1;
    }

    while (1) {
        uint8_t dummy[1];
        my_read(dummy, 1);

        uint8_t banner[32];
        sprintf(banner, "%sf\n", flash_device->name);
        my_write(banner, strlen(banner));

        uint8_t command[1];
        my_read(command, 1);

        if (command[0] == 'f') {
            reprogram_flash(flash_device);
        }
    }
    return 0;
}
