#include "../types.h"
#include "../x86.h"
#include "../defs.h"
#include "../net/net.h"
#include "ne.h"

// Comments containing [page numbers] refer to sections in the DP8390 specification.
// DP8390 Datasheet: http://www.national.com/ds/DP/DP8390D.pdf

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
    // Switch to page 1, save MAR5, then write 0xFF.
    outb(ne->base + DP_CR, CR_STP | CR_NO_DMA | CR_PS_P1);
    regd = inb(ne->base + DP_MAR5);
    outb(ne->base + DP_MAR5, 0xFF);
    // [17] On page 1 MAR5 mirrors CNTR0 on page 0.
    outb(ne->base + DP_CR, CR_NO_DMA | CR_PS_P0);
    // [29] CNTR0 increments on CRC errors and clears on read.
    inb(ne->base + DP_CNTR0);
    if (inb(ne->base + DP_CNTR0) != 0) {
      // Unexpected value, restore registers.
      outb(ne->base, reg0);
      outb(ne->base + DP_TCR, regd);
      cprintf("%s: This is not NEx000.\n", ne->name);
      return FALSE;
    }
  }

  // Reset the board.
  {
    int i = 0;
    // Toggle the reset port to trigger a hardware reset.
    outb(ne->base + NE_RESET, inb(ne->base + NE_RESET));
    // Poll the interrupt status register until reset completes.
    while (inb(ne->base + DP_ISR) == 0) {
      // Ensure loop termination; precise 20 ms timeout is ideal but unavailable.
      if (i++ > 10000) {
        cprintf("%s: NIC reset failure\n", ne->name);
        return FALSE;
      }
    }
    // [20] An ISR bit set to 1 means no interrupt pending.
    // Clear all bits manually since CPU interrupts are disabled.
    outb(ne->base + DP_ISR, 0xFF);
  }

  // Read 16 bytes from the PROM (32 bytes on the wire) using the standard
  // initialization sequence described in the datasheet.
  {
    int i;
    // [27] Some DMA registers are 16-bit but must be accessed byte-wise
    // (e.g., RBCR0 and RBCR1).
    // [29] The following sequence follows the reference initialization.
    struct {
      uchar offset, value;
    } seq[] = {
      // 1. Select page 0 for writing; disable DMA and take the NIC offline.
      { DP_CR, CR_NO_DMA | CR_PS_P0 | CR_STP },
      // 2. Byte-wide burst transfers.
      { DP_DCR, (DCR_BMS | DCR_8BYTES) },
      // 3. Clear the byte count registers.
      { DP_RBCR0, 0x00 }, { DP_RBCR1, 0x00 },
      // 4. Enable monitor mode (received frames discarded).
      { DP_RCR, RCR_MON },
      // 5. Enter internal loopback mode.
      { DP_TCR, TCR_INTERNAL },
      // (6 unused)
      // 7. Clear interrupt status.
      { DP_ISR, 0xFF },
      // 8. Mask all interrupts.
      { DP_IMR, 0x00 },
      // (9 unused)

      // Configure PROM read of 32 bytes starting at address 0x00.
      { DP_RBCR0, 32 }, { DP_RBCR1, 0 },
      { DP_RSAR0, 0x00 }, { DP_RSAR1, 0x00 },
      // 10. Switch to page 0 read mode; NIC is online but receive DMA
      //     remains halted by loopback.
      { DP_CR, (CR_PS_P0 | CR_DM_RR | CR_STA) },
    };
    for (i = 0; i < NELEM(seq); ++i)
      outb(ne->base + seq[i].offset, seq[i].value);
    // Detect whether the NIC is 8-bit (NE1000) or 16-bit (NE2000).
    ne->is16bit = TRUE;
    for (i = 0; i < 32; i += 2) {
      eprom[i+0] = inb(ne->base + NE_DATA);
      eprom[i+1] = inb(ne->base + NE_DATA);
      // NE2000 clones duplicate each byte; mismatches imply an NE1000.
      if (eprom[i+0] != eprom[i+1])
        ne->is16bit = FALSE;
    }
    // Normalize to 16 bytes when in 16-bit mode.
    if (ne->is16bit)
      for (i = 0; i < 16; ++i)
        eprom[i] = eprom[i*2];
    // Signature bytes 14 and 15 should be 0x57.
    if (eprom[14] != 0x57 || eprom[15] != 0x57)
      return FALSE;
  }
  
  // Store the MAC address.
  for (i = 0; i < 6; ++i)
    ne->address[i] = eprom[i];
  
  return TRUE;
}

