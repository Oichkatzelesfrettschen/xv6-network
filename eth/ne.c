#include "../types.h"
#include "../x86.h"
#include "../defs.h"
#include "../net/net.h"
#include "ne.h"

// Notes: the numbers (keywords) in comments refer to pages in the DP8390 specification.
// DP8390 Documents: http://www.national.com/ds/DP/DP8390D.pdf

/*
 * @brief Probes for a NE2000-compatible network card.
 *
 * This function checks for the presence of the network card at the given base
 * I/O port and reads its MAC address from the on-board EPROM. It also determines
 * if the card is a 16-bit NE2000 or an 8-bit NE1000.
 *
 * @param ne A pointer to the network card state structure.
 * @return TRUE if a card is found and initialized, FALSE otherwise.
 */
int ne_probe(ne_t* ne) {
    uchar eprom[32];
    int reg0;

    // Check for a valid base I/O port.
    reg0 = inb(ne->base);
    if (reg0 == 0xFF) {
        return FALSE;
    }

    // Verify the presence of the DP8390 by writing to a register on page 1
    // and checking its value on page 0.
    int regd;
    outb(ne->base + DP_CR, CR_STP | CR_NO_DMA | CR_PS_P1);
    regd = inb(ne->base + DP_MAR5);
    outb(ne->base + DP_MAR5, 0xFF);
    outb(ne->base + DP_CR, CR_NO_DMA | CR_PS_P0);
    inb(ne->base + DP_CNTR0);
    if (inb(ne->base + DP_CNTR0) != 0) {
        // Values differ, so restore and fail.
        outb(ne->base, reg0);
        outb(ne->base + DP_TCR, regd);
        cprintf("%s: This is not a NEx000 card.\n", ne->name);
        return FALSE;
    }

    // Reset the board by pulsing the reset port.
    outb(ne->base + NE_RESET, inb(ne->base + NE_RESET));
    int i = 0;
    while (!(inb(ne->base + DP_ISR) & ISR_RST)) {
        if (i++ > 10000) {
            cprintf("%s: NIC reset failure\n", ne->name);
            return FALSE;
        }
    }
    outb(ne->base + DP_ISR, 0xFF);

    // Initialize the card to read from the EPROM. This sequence is a known
    // procedure for accessing the card's on-board configuration.
    struct {
        uchar offset, value;
    } seq[] = {
        {DP_CR, CR_NO_DMA | CR_PS_P0 | CR_STP},
        {DP_DCR, (DCR_BMS | DCR_8BYTES)},
        {DP_RBCR0, 0x00}, {DP_RBCR1, 0x00},
        {DP_RCR, RCR_MON},
        {DP_TCR, TCR_INTERNAL},
        {DP_ISR, 0xFF},
        {DP_IMR, 0x00},
        {DP_RBCR0, 32}, {DP_RBCR1, 0},
        {DP_RSAR0, 0x00}, {DP_RSAR1, 0x00},
        {DP_CR, (CR_PS_P0 | CR_DM_RR | CR_STA)},
    };
    for (i = 0; (uint)i < NELEM(seq); ++i) {
        outb(ne->base + seq[i].offset, seq[i].value);
    }

    // Read 32 bytes from the PROM. The NE1000 and NE2000 store data differently.
    ne->is16bit = TRUE;
    for (i = 0; i < 32; i += 2) {
        eprom[i] = inb(ne->base + NE_DATA);
        eprom[i + 1] = inb(ne->base + NE_DATA);
        // NE2000 stores identical bytes at odd and even addresses.
        if (eprom[i] != eprom[i + 1]) {
            ne->is16bit = FALSE;
        }
    }

    // Normalize the EPROM data for 16-bit cards.
    if (ne->is16bit) {
        for (i = 0; i < 16; ++i) {
            eprom[i] = eprom[i * 2];
        }
    }

    // Check the signature bytes (0x57 at offset 14 and 15).
    if (eprom[14] != 0x57 || eprom[15] != 0x57) {
        return FALSE;
    }

    // Retrieve the MAC address from the EPROM data.
    for (i = 0; i < 6; ++i) {
        ne->address[i] = eprom[i];
    }

    return TRUE;
}

