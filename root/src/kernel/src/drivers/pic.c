#include <stdint.h>
#include <apic.h>
#include <pic_8259.h>
#include <pic.h>

static int should_use_apic = 0;

void pic_send_eoi(uint8_t current_vector) {
    if (should_use_apic) {
        lapic_eoi();
    } else {
        pic_8259_eoi(current_vector);
    }

    return;
}

void pic_set_mask(int irq, int status) {
    if (should_use_apic) {
        io_apic_set_mask(irq, status);
    } else {
        pic_8259_set_mask(irq, status);
    }

    return;
}

void init_pic(void) {
    if (apic_supported()) {
        pic_8259_remap(0xa0, 0xa8);
        pic_8259_mask_all();
        should_use_apic = 1;
        init_apic();
    } else {
        /* FIXME: Should we make these offsets tunable? (might coincide with other vectors
           we don't want to be tunable ...) */
        should_use_apic = 0;
        pic_8259_remap(0x20, 0x28);
    }

    return;
}
