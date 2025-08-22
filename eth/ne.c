#include "../types.h"
#include "../x86.h"
#include "../defs.h"
#include "../net/net.h"
#include "ne.h"

// Constants for DMA and hardware interaction
#define PROM_SIGNATURE        0x57
#define RESET_TIMEOUT_POLL_LIMIT 10000

// Helper function to perform a sequence of register writes
static void write_sequence(ne_t* ne, const struct { uchar offset, value; } *seq, int len);

// Probe the NIC and retrieve its MAC address.
int
ne_probe(ne_t* ne)
{
    uchar eprom[32];
    int reg0;
    int i;
    
    reg0 = inb(ne->base);
    if (reg0 == 0xFF)
        return FALSE;

    // Verify that a DP8390 controller is present.
    {
        int regd;
        outb(ne->base + DP_CR, CR_STP | CR_NO_DMA | CR_PS_P1);
        regd = inb(ne->base + DP_MAR5);
        outb(ne->base + DP_MAR5, 0xFF);
        outb(ne->base + DP_CR, CR_NO_DMA | CR_PS_P0);
        inb(ne->base + DP_CNTR0);
        if (inb(ne->base + DP_CNTR0) != 0) {
            outb(ne->base, reg0);
            outb(ne->base + DP_TCR, regd);
            cprintf("%s: This is not NEx000.\n", ne->name);
            return FALSE;
        }
    }

    // Reset the board.
    {
        int j = 0;
        outb(ne->base + NE_RESET, inb(ne->base + NE_RESET));
        while (inb(ne->base + DP_ISR) == 0) {
            if (j++ > RESET_TIMEOUT_POLL_LIMIT) {
                cprintf("%s: NIC reset failure\n", ne->name);
                return FALSE;
            }
        }
        outb(ne->base + DP_ISR, 0xFF);
    }

    // Read 16 bytes from the PROM (32 bytes on the wire) using the standard
    // initialization sequence described in the datasheet.
    {
        struct {
            uchar offset, value;
        } seq[] = {
            { DP_CR, CR_NO_DMA | CR_PS_P0 | CR_STP },
            { DP_DCR, (DCR_BMS | DCR_8BYTES) },
            { DP_RBCR0, 0x00 }, { DP_RBCR1, 0x00 },
            { DP_RCR, RCR_MON },
            { DP_TCR, TCR_INTERNAL },
            { DP_ISR, 0xFF },
            { DP_IMR, 0x00 },
            { DP_RBCR0, 32 }, { DP_RBCR1, 0 },
            { DP_RSAR0, 0x00 }, { DP_RSAR1, 0x00 },
            { DP_CR, (CR_PS_P0 | CR_DM_RR | CR_STA) },
        };
        write_sequence(ne, seq, NELEM(seq));

        ne->is16bit = TRUE;
        for (i = 0; i < 32; i += 2) {
            eprom[i+0] = inb(ne->base + NE_DATA);
            eprom[i+1] = inb(ne->base + NE_DATA);
            if (eprom[i+0] != eprom[i+1])
                ne->is16bit = FALSE;
        }
        if (ne->is16bit)
            for (i = 0; i < 16; ++i)
                eprom[i] = eprom[i*2];
        if (eprom[14] != PROM_SIGNATURE || eprom[15] != PROM_SIGNATURE)
            return FALSE;
    }
    
    for (i = 0; i < 6; ++i)
        ne->address[i] = eprom[i];
    
    return TRUE;
}

// A helper function to execute a series of register writes.
static void write_sequence(ne_t* ne, const struct { uchar offset, value; } *seq, int len) {
    for (int i = 0; i < len; ++i) {
        outb(ne->base + seq[i].offset, seq[i].value);
    }
}