/*
 * @brief Initializes the network card after a successful probe.
 *
 * This function sets up the device's internal state, configures the on-board
 * RAM for send/receive queues, and initializes the DP8390 registers.
 *
 * @param ne A pointer to the network card state structure.
 */
void ne_init(ne_t* ne) {
    int i;

    // Compute memory layout based on 8-bit or 16-bit card.
    if (ne->is16bit) {
        ne->ramsize = NE2000_SIZE;
        ne->startaddr = NE2000_START;
    } else {
        ne->ramsize = NE1000_SIZE;
        ne->startaddr = NE1000_START;
    }
    ne->send_startpage = ne->startaddr / DP_PAGESIZE;
    ne->pages = ne->ramsize / DP_PAGESIZE;
    ne->send_stoppage = ne->send_startpage + SENDQ_PAGES * SENDQ_LEN - 1;
    ne->recv_startpage = ne->send_stoppage + 1;
    ne->recv_stoppage = ne->send_startpage + ne->pages;

    // Prepare the send queue.
    for (i = 0; i < SENDQ_LEN; ++i) {
        ne->sendq[i].sendpage = ne->send_startpage + i * SENDQ_PAGES;
        ne->sendq[i].filled = 0;
    }
    ne->sendq_head = 0;
    ne->sendq_tail = SENDQ_LEN - 1;

    // Display card information.
    cprintf("%s: NE%d000 (%dkB RAM) at 0x%x:%d - ",
            ne->name,
            ne->is16bit ? 2 : 1,
            ne->ramsize / 1024,
            ne->base,
            ne->irq);
    for (i = 0; i < 6; ++i) {
        cprintf("%x%s", ne->address[i], i < 5 ? ":" : "\n");
    }
    
    // Basic initialization sequence for the DP8390.
    struct {
        uchar offset, value;
    } seq[] = {
        {DP_CR, CR_PS_P0 | CR_STP | CR_NO_DMA},
        {DP_DCR, ((ne->is16bit ? DCR_WORDWIDE : DCR_BYTEWIDE) | DCR_LTLENDIAN | DCR_8BYTES | DCR_BMS)},
        {DP_RCR, RCR_MON},
        {DP_RBCR0, 0}, {DP_RBCR1, 0},
        {DP_TCR, TCR_INTERNAL},
        {DP_PSTART, ne->recv_startpage},
        {DP_PSTOP, ne->recv_stoppage},
        {DP_BNRY, ne->recv_startpage},
        {DP_ISR, 0xFF},
        {DP_IMR, (IMR_PRXE | IMR_PTXE | IMR_RXEE | IMR_TXEE | IMR_OVWE | IMR_CNTE)},
        {DP_CR, CR_PS_P1 | CR_NO_DMA},
        {DP_PAR0, ne->address[0]}, {DP_PAR1, ne->address[1]},
        {DP_PAR2, ne->address[2]}, {DP_PAR3, ne->address[3]},
        {DP_PAR4, ne->address[4]}, {DP_PAR5, ne->address[5]},
        {DP_MAR0, 0xFF}, {DP_MAR1, 0xFF}, {DP_MAR2, 0xFF},
        {DP_MAR3, 0xFF}, {DP_MAR4, 0xFF}, {DP_MAR5, 0xFF},
        {DP_MAR6, 0xFF}, {DP_MAR7, 0xFF},
        {DP_CURR, ne->recv_startpage + 1},
        {DP_CR, CR_STA | CR_NO_DMA},
        {DP_TCR, TCR_NORMAL},
        {DP_RCR, RCR_PRO},
    };
    for (i = 0; (uint)i < NELEM(seq); ++i) {
        outb(ne->base + seq[i].offset, seq[i].value);
    }
}

/*
 * @brief Sets up remote DMA for read/write operations.
 *
 * This function configures the DP8390's remote DMA registers (RSAR, RBCR)
 * and initiates a data transfer.
 *
 * @param ne A pointer to the network card state structure.
 * @param mode The DMA mode (e.g., CR_DM_RR for remote read).
 * @param addr The starting address in the local buffer.
 * @param size The number of bytes to transfer.
 */
