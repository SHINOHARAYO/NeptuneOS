#include "kernel/acpi.h"
#include "kernel/console.h"
#include "kernel/log.h"
#include "kernel/mmu.h"

#include <stddef.h>
#include <stdint.h>

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt header;
    uint32_t lapic_addr;
    uint32_t flags;
} __attribute__((packed));

struct acpi_state {
    uint64_t rsdp_phys;
    uint32_t lapic_addr;
    uint32_t ioapic_addr;
    uint8_t ioapic_id;
    uint8_t cpu_count;
    uint8_t ioapic_count;
    uint8_t iso_count;
    uint8_t ready;
};

static struct acpi_state acpi;

static int checksum_ok(const uint8_t *data, uint32_t length)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum == 0;
}

static const struct acpi_rsdp *rsdp_scan(uint64_t start, uint64_t length)
{
    for (uint64_t addr = start; addr < start + length; addr += 16) {
        const struct acpi_rsdp *rsdp = (const struct acpi_rsdp *)phys_to_hhdm(addr);
        if (rsdp->signature[0] != 'R' || rsdp->signature[1] != 'S' ||
            rsdp->signature[2] != 'D' || rsdp->signature[3] != ' ' ||
            rsdp->signature[4] != 'P' || rsdp->signature[5] != 'T' ||
            rsdp->signature[6] != 'R' || rsdp->signature[7] != ' ') {
            continue;
        }
        if (!checksum_ok((const uint8_t *)rsdp, 20)) {
            continue;
        }
        if (rsdp->revision >= 2 && rsdp->length >= sizeof(*rsdp)) {
            if (!checksum_ok((const uint8_t *)rsdp, rsdp->length)) {
                continue;
            }
        }
        return rsdp;
    }
    return NULL;
}

static const struct acpi_sdt *acpi_find_table(const struct acpi_rsdp *rsdp, const char sig[4])
{
    if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
        const struct acpi_sdt *xsdt = (const struct acpi_sdt *)phys_to_hhdm(rsdp->xsdt_addr);
        if (!checksum_ok((const uint8_t *)xsdt, xsdt->length)) {
            return NULL;
        }
        uint32_t entries = (xsdt->length - sizeof(*xsdt)) / 8;
        const uint64_t *addrs = (const uint64_t *)((const uint8_t *)xsdt + sizeof(*xsdt));
        for (uint32_t i = 0; i < entries; ++i) {
            const struct acpi_sdt *hdr = (const struct acpi_sdt *)phys_to_hhdm(addrs[i]);
            if (hdr->signature[0] == sig[0] && hdr->signature[1] == sig[1] &&
                hdr->signature[2] == sig[2] && hdr->signature[3] == sig[3]) {
                return hdr;
            }
        }
    }
    if (rsdp->rsdt_addr) {
        const struct acpi_sdt *rsdt = (const struct acpi_sdt *)phys_to_hhdm(rsdp->rsdt_addr);
        if (!checksum_ok((const uint8_t *)rsdt, rsdt->length)) {
            return NULL;
        }
        uint32_t entries = (rsdt->length - sizeof(*rsdt)) / 4;
        const uint32_t *addrs = (const uint32_t *)((const uint8_t *)rsdt + sizeof(*rsdt));
        for (uint32_t i = 0; i < entries; ++i) {
            const struct acpi_sdt *hdr = (const struct acpi_sdt *)phys_to_hhdm(addrs[i]);
            if (hdr->signature[0] == sig[0] && hdr->signature[1] == sig[1] &&
                hdr->signature[2] == sig[2] && hdr->signature[3] == sig[3]) {
                return hdr;
            }
        }
    }
    return NULL;
}

static void acpi_parse_madt(const struct acpi_madt *madt)
{
    acpi.lapic_addr = madt->lapic_addr;
    const uint8_t *ptr = (const uint8_t *)madt + sizeof(*madt);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;
    while (ptr + 2 <= end) {
        uint8_t type = ptr[0];
        uint8_t len = ptr[1];
        if (len < 2 || ptr + len > end) {
            break;
        }
        if (type == 0 && len >= 8) {
            uint32_t flags = *(const uint32_t *)(ptr + 4);
            if (flags & 0x1) {
                acpi.cpu_count++;
            }
        } else if (type == 1 && len >= 12) {
            acpi.ioapic_id = ptr[2];
            acpi.ioapic_addr = *(const uint32_t *)(ptr + 4);
            acpi.ioapic_count++;
        } else if (type == 2 && len >= 10) {
            acpi.iso_count++;
        }
        ptr += len;
    }
}

void acpi_init(void)
{
    acpi = (struct acpi_state){0};
    uint16_t ebda_seg = *(const uint16_t *)phys_to_hhdm(0x40E);
    uint64_t ebda_addr = ((uint64_t)ebda_seg) << 4;
    const struct acpi_rsdp *rsdp = rsdp_scan(ebda_addr, 1024);
    if (!rsdp) {
        rsdp = rsdp_scan(0xE0000, 0x20000);
    }
    if (!rsdp) {
        log_warn("ACPI RSDP not found");
        return;
    }
    acpi.rsdp_phys = hhdm_to_phys((uint64_t)rsdp);
    const struct acpi_sdt *madt_hdr = acpi_find_table(rsdp, "APIC");
    if (madt_hdr) {
        acpi_parse_madt((const struct acpi_madt *)madt_hdr);
    }
    acpi.ready = 1;
    log_info("ACPI tables discovered");
}

void acpi_dump(void)
{
    console_write("ACPI:\n");
    if (!acpi.ready) {
        console_write("  not found\n");
        return;
    }
    console_write("  RSDP=");
    console_write_hex(acpi.rsdp_phys);
    console_write("\n  LAPIC=");
    console_write_hex(acpi.lapic_addr);
    console_write(" CPUs=");
    console_write_hex(acpi.cpu_count);
    console_write("\n  IOAPIC=");
    console_write_hex(acpi.ioapic_addr);
    console_write(" ID=");
    console_write_hex(acpi.ioapic_id);
    console_write(" ISO=");
    console_write_hex(acpi.iso_count);
    console_write("\n");
}