// Initialize the NIC.
void
ne_init(ne_t* ne)
{
    int i;
    
    if (ne->is16bit) {
        ne->ramsize = NE2000_SIZE;
        ne->startaddr = NE2000_START;
        ne->send_startpage = NE2000_START / DP_PAGESIZE;
    } else {
        ne->startaddr = NE1000_START;
        ne->ramsize = NE1000_SIZE;
        ne->send_startpage = NE1000_START / DP_PAGESIZE;
    }
    ne->pages = ne->ramsize / DP_PAGESIZE;
    ne->send_stoppage = ne->send_startpage + SENDQ_PAGES * SENDQ_LEN - 1;
    ne->recv_startpage = ne->send_stoppage + 1;
    ne->recv_stoppage = ne->send_startpage + ne->pages;
    for (i = 0; i < SENDQ_LEN; ++i) {
        ne->sendq[i].sendpage = ne->send_startpage + i * SENDQ_PAGES;
        ne->sendq[i].filled = 0;
    }
    ne->sendq_head = 0;
    ne->sendq_tail = SENDQ_LEN-1;

    cprintf("%s: NE%d000 (%dkB RAM) at 0x%x:%d - ",
            ne->name,
            ne->is16bit ? 2 : 1,
            ne->ramsize/1024,
            ne->base,
            ne->irq);
    for (i = 0; i < 6; ++i)
        cprintf("%x%s", ne->address[i], i < 5 ? ":" : "\n");

    {
        struct {
            uchar offset, value;
        } seq[] = {
            { DP_CR, CR_PS_P0 | CR_STP | CR_NO_DMA },
            { DP_DCR, ((ne->is16bit ? DCR_WORDWIDE : DCR_BYTEWIDE) | DCR_LTLENDIAN | DCR_8BYTES | DCR_BMS) },
            { DP_RCR, RCR_MON },
            { DP_RBCR0, 0 }, { DP_RBCR1, 0 },
            { DP_TCR, TCR_INTERNAL },
            { DP_PSTART, ne->recv_startpage },
            { DP_PSTOP, ne->recv_stoppage },
            { DP_BNRY, ne->recv_startpage },
            { DP_ISR, 0xFF },
            { DP_IMR, (IMR_PRXE | IMR_PTXE | IMR_RXEE | IMR_TXEE | IMR_OVWE | IMR_CNTE) },
            { DP_CR, CR_PS_P1 | CR_NO_DMA },
            { DP_PAR0, ne->address[0] }, { DP_PAR1, ne->address[1] },
            { DP_PAR2, ne->address[2] }, { DP_PAR3, ne->address[3] },
            { DP_PAR4, ne->address[4] }, { DP_PAR5, ne->address[5] },
            { DP_MAR0, 0xFF }, { DP_MAR1, 0xFF }, { DP_MAR2, 0xFF },
            { DP_MAR3, 0xFF }, { DP_MAR4, 0xFF }, { DP_MAR5, 0xFF },
            { DP_MAR6, 0xFF }, { DP_MAR7, 0xFF },
            { DP_CURR, ne->recv_startpage + 1 },
            { DP_CR, CR_STA | CR_NO_DMA },
            { DP_TCR, TCR_NORMAL },
            { DP_RCR, RCR_PRO },
        };
        write_sequence(ne, seq, NELEM(seq));
    }
}

// Configure remote DMA for read or write operations.
void
ne_rdma_setup(ne_t* ne, int mode, ushort addr, int size)
{
    if (mode == CR_DM_RW) {
        uchar dummy[4];
        ushort safeloc = ne->startaddr - sizeof(dummy);
        int oldcrda, newcrda;
        oldcrda = inb(ne->base + DP_CRDA0);
        oldcrda |= ((inb(ne->base + DP_CRDA1) << 8) & 0xFF00);
        ne_getblock(ne, safeloc, sizeof(dummy), dummy);
        do {
            newcrda = inb(ne->base + DP_CRDA0);
            newcrda |= ((inb(ne->base + DP_CRDA1) << 8) & 0xFF00);
        } while (oldcrda == newcrda);
    }
    outb(ne->base + DP_RSAR0, addr & 0xFF);
    outb(ne->base + DP_RSAR1, (addr >> 8) & 0xFF);
    outb(ne->base + DP_RBCR0, size & 0xFF);
    outb(ne->base + DP_RBCR1, (size >> 8) & 0xFF);
    outb(ne->base + DP_CR, mode | CR_PS_P0 | CR_STA);
    return;
}