void ne_rdma_setup(ne_t* ne, int mode, ushort addr, int size) {
    if (mode == CR_DM_RW) {
        // This logic seems to perform a dummy read to prepare for a write,
        // which can be a tricky part of the DP8390's protocol.
        uchar dummy[4];
        ushort safeloc = ne->startaddr - sizeof(dummy);
        int oldcrda, newcrda;
        oldcrda = inb(ne->base + DP_CRDA0) | (inb(ne->base + DP_CRDA1) << 8);
        ne_getblock(ne, safeloc, sizeof(dummy), dummy);
        do {
            newcrda = inb(ne->base + DP_CRDA0) | (inb(ne->base + DP_CRDA1) << 8);
        } while (oldcrda == newcrda);
    }
    
    // Configure remote DMA registers.
    outb(ne->base + DP_RSAR0, addr & 0xFF);
    outb(ne->base + DP_RSAR1, (addr >> 8) & 0xFF);
    outb(ne->base + DP_RBCR0, size & 0xFF);
    outb(ne->base + DP_RBCR1, (size >> 8) & 0xFF);
    
    // Start the transfer.
    outb(ne->base + DP_CR, mode | CR_PS_P0 | CR_STA);
}

/*
 * @brief Reads a block of data from the card's local RAM.
 *
 * This function initiates a remote DMA read and then uses insb/insw to
 * transfer the data into a local buffer.
 *
 * @param ne A pointer to the network card state structure.
 * @param addr The starting address in the card's RAM.
 * @param size The number of bytes to read.
 * @param dst A pointer to the destination buffer.
 */
void ne_getblock(ne_t* ne, ushort addr, int size, void* dst) {
    ne_rdma_setup(ne, CR_DM_RR, addr, size);
    if (ne->is16bit) {
        insw(ne->base + NE_DATA, dst, size);
    } else {
        insb(ne->base + NE_DATA, dst, size);
    }
}

/*
 * @brief Initiates the transmission of a packet.
 *
 * This function sets the transmission registers and triggers the send command.
 *
 * @param ne A pointer to the network card state structure.
 * @param page The starting page of the packet in the card's local RAM.
 * @param size The size of the packet in bytes.
 */
void ne_start_xmit(ne_t* ne, int page, int size) {
    outb(ne->base + DP_TPSR, page);
    outb(ne->base + DP_TBCR0, size & 0xFF);
    outb(ne->base + DP_TBCR1, (size >> 8) & 0xFF);
    outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA | CR_TXP);
}

/*
 * @brief Writes a packet to the card's local buffer and transmits it.
 *
 * This function handles the full process of preparing and sending a packet,
 * including queue management and remote DMA write.
 *
 * @param ne A pointer to the network card state structure.
 * @param packet A pointer to the packet data.
 * @param size The size of the packet.
 * @return The number of bytes written on success, or 0 on failure.
 */
int ne_pio_write(ne_t* ne, uchar* packet, int size) {
    int q = ne->sendq_head % SENDQ_LEN;
    
    // Check for a free send buffer slot.
    if (ne->sendq[q].filled || ne->sendq_head > ne->sendq_tail) {
        cprintf("%s: all transmitting buffers in NIC are busy.\n", ne->name);
        return 0;
    }

    // Write the packet data to the card's local RAM via remote DMA.
    ne_rdma_setup(ne, CR_DM_RW, ne->sendq[q].sendpage * DP_PAGESIZE, size);
    if (ne->is16bit) {
        outsw(ne->base + NE_DATA, packet, size);
    } else {
        outsb(ne->base + NE_DATA, packet, size);
    }

    // Mark the queue slot as filled and initiate transmission.
    ne->sendq[q].filled = TRUE;
    ne_start_xmit(ne, ne->sendq[q].sendpage, size);

    // Advance the send queue head.
    ne->sendq_head++;
    
    return size;
}