// Initialize the NIC.
void
ne_init(ne_t* ne)
{
  int i;

  // Calculate page layout parameters.
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
  // Initialize send queue bookkeeping.
  for (i = 0; i < SENDQ_LEN; ++i) {
    ne->sendq[i].sendpage = ne->send_startpage + i * SENDQ_PAGES;
    ne->sendq[i].filled = 0;
  }
  ne->sendq_head = 0;
  ne->sendq_tail = SENDQ_LEN-1;

  // Display status information.
  cprintf("%s: NE%d000 (%dkB RAM) at 0x%x:%d - ",
         ne->name,
         ne->is16bit ? 2 : 1,
         ne->ramsize/1024,
         ne->base,
         ne->irq);
  for (i = 0; i < 6; ++i)
    cprintf("%x%s", ne->address[i], i < 5 ? ":" : "\n");

  // [29] Core initialization sequence.
  {
    struct {
      uchar offset, value;
    } seq[] = {
      // 1. Command Register.
      { DP_CR, CR_PS_P0 | CR_STP | CR_NO_DMA },
      // 2. Data Configuration Register. [5(PRQ)] 16-bit mode when LAS is unset.
      { DP_DCR, ((ne->is16bit ? DCR_WORDWIDE : DCR_BYTEWIDE) |
                 DCR_LTLENDIAN | DCR_8BYTES | DCR_BMS) },
      // 3. Receive Configuration Register.
      { DP_RCR, RCR_MON },
      // 4. Clear Remote Byte Count.
      { DP_RBCR0, 0 }, { DP_RBCR1, 0 },
      // 5. Transmit Configuration Register.
      { DP_TCR, TCR_INTERNAL },
      // 6. Initialize ring buffer [10].
      { DP_PSTART, ne->recv_startpage },
      { DP_PSTOP, ne->recv_stoppage },
      { DP_BNRY, ne->recv_startpage }, // One page behind CURR.
      // 7. Clear ISR.
      { DP_ISR, 0xFF },
      // 8. Enable all interrupt masks.
      { DP_IMR, (IMR_PRXE | IMR_PTXE | IMR_RXEE |
                 IMR_TXEE | IMR_OVWE | IMR_CNTE) },
      // 9. Switch to page 1.
      { DP_CR, CR_PS_P1 | CR_NO_DMA },
      // 9.i Load MAC address into PAR0-5.
      { DP_PAR0, ne->address[0] }, { DP_PAR1, ne->address[1] },
      { DP_PAR2, ne->address[2] }, { DP_PAR3, ne->address[3] },
      { DP_PAR4, ne->address[4] }, { DP_PAR5, ne->address[5] },
      // 9.ii Initialize multicast filter.
      { DP_MAR0, 0xFF }, { DP_MAR1, 0xFF }, { DP_MAR2, 0xFF },
      { DP_MAR3, 0xFF }, { DP_MAR4, 0xFF }, { DP_MAR5, 0xFF },
      { DP_MAR6, 0xFF }, { DP_MAR7, 0xFF },
      // 9.iii Initialize current page pointer.
      { DP_CURR, ne->recv_startpage + 1 },
      // 10. Start NIC (0x22); remote DMA remains idle.
      { DP_CR, CR_STA | CR_NO_DMA },
      // 11. Enable transmitter for normal operation.
      { DP_TCR, TCR_NORMAL },
      // Receiver set to promiscuous mode for now.
      { DP_RCR, RCR_PRO },
    };
    for (i = 0; i < NELEM(seq); ++i)
      outb(ne->base + seq[i].offset, seq[i].value);
  }

  return;
}

// Configure remote DMA for read or write operations.
// mode: one of CR_DM_R*, addr: local buffer physical address, size: bytes.
void
ne_rdma_setup(ne_t* ne, int mode, ushort addr, int size)
{
  // [13-14] Writes require issuing a Port ReQuest (PRQ).
  if (mode == CR_DM_RW) {
    // Perform a dummy DMA read to trigger PRQ.
    uchar dummy[4];
    ushort safeloc = ne->startaddr - sizeof(dummy);
    int oldcrda, newcrda;
    oldcrda = inb(ne->base + DP_CRDA0);
    oldcrda |= ((inb(ne->base + DP_CRDA1) << 8) & 0xFF00);
    ne_getblock(ne, safeloc, sizeof(dummy), dummy);
    // Poll until the current DMA address changes.
    do {
      newcrda = inb(ne->base + DP_CRDA0);
      newcrda |= ((inb(ne->base + DP_CRDA1) << 8) & 0xFF00);
    } while (oldcrda == newcrda);
  }
  // Remote DMA transfers through the data port; each byte decrements RBCR
  // and increments RSAR. Transfer ends when RBCR reaches zero.
  outb(ne->base + DP_RSAR0, addr & 0xFF);
  outb(ne->base + DP_RSAR1, (addr >> 8) & 0xFF);
  outb(ne->base + DP_RBCR0, size & 0xFF);
  outb(ne->base + DP_RBCR1, (size >> 8) & 0xFF);
  // Start the remote DMA operation.
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
// Transmits 'size' bytes starting at 'page' in local buffer memory.
void
ne_start_xmit(ne_t* ne, int page, int size)
{
  outb(ne->base + DP_TPSR, page);
  outb(ne->base + DP_TBCR0, size & 0xFF);
  outb(ne->base + DP_TBCR1, (size >> 8) & 0xFF);
  // [12,19] Asserting TXP starts the transfer; the flag clears on
  // completion or failure. TBCR and TPSR must be configured first.
  outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA | CR_TXP); // start!
  return;
}