// Read 'size' bytes from NIC RAM at 'addr' into 'dst'.
void
ne_getblock(ne_t* ne, ushort addr, int size, void* dst)
{
    ne_rdma_setup(ne, CR_DM_RR, addr, size);
    if (ne->is16bit)
        insw(ne->base + NE_DATA, dst, size);
    else
        insb(ne->base + NE_DATA, dst, size);
    return;
}

// Begin transmitting data.
void
ne_start_xmit(ne_t* ne, int page, int size)
{
    outb(ne->base + DP_TPSR, page);
    outb(ne->base + DP_TBCR0, size & 0xFF);
    outb(ne->base + DP_TBCR1, (size >> 8) & 0xFF);
    outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA | CR_TXP);
    return;
}

// Write a packet of 'size' bytes into local memory and transmit it.
int
ne_pio_write(ne_t* ne, uchar* packet, int size)
{
    int q = ne->sendq_head % SENDQ_LEN;
    if (ne->sendq[q].filled || ne->sendq_head > ne->sendq_tail) {
        cprintf("%s: all transmitting buffers in NIC are busy.\n", ne->name);
        return 0;
    }
    ne_rdma_setup(ne, CR_DM_RW, ne->sendq[q].sendpage * DP_PAGESIZE, size);
    if (ne->is16bit)
        outsw(ne->base + NE_DATA, packet, size);
    else
        outsb(ne->base + NE_DATA, packet, size);

    // Add a check for ISR_RDC to ensure the DMA write is complete.
    while ((inb(ne->base + DP_ISR) & ISR_RDC) == 0)
        ;

    ne->sendq[q].filled = TRUE;
    ne_start_xmit(ne, ne->sendq[q].sendpage, size);
    ne->sendq_head++;
    return size;
}

// [11] Strage Format
typedef struct {
    uchar status;
    uchar next;
    uchar rbc0, rbc1;
} ne_recv_hdr;

// Read the next packet from the ring buffer.
int
ne_pio_read(ne_t* ne, uchar* buf, int bufsize)
{
    uint pktsize;
    ne_recv_hdr header;
    uint curr, bnry, page;
    outb(ne->base + DP_CR, CR_PS_P1);
    curr = inb(ne->base + DP_CURR);
    outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA);
    bnry = inb(ne->base + DP_BNRY);
    page = bnry + 1;
    if (page == ne->recv_stoppage)
        page = ne->recv_startpage;

    if (page == curr) {
        cprintf("%s: no packet to read\n", ne->name);
        return 0;
    }

    ne_getblock(ne, page * DP_PAGESIZE, sizeof(header), &header);
    pktsize = (header.rbc0 | (header.rbc1 << 8)) - sizeof(header);
    if (pktsize < ETH_MIN_SIZE || pktsize > ETH_MAX_SIZE || (header.status & RSR_PRX) == 0) {
        cprintf("%s: Bad packet (size: %d, status: 0x%x)\n", ne->name, pktsize, header.status);
        return -1;
    }

    if (buf == 0 || pktsize > bufsize) {
        return pktsize;
    } else {
        int remain = (ne->recv_stoppage - page) * DP_PAGESIZE;
        if (remain < pktsize) {
            ne_getblock(ne, page * DP_PAGESIZE + sizeof(header), remain, buf);
            ne_getblock(ne, ne->recv_startpage * DP_PAGESIZE, pktsize - remain, buf + remain);
        } else {
            ne_getblock(ne, page * DP_PAGESIZE + sizeof(header), pktsize, buf);
        }
    }
    bnry = header.next - 1;
    outb(ne->base + DP_BNRY, bnry < ne->recv_startpage ? ne->recv_stoppage - 1 : bnry);
    return pktsize;
}

void
ne_interrupt(ne_t* ne)
{
    int isr;
    while ((isr = inb(ne->base + DP_ISR)) != 0) {
        outb(ne->base + DP_ISR, isr);
        if (isr & ISR_PTX) {
            ne->sendq_tail++;
            ne->sendq[ne->sendq_tail % SENDQ_LEN].filled = FALSE;
            cprintf("%s: packet transmitted with no error.\n", ne->name);
        }
        if (isr & ISR_PRX) {
            cprintf("%s: packet received with no error.\n", ne->name);
        }
    }
}