// [11] Storage Format
typedef struct {
    uchar status;       // Receive Status
    uchar next;         // Next Packet Pointer
    uchar rbc0, rbc1;   // Receive Byte Count
} ne_recv_hdr;

/*
 * @brief Reads a packet from the card's local buffer.
 *
 * This function checks for a new packet, reads its header and data from the
 * ring buffer, and handles wrap-around if the packet spans the buffer boundary.
 *
 * @param ne A pointer to the network card state structure.
 * @param buf The buffer to store the received packet.
 * @param bufsize The size of the buffer.
 * @return The packet size on success, 0 if no packet exists, or -1 on error.
 */
int ne_pio_read(ne_t* ne, uchar* buf, int bufsize) {
    uint pktsize;
    ne_recv_hdr header;
    uint curr, bnry, page;

    // Switch to page 1 to read the CURR pointer.
    outb(ne->base + DP_CR, CR_PS_P1);
    curr = inb(ne->base + DP_CURR);
    
    // Switch back to page 0 to read the BNRY pointer.
    outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA);
    bnry = inb(ne->base + DP_BNRY);
    
    // The next packet to read is at the page following BNRY.
    page = bnry + 1;
    if (page == ne->recv_stoppage) {
        page = ne->recv_startpage;
    }

    // Check if the current page to read is the same as the page the NIC is
    // currently writing to. If so, there are no unread packets.
    if (page == curr) {
        cprintf("%s: No packet to read.\n", ne->name);
        return 0;
    }

    // Read the 4-byte header of the next packet.
    ne_getblock(ne, page * DP_PAGESIZE, sizeof(header), &header);
    pktsize = (header.rbc0 | (header.rbc1 << 8)) - sizeof(header);
    
    // Validate packet size and status.
    if (pktsize < ETH_MIN_SIZE || pktsize > ETH_MAX_SIZE || (header.status & RSR_PRX) == 0) {
        cprintf("%s: Bad packet (size: %d, status: 0x%x)\n", ne->name, pktsize, header.status);
        return -1;
    }

    // If a buffer is provided and it is large enough, read the packet data.
    if (buf != 0 && (uint)bufsize >= pktsize) {
        // Read data, handling wrap-around from end to start.
        uint remain = (ne->recv_stoppage - page) * DP_PAGESIZE;
        if (remain < pktsize) {
            ne_getblock(ne, page * DP_PAGESIZE + sizeof(header), remain, buf);
            ne_getblock(ne, ne->recv_startpage * DP_PAGESIZE, pktsize - remain, buf + remain);
        } else {
            ne_getblock(ne, page * DP_PAGESIZE + sizeof(header), pktsize, buf);
        }
    } else if (buf == 0 || (uint)bufsize < pktsize) {
        return pktsize;
    }

    // Advance BNRY to one page before the next packet. This tells the NIC
    // that this packet has been read and the space can be reclaimed.
    bnry = header.next - 1;
    outb(ne->base + DP_BNRY,
         bnry < (uint)ne->recv_startpage ?
         (uint)(ne->recv_stoppage - 1) : bnry);

    return pktsize;
}

/*
 * @brief Interrupt handler for the network card.
 *
 * This function is called by the system's interrupt handler when the NIC
 * triggers an interrupt. It reads the ISR to determine the cause and handles
 * received or transmitted packets.
 *
 * @param ne A pointer to the network card state structure.
 */
void ne_interrupt(ne_t* ne) {
    int isr;

    // Loop as long as there are pending interrupts.
    while ((isr = inb(ne->base + DP_ISR)) != 0) {
        // Acknowledge the interrupt by writing back the ISR value.
        outb(ne->base + DP_ISR, isr);
        
        if (isr & ISR_PTX) {
            // Packet Transmit (PTX) interrupt.
            ne->sendq_tail++;
            ne->sendq[ne->sendq_tail % SENDQ_LEN].filled = FALSE;
            cprintf("%s: packet transmitted with no error.\n", ne->name);
        }
        if (isr & ISR_PRX) {
            // Packet Receive (PRX) interrupt.
            cprintf("%s: packet received with no error.\n", ne->name);
        }
    }
}