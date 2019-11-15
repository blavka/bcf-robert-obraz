#include <application.h>
#include <bcl.h>
#include <bc_ws2812b.h>

#define PASEK_COUNT 40
#define SRDCE_COUNT 32
#define LED_STRIP_TYPE BC_LED_STRIP_TYPE_RGBW
#define LED_STRIP_COUNT (PASEK_COUNT + SRDCE_COUNT)

bc_led_t led;
bc_button_t button;
int mode = 0;
bool radio_pairing_mode = false;

// Led strip
static uint32_t _bc_module_power_led_strip_dma_buffer[LED_STRIP_COUNT * LED_STRIP_TYPE * 2];

const bc_led_strip_buffer_t led_strip_buffer =
{
    .type = LED_STRIP_TYPE,
    .count = LED_STRIP_COUNT,
    .buffer = _bc_module_power_led_strip_dma_buffer
};

bc_led_strip_t full_led_strip;

const bc_led_strip_buffer_t pasek_led_strip_buffer =
{
    .type = LED_STRIP_TYPE,
    .count = PASEK_COUNT,
    .buffer = NULL
};

bc_led_strip_t srdce;

const bc_led_strip_buffer_t srdce_led_strip_buffer =
{
    .type = LED_STRIP_TYPE,
    .count = SRDCE_COUNT,
    .buffer = NULL
};

bc_led_strip_t pasek;

bc_scheduler_task_id_t led_strip_write_task_id;

void led_strip_write_task(void *param)
{
    (void) param;

    if (!bc_led_strip_write(&full_led_strip))
    {
        bc_scheduler_plan_current_now();
    }
}

bool driver_led_strip_init(const bc_led_strip_buffer_t *led_strip)
{
    (void) led_strip;

    return true;
}

bool driver_led_strip_write(void)
{
    bc_scheduler_plan_now(led_strip_write_task_id);

    return true;
}

const bc_led_strip_driver_t pasek_led_strip_driver =
{
    .init = driver_led_strip_init,
    .write = driver_led_strip_write,
    .set_pixel = bc_ws2812b_set_pixel_from_uint32,
    .set_pixel_rgbw = bc_ws2812b_set_pixel_from_rgb,
    .is_ready = bc_ws2812b_is_ready
};

void driver_srdce_set_pixel_from_uint32(int position, uint32_t color)
{
    bc_ws2812b_set_pixel_from_uint32(position + PASEK_COUNT, color);
}

