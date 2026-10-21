#include "sensors.h"
#include "platform.h"

void sensors_reset_heading(struct sensors *s) {
    s->have_mag = s->have_acc = s->new_mag = s->heading_valid = false;
    s->direction = s->candidate_direction = -1;
    s->direction_count = 0;
}

void sensors_init(struct sensors *s) {
    uint8_t id = 255, a = 255, b = 255, c = 255;
    s->mag_ready = read_reg(0x1e, 0x4f, &id) && id == 0x40;
    s->who_mag = id;
    s->mag_ready = s->mag_ready && write_reg(0x1e, 0x62, 0x10) &&
        write_reg(0x1e, 0x61, 0x02) && write_reg(0x1e, 0x60, 0x84);
    s->mag_ready = s->mag_ready && read_reg(0x1e, 0x60, &a) &&
        read_reg(0x1e, 0x61, &b) && read_reg(0x1e, 0x62, &c) && a == 0x84 && b == 2 && c == 0x10;
    s->mag_config[0] = a; s->mag_config[1] = b; s->mag_config[2] = c;
    id = 255; a = b = 255;
    s->acc_ready = read_reg(0x19, 0x0f, &id) && id == 0x33;
    s->who_acc = id;
    s->acc_ready = s->acc_ready && write_reg(0x19, 0x23, 0x88) && write_reg(0x19, 0x20, 0x47);
    s->acc_ready = s->acc_ready && read_reg(0x19, 0x23, &a) &&
        read_reg(0x19, 0x20, &b) && a == 0x88 && b == 0x47;
    s->acc_config[0] = b; s->acc_config[1] = a;
    sensors_reset_heading(s);
}

static bool read_vector(uint8_t address, uint8_t reg, int32_t *v, bool accel) {
    uint8_t bytes[6];
    if (!i2c_read(address, (uint8_t)(reg | 0x80), bytes, 6)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        int16_t n = (int16_t)((uint16_t)bytes[i * 2] | ((uint16_t)bytes[i * 2 + 1] << 8));
        v[i] = accel ? n / 16 : n;
    }
    return true;
}

static void filter(int32_t *out, const int32_t *in, bool first) {
    for (unsigned i = 0; i < 3; ++i)
        out[i] = first ? in[i] * 256 : out[i] + (in[i] * 256 - out[i]) / 4;
}

void sensors_poll(struct sensors *s, const struct calibration *cal, uint32_t now) {
    s->new_mag = false;
    uint8_t flags = 0;
    if (s->acc_ready) {
        if (!read_reg(0x19, 0x27, &flags)) ++s->acc_errors;
        else if (flags & 8) {
            if (read_vector(0x19, 0x28, s->acc, true)) {
                filter(s->filtered_acc, s->acc, !s->have_acc);
                s->have_acc = true; s->acc_at = now; ++s->acc_samples;
            } else ++s->acc_errors;
        }
    }
    if (s->mag_ready) {
        if (!read_reg(0x1e, 0x67, &flags)) ++s->mag_errors;
        else if (flags & 8) {
            if (read_vector(0x1e, 0x68, s->mag, false)) {
                filter(s->filtered_mag, s->mag, !s->have_mag);
                s->have_mag = true; s->mag_at = now; s->new_mag = true; ++s->mag_samples;
            } else ++s->mag_errors;
        }
    }
    bool fresh = s->have_mag && s->have_acc && (uint32_t)(now - s->mag_at) < 250 &&
        (uint32_t)(now - s->acc_at) < 150;
    int angle = 0, unused;
    int32_t m[3], a[3];
    for (unsigned i = 0; i < 3; ++i) { m[i] = s->filtered_mag[i] / 256; a[i] = s->filtered_acc[i] / 256; }
    s->heading_valid = cal->valid && fresh && compass_angle(cal, s->mag, s->acc, &unused) &&
        compass_angle(cal, m, a, &angle);
    if (!s->heading_valid) {
        s->direction = s->candidate_direction = -1; s->direction_count = 0;
        return;
    }
    s->heading = compass_heading(angle, cal->north);
    int direction = direction_index(s->heading);
    if (direction != s->direction) s->direction = -1;
    if (s->new_mag) {
        if (direction == s->candidate_direction) ++s->direction_count;
        else { s->candidate_direction = direction; s->direction_count = 1; }
        if (direction >= 0 && s->direction_count >= 3) s->direction = direction;
    }
}