// Write a packet of 'size' bytes into local memory and transmit it.
// 'size' must be within [46, 1514].
int
ne_pio_write(ne_t* ne, uchar* packet, int size)
{
  // Ensure the head of the send queue is available.
  int q = ne->sendq_head % SENDQ_LEN;
  if (ne->sendq[q].filled || ne->sendq_head > ne->sendq_tail) {
    cprintf("%s: all transmitting buffers in NIC are busy.\n", ne->name);
    return 0;
  }

  // Copy packet into NIC memory.
  ne_rdma_setup(ne, CR_DM_RW, ne->sendq[q].sendpage * DP_PAGESIZE, size);
  if (ne->is16bit)
    outsw(ne->base + NE_DATA, packet, size);
  else
    outsb(ne->base + NE_DATA, packet, size);
  // TODO: determine whether ISR_RDC needs checking here.

  ne->sendq[q].filled = TRUE;
  ne_start_xmit(ne, ne->sendq[q].sendpage, size);

  // TODO: handle possible arithmetic overflow.
  ne->sendq_head++;

  return size;
}

// [11] Strage Format
typedef struct {
  uchar status;           // Receive Status
  uchar next;             // Next Packet Pointer
  uchar rbc0, rbc1;       // Receive Byte Count 0 (low)
} ne_recv_hdr;            // Receive Byte Count 1 (high)

// Read the next packet from the ring buffer starting at 'page'.
// If 'buf' is NULL or 'bufsize' is insufficient, return the packet length.
// Returns the length read on success, 0 if no packet is available,
// and -1 on error.
int
ne_pio_read(ne_t* ne, uchar* buf, int bufsize)
{
  uint pktsize;
  ne_recv_hdr header;
  uint curr, bnry, page;
  // CURR: next page NIC will write.
  // BNRY: page preceding the next packet to read.
  // Ring overflow occurs when CURR catches up with BNRY.
  outb(ne->base + DP_CR, CR_PS_P1); // CURR resides on page 2.
  curr = inb(ne->base + DP_CURR);
  outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA);
  bnry = inb(ne->base + DP_BNRY);
  page = bnry + 1;
  
  // Wrap to start when reaching the end.
  if (page == ne->recv_stoppage)
    page = ne->recv_startpage;

  // If CURR equals BNRY+1, there is no packet to read.
  if (page == curr) {
    cprintf("%s: no packet to read\n", ne->name);
    return 0;
  }

  // Read the header located at the start of the buffer.
  ne_getblock(ne, page * DP_PAGESIZE, sizeof(header), &header);
  // Determine packet size.
  pktsize = (header.rbc0 | (header.rbc1 << 8)) - sizeof(header);

  // Validate packet length against Ethernet limits.
  if (pktsize < ETH_MIN_SIZE || pktsize > ETH_MAX_SIZE) {
    cprintf("%s: Packet with strange length arrived: %d\n",
            ne->name, pktsize);
    return -1;
  }
  // Verify receive status.
  if ((header.status & RSR_PRX) == 0) {
    cprintf("%s: Bad status: %d\n", ne->name, header.status);
    return -1;
  }

  if (buf == 0 || pktsize > bufsize) {
    return pktsize;
  } else {
    // Read data, handling possible wrap-around.
    int remain = (ne->recv_stoppage - page) * DP_PAGESIZE;
    if (remain < pktsize) {
      ne_getblock(ne, page * DP_PAGESIZE + sizeof(header), remain, buf);
      ne_getblock(ne, ne->recv_startpage * DP_PAGESIZE, pktsize - remain, buf);
    } else {
      ne_getblock(ne, page * DP_PAGESIZE + sizeof(header), pktsize, buf);
    }
  }

  // Advance BNRY to one page before the next packet.
  bnry = header.next - 1;
  outb(ne->base + DP_BNRY,
       bnry < ne->recv_startpage ? ne->recv_stoppage-1 : bnry);

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
      ne->sendq[ne->sendq_tail%SENDQ_LEN].filled = FALSE;
      cprintf("%s: packet transmitted with no error.\n", ne->name);
    }
    if (isr & ISR_PRX) {
      cprintf("%s: packet received with no error.\n", ne->name);
    }
#if 0
    if (~(isr & (ISR_PTX | ISR_PRX)) != 0) {
      cprintf("%s: this interrupt event is not implemented now.\n", ne->name);
      panic("panic end\n");
    }
#endif
  }
}