void driver_srdce_set_pixel_from_rgb(int position, uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{
    bc_ws2812b_set_pixel_from_rgb(position + PASEK_COUNT, red, green, blue, white);
}

const bc_led_strip_driver_t srdce_led_strip_driver =
{
    .init = driver_led_strip_init,
    .write = driver_led_strip_write,
    .set_pixel = driver_srdce_set_pixel_from_uint32,
    .set_pixel_rgbw = driver_srdce_set_pixel_from_rgb,
    .is_ready = bc_ws2812b_is_ready
};

void led_strip_fill(bc_led_strip_t *self, uint32_t color)
{
    bc_led_strip_effect_stop(self);

    bc_led_strip_fill(self, color);

    bc_scheduler_plan_now(led_strip_write_task_id);
}

static void _bc_led_strip_effect_pulse_color_task(void *param)
{
    bc_led_strip_t *self = (bc_led_strip_t *)param;

    uint8_t r = self->_effect.color >> 24;
    uint8_t g = self->_effect.color >> 16;
    uint8_t b = self->_effect.color >> 8;
    uint8_t w = self->_effect.color;

    uint8_t brightness = (abs(19 - self->_effect.round) + 1) * (255 / 20);

    if (++self->_effect.round == 38)
    {
        self->_effect.round = 0;
    }

    r = ((uint16_t) r * brightness) >> 8;
    g = ((uint16_t) g * brightness) >> 8;
    b = ((uint16_t) b * brightness) >> 8;
    w = ((uint16_t) w * brightness) >> 8;

    for (int i = 0; i < self->_buffer->count; i++)
    {
        bc_led_strip_set_pixel_rgbw(self, i, r, g, b, w);
    }

    self->_driver->write();

    bc_scheduler_plan_current_relative(self->_effect.wait);
}

void bc_led_strip_effect_pulse_color(bc_led_strip_t *self, uint32_t color, bc_tick_t wait)
{
    bc_led_strip_effect_stop(self);

    self->_effect.round = 0;
    self->_effect.wait = wait;
    self->_effect.color = color;

    self->_effect.task_id = bc_scheduler_register(_bc_led_strip_effect_pulse_color_task, self, 0);
}

void change_mode(void)
{
    switch (++mode) {
        case 1: // zapnout pásek - duha, bez srdce
        {
            bc_led_strip_effect_rainbow(&pasek, 50);
            led_strip_fill(&srdce, 0);
            break;
        }
        case 2: // pásek – bílá, bez srdce
        {
            led_strip_fill(&pasek, 50);
            led_strip_fill(&srdce, 0);
            break;
        }
        case 3: // pásek – červená, bez srdce
        {
            led_strip_fill(&pasek, 0xff000000);
            led_strip_fill(&srdce, 0);
            break;
        }
        case 4: // pásek – zelená, bez srdce
        {
            led_strip_fill(&pasek, 0x00ff0000);
            led_strip_fill(&srdce, 0);
            break;
        }
        case 5: // pásek – modrá, bez srdce
        {
            led_strip_fill(&pasek, 0x0000ff00);
            led_strip_fill(&srdce, 0);
            break;
        }
        case 6: // pásek vypnout, zapnout srdce – červeně
        {
            led_strip_fill(&pasek, 0);
            bc_led_strip_effect_pulse_color(&srdce, 0xff000000, 50);
            break;
        }
        case 7: // pásek off, srdce duha
        {
            led_strip_fill(&pasek, 0);
            bc_led_strip_effect_rainbow(&srdce, 50);
            break;
        }
        case 8: // pásek duha, srdce duha
        {
            bc_led_strip_effect_rainbow(&pasek, 50);
            bc_led_strip_effect_rainbow(&srdce, 50);
            break;
        }
        default:
        {
            mode = 0;
            led_strip_fill(&pasek, 0);
            led_strip_fill(&srdce, 0);
            break;
        }
    }
}

void radio_event_handler(bc_radio_event_t event, void *event_param)
{
    (void) event_param;

    if (event == BC_RADIO_EVENT_ATTACH)
    {
        bc_led_pulse(&led, 1000);
    }
    else if (event == BC_RADIO_EVENT_ATTACH_FAILURE)
    {
        bc_led_pulse(&led, 5000);
    }
}

void bc_radio_pub_on_event_count(uint64_t *id, uint8_t event_id, uint16_t *event_count)
{
    (void) id;
    (void) event_count;

    bc_led_pulse(&led, 10);

    if (event_id == BC_RADIO_PUB_EVENT_PUSH_BUTTON)
    {
        change_mode();
    }
}

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_BUTTON_EVENT_PRESS)
    {
        change_mode();
    }
    else if (event == BC_BUTTON_EVENT_HOLD)
    {
        if (radio_pairing_mode)
        {
            radio_pairing_mode = false;
            bc_radio_pairing_mode_stop();
            bc_led_set_mode(&led, BC_LED_MODE_OFF);
        }
        else{
            radio_pairing_mode = true;
            bc_radio_pairing_mode_start();
            bc_led_set_mode(&led, BC_LED_MODE_BLINK_FAST);
        }
    }
}

void application_init(void)
{
    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    bc_led_strip_init(&full_led_strip, bc_module_power_get_led_strip_driver(), &led_strip_buffer);

    bc_led_strip_init(&pasek, &pasek_led_strip_driver, &pasek_led_strip_buffer);

    bc_led_strip_init(&srdce, &srdce_led_strip_driver, &srdce_led_strip_buffer);

    led_strip_write_task_id = bc_scheduler_register(led_strip_write_task, NULL, BC_TICK_INFINITY);

    led_strip_fill(&full_led_strip, 0);

    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    bc_radio_init(BC_RADIO_MODE_GATEWAY);
    bc_radio_set_event_handler(radio_event_handler, NULL);

    bc_led_pulse(&led, 2000);
}


