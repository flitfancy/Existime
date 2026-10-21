#include <stdint.h>
#include <stdbool.h>

/* STM32C011F4P6, map PCB1: PB6=SCL, PB7=SDA; PA6=D7, PA7=D8, PA8=D43. */
#define REG32(a) (*(volatile uint32_t *)(a))
#define GPIOA 0x50000000u
#define GPIOB 0x50000400u
#define RCC_IOPENR REG32(0x40021034u)
#define MODER(p) REG32((p) + 0x00u)
#define OTYPER(p) REG32((p) + 0x04u)
#define OSPEEDR(p) REG32((p) + 0x08u)
#define PUPDR(p) REG32((p) + 0x0cu)
#define IDR(p) REG32((p) + 0x10u)
#define BSRR(p) REG32((p) + 0x18u)
#define LED_MASK ((1u << 6) | (1u << 7) | (1u << 8))
#define SCL (1u << 6)
#define SDA (1u << 7)

/* Fixed RAM diagnostics allow a debugger to verify real I2C acknowledgements. */
struct diagnostics {
    uint32_t magic, version, heartbeat, write_ack_mask, verified_mask, errors;
    uint32_t gpioa_idr, gpiob_idr;
    uint32_t output[5], config[5], input[5];
};
__attribute__((section(".status"), used))
volatile struct diagnostics status;

static void delay(uint32_t n) {
    while (n--) __asm__ volatile ("nop");
}
static void pause_bus(void) { delay(48); }
static void scl_low(void) { BSRR(GPIOB) = SCL << 16; }
static void sda_low(void) { BSRR(GPIOB) = SDA << 16; }
static void sda_release(void) { BSRR(GPIOB) = SDA; }
static bool scl_release(void) {
    BSRR(GPIOB) = SCL;
    for (uint32_t n = 0; n < 2000; ++n) {
        if (IDR(GPIOB) & SCL) return true;
        delay(8);
    }
    return false;
}
static bool start(void) {
    sda_release(); pause_bus();
    if (!scl_release()) return false;
    pause_bus();
    if (!(IDR(GPIOB) & SDA)) return false;
    sda_low(); pause_bus(); scl_low();
    return true;
}
static void stop(void) {
    scl_low(); sda_low(); pause_bus();
    (void)scl_release(); pause_bus(); sda_release(); pause_bus();
}
static bool write_byte(uint8_t b) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (b & 0x80u) sda_release(); else sda_low();
        pause_bus();
        if (!scl_release()) return false;
        pause_bus(); scl_low(); b <<= 1;
    }
    sda_release(); pause_bus();
    if (!scl_release()) return false;
    pause_bus();
    bool ack = !(IDR(GPIOB) & SDA);
    scl_low();
    return ack;
}
static bool read_byte(uint8_t *out) {
    uint8_t b = 0;
    sda_release();
    for (unsigned bit = 0; bit < 8; ++bit) {
        pause_bus();
        if (!scl_release()) return false;
        pause_bus(); b = (uint8_t)((b << 1) | !!(IDR(GPIOB) & SDA));
        scl_low();
    }
    /* NACK the final byte. */
    sda_release(); pause_bus();
    if (!scl_release()) return false;
    pause_bus(); scl_low();
    *out = b;
    return true;
}
static bool write_reg(uint8_t addr, uint8_t reg, uint8_t value) {
    bool ok = start() && write_byte((uint8_t)(addr << 1)) &&
              write_byte(reg) && write_byte(value);
    stop(); return ok;
}
static bool read_reg(uint8_t addr, uint8_t reg, uint8_t *value) {
    bool ok = start() && write_byte((uint8_t)(addr << 1)) && write_byte(reg) &&
              start() && write_byte((uint8_t)((addr << 1) | 1u)) && read_byte(value);
    stop(); return ok;
}
static void configure_output(unsigned port, unsigned pin) {
    MODER(port) = (MODER(port) & ~(3u << (pin * 2))) | (1u << (pin * 2));
    OSPEEDR(port) &= ~(3u << (pin * 2));
    PUPDR(port) &= ~(3u << (pin * 2));
}
static void gpio_init(void) {
    RCC_IOPENR |= 3u;
    (void)RCC_IOPENR;
    /* Set the LED latches low before enabling their push-pull outputs. */
    BSRR(GPIOA) = LED_MASK << 16;
    OTYPER(GPIOA) &= ~LED_MASK;
    for (unsigned pin = 6; pin <= 8; ++pin) configure_output(GPIOA, pin);
    /* Only PB6/PB7 are modified; PA13/PA14 retain their SWD function. */
    BSRR(GPIOB) = SCL | SDA;
    OTYPER(GPIOB) |= SCL | SDA;
    configure_output(GPIOB, 6); configure_output(GPIOB, 7);
    pause_bus();
    for (unsigned i = 0; i < 9 && !(IDR(GPIOB) & SDA); ++i) {
        scl_low(); pause_bus(); (void)scl_release(); pause_bus();
    }
    stop();
}
static void main_loop(void) {
    gpio_init();
    status.magic = 0x4d41504cu; /* MAPL */
    status.version = 1;
    for (;;) {
        uint32_t acknowledged = 0, verified = 0;
        for (unsigned i = 0; i < 5; ++i) {
            uint8_t addr = (uint8_t)(0x20u + i), out = 0xff, cfg = 0xff, in = 0xff;
            /* TCA9534: output latch=0 first, then direction=output (0). */
            bool wrote = write_reg(addr, 1, 0) && write_reg(addr, 3, 0);
            if (wrote) acknowledged |= 1u << i;
            bool read = read_reg(addr, 1, &out) && read_reg(addr, 3, &cfg) &&
                        read_reg(addr, 0, &in);
            status.output[i] = out; status.config[i] = cfg; status.input[i] = in;
            if (wrote && read && out == 0 && cfg == 0 && in == 0) verified |= 1u << i;
            if (!wrote || !read) ++status.errors;
        }
        status.write_ack_mask = acknowledged;
        status.verified_mask = verified;
        status.gpioa_idr = IDR(GPIOA); status.gpiob_idr = IDR(GPIOB);
        ++status.heartbeat;
        delay(120000);
    }
}

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
__attribute__((noreturn)) void Default_Handler(void) { for (;;) __asm__ volatile ("nop"); }
__attribute__((noreturn)) void Reset_Handler(void) {
    __asm__ volatile ("cpsid i");
    uint32_t *src = &_sidata;
    for (uint32_t *p = &_sdata; p < &_edata;) *p++ = *src++;
    for (uint32_t *p = &_sbss; p < &_ebss;) *p++ = 0;
    volatile uint32_t *s = (volatile uint32_t *)&status;
    for (unsigned i = 0; i < sizeof(status) / sizeof(uint32_t); ++i) s[i] = 0;
    main_loop();
    for (;;) {}
}
