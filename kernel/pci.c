#include "kernel/pci.h"
#include "kernel/console.h"
#include "kernel/io.h"
#include "kernel/log.h"

#include <stdint.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_DEVICES 64

struct pci_device {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
    uint8_t irq_line;
    uint8_t irq_pin;
};

static struct pci_device pci_devices[PCI_MAX_DEVICES];
static uint32_t pci_device_count = 0;

static uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    uint8_t shift = (offset & 2) * 8;
    return (uint16_t)((val >> shift) & 0xFFFF);
}

static uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    uint8_t shift = (offset & 3) * 8;
    return (uint8_t)((val >> shift) & 0xFF);
}

static void pci_record_device(uint8_t bus, uint8_t dev, uint8_t func)
{
    if (pci_device_count >= PCI_MAX_DEVICES) {
        return;
    }
    struct pci_device *entry = &pci_devices[pci_device_count++];
    entry->bus = bus;
    entry->dev = dev;
    entry->func = func;
    entry->vendor = pci_config_read16(bus, dev, func, 0x00);
    entry->device = pci_config_read16(bus, dev, func, 0x02);
    entry->prog_if = pci_config_read8(bus, dev, func, 0x09);
    entry->subclass = pci_config_read8(bus, dev, func, 0x0A);
    entry->class_code = pci_config_read8(bus, dev, func, 0x0B);
    entry->header_type = pci_config_read8(bus, dev, func, 0x0E);
    entry->irq_line = pci_config_read8(bus, dev, func, 0x3C);
    entry->irq_pin = pci_config_read8(bus, dev, func, 0x3D);
}

void pci_init(void)
{
    pci_device_count = 0;
    for (uint16_t bus = 0; bus < 32; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            uint16_t vendor = pci_config_read16((uint8_t)bus, dev, 0, 0x00);
            if (vendor == 0xFFFF) {
                continue;
            }
            uint8_t header = pci_config_read8((uint8_t)bus, dev, 0, 0x0E);
            uint8_t functions = (header & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < functions; ++func) {
                vendor = pci_config_read16((uint8_t)bus, dev, func, 0x00);
                if (vendor == 0xFFFF) {
                    continue;
                }
                pci_record_device((uint8_t)bus, dev, func);
            }
        }
    }
    log_info("PCI enumeration complete");
    log_info_hex("PCI devices found", pci_device_count);
}

void pci_dump(void)
{
    console_write("PCI devices:\n");
    for (uint32_t i = 0; i < pci_device_count; ++i) {
        const struct pci_device *dev = &pci_devices[i];
        console_write("bus=");
        console_write_hex(dev->bus);
        console_write(" dev=");
        console_write_hex(dev->dev);
        console_write(" func=");
        console_write_hex(dev->func);
        console_write(" vid=");
        console_write_hex(dev->vendor);
        console_write(" did=");
        console_write_hex(dev->device);
        console_write(" cls=");
        console_write_hex(dev->class_code);
        console_write(" sub=");
        console_write_hex(dev->subclass);
        console_write(" irq=");
        console_write_hex(dev->irq_line);
        console_write("\n");
    }
}
