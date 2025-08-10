#include "../types.h"
#include "../x86.h"
#include "../defs.h"
#include "../net/net.h"
#include "ne.h"

// Notes: the numbers (keywords) in comments refer to pages in the DP8390 specification.
// DP8390 Documents: http://www.national.com/ds/DP/DP8390D.pdf

// Check NIC connectivity and obtain the MAC address.
int
ne_probe(ne_t* ne)
{
  uchar eprom[32];
  int reg0;
  int i;
  
  reg0 = inb(ne->base);
  if (reg0 == 0xFF)
    return FALSE;

  // Verify the presence of the DP8390.
  {
    int regd;
    // Switch to page 1, save MAR5, then write 0xFF.
    outb(ne->base + DP_CR, CR_STP | CR_NO_DMA | CR_PS_P1);
    regd = inb(ne->base + DP_MAR5);
    outb(ne->base + DP_MAR5, 0xFF);
    // [17] MAR5 on page 1 corresponds to CNTR0 on page 0.
    outb(ne->base + DP_CR, CR_NO_DMA | CR_PS_P0);
    // [29] CNTR0 increments on CRC errors and is cleared after the CPU reads it.
    inb(ne->base + DP_CNTR0);
    if (inb(ne->base + DP_CNTR0) != 0) {
      // Values differ, so restore the originals.
      outb(ne->base, reg0);
      outb(ne->base + DP_TCR, regd);
      cprintf("%s: This is not NEx000.\n", ne->name);
      return FALSE;
    }
  }

  // Reset the board.
  {
    int i = 0;
    // Unclear, but this seems to reset the device.
    outb(ne->base + NE_RESET, inb(ne->base + NE_RESET));
    // Poll the reset interrupt status register (ISR).
    while (inb(ne->base + DP_ISR) == 0) {
      // Ensure the loop eventually exits. A precise timeout such as 20 ms would be ideal,
      // but xv6 currently lacks accurate sleep primitives, so this is approximate.
      if (i++ > 10000) {
        cprintf("%s: NIC reset failure\n", ne->name);
        return FALSE;
      }
    }
    // [20] In the ISR, a bit value of 1 means no interrupt.
    // If a bit is 0 an interrupt is triggered, and the CPU would normally re-set the flag,
    // but interrupts to the CPU are still disabled, so set them manually.
    outb(ne->base + DP_ISR, 0xFF);
  }

  // Read 16 bytes from the PROM (actually 32).
  // This apparently requires performing a standard initialization sequence beforehand.
  // (Based on empirical knowledge; detailed reasoning omitted.)
  {
    int i;
    // [27] Some DMA registers are 16-bit, but they must be accessed 8 bits at a time
    // (e.g., RBCR0, RBCR1). [29] seq follows the reference initialization sequence.
    struct {
      uchar offset, value;
    } seq[] = {
      // 1. Page 0 write mode, DMA off, NIC offline.
      { DP_CR, CR_NO_DMA | CR_PS_P0 | CR_STP },
      // 2. Byte-wide access in burst mode.
      { DP_DCR, (DCR_BMS | DCR_8BYTES) },
      // 3. Clear count registers.
      { DP_RBCR0, 0x00 }, { DP_RBCR1, 0x00 },
      // 4. Monitor mode (possibly no memory writes).
      { DP_RCR, RCR_MON },
      // 5. Loopback mode.
      { DP_TCR, TCR_INTERNAL },
      // (Step 6 may be unnecessary?)
      // 7. Initialize ISR.
      { DP_ISR, 0xFF },
      // 8. Disable all interrupts for now.
      { DP_IMR, 0x00 },
      // (Is step 9 unnecessary?)

      // Configure for PROM reads.
      // Read 32 bytes.
      { DP_RBCR0, 32 }, { DP_RBCR1, 0 },
      // Start at memory address 0x00.
      { DP_RSAR0, 0x00 }, { DP_RSAR1, 0x00 },

      // 10. Page 0 read mode. The NIC is online, but local receive DMA remains inactive in loopback.
      { DP_CR, (CR_PS_P0 | CR_DM_RR | CR_STA) },
    };
    for (i = 0; i < NELEM(seq); ++i)
      outb(ne->base + seq[i].offset, seq[i].value);
    // Also check whether the NIC is 8-bit (NE1000?).
    ne->is16bit = TRUE;
    for (i = 0; i < 32; i += 2) {
      eprom[i+0] = inb(ne->base + NE_DATA);
      eprom[i+1] = inb(ne->base + NE_DATA);
      // NE2000 and clones store identical values at odd and even offsets; a mismatch suggests NE1000.
      if (eprom[i+0] != eprom[i+1])
        ne->is16bit = FALSE;
    }
    // Normalize.
    if (ne->is16bit)
      for (i = 0; i < 16; ++i)
        eprom[i] = eprom[i*2];
    // NE2000/NE1000 place 0x57 at bytes 14 and 15.
    if (eprom[14] != 0x57 || eprom[15] != 0x57)
      return FALSE;
  }
  
  // Retrieve the MAC address.
  for (i = 0; i < 6; ++i)
    ne->address[i] = eprom[i];
  
  return TRUE;
}

// Initialize the NIC.
void
ne_init(ne_t* ne)
{
  int i;

  // Compute page-related values.
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
  // Prepare the send queue.
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

  // [29] Basic initialization steps.
  {
    struct {
      uchar offset, value;
    } seq[] = {
      // 1. CR
      { DP_CR, CR_PS_P0 | CR_STP | CR_NO_DMA },
      // 2. DCR. [5(PRQ)] Without LAS specified, use 16-bit mode.
      { DP_DCR, ((ne->is16bit ? DCR_WORDWIDE : DCR_BYTEWIDE) |
                 DCR_LTLENDIAN | DCR_8BYTES | DCR_BMS) },
      // 3. RCR
      { DP_RCR, RCR_MON },
      // 4. RBCR
      { DP_RBCR0, 0 }, { DP_RBCR1, 0 },
      // 5. TCR
      { DP_TCR, TCR_INTERNAL },
      // 6. Initialize ring buffer [10].
      { DP_PSTART, ne->recv_startpage },
      { DP_PSTOP, ne->recv_stoppage },
      { DP_BNRY, ne->recv_startpage }, // Should be one page before CURR.
      // 7. Initialize ISR.
      { DP_ISR, 0xFF },
      // 8. Initialize IMR (enable all interrupts).
      { DP_IMR, (IMR_PRXE | IMR_PTXE | IMR_RXEE |
                 IMR_TXEE | IMR_OVWE | IMR_CNTE) },
      // 9. Switch to page 1, then
      { DP_CR, CR_PS_P1 | CR_NO_DMA },
      // 9.i Initialize PAR0-5.
      { DP_PAR0, ne->address[0] }, { DP_PAR1, ne->address[1] },
      { DP_PAR2, ne->address[2] }, { DP_PAR3, ne->address[3] },
      { DP_PAR4, ne->address[4] }, { DP_PAR5, ne->address[5] },
      // 9.ii Initialize MAR0-7.
      { DP_MAR0, 0xFF }, { DP_MAR1, 0xFF }, { DP_MAR2, 0xFF },
      { DP_MAR3, 0xFF }, { DP_MAR4, 0xFF }, { DP_MAR5, 0xFF },
      { DP_MAR6, 0xFF }, { DP_MAR7, 0xFF },
      // 9.iii Initialize CURRent pointer.
      { DP_CURR, ne->recv_startpage + 1 },
      // 10. Put NIC into start mode (0x22). DMA still inactive.
      { DP_CR, CR_STA | CR_NO_DMA },
      // 11. Modify TCR (start NIC operation?).
      { DP_TCR, TCR_NORMAL },
      // Set RCR to promiscuous mode for now.
      // (Should this be configurable?)
      { DP_RCR, RCR_PRO },
    };
    for (i = 0; i < NELEM(seq); ++i)
      outb(ne->base + seq[i].offset, seq[i].value);
  }

  return;
}

// Set up remote DMA for read/write operations.
// mode: CR_DM_R*, addr: physical address in the local buffer, size in bytes.
void
ne_rdma_setup(ne_t* ne, int mode, ushort addr, int size)
{
  // [13-14] For writes, a Port Request (PRQ) must be issued.
  if (mode == CR_DM_RW) {
    // This code is puzzling.
    //outb(ne->base + DP_ISR, ISR_RDC);
    // Perform a dummy DMA read.
    uchar dummy[4];
    ushort safeloc = ne->startaddr - sizeof(dummy);
    int oldcrda, newcrda;
    oldcrda = inb(ne->base + DP_CRDA0);
    oldcrda |= ((inb(ne->base + DP_CRDA1) << 8) & 0xFF00);
    ne_getblock(ne, safeloc, sizeof(dummy), dummy);
    // Required delay (polling).
    do {
      newcrda = inb(ne->base + DP_CRDA0);
      newcrda |= ((inb(ne->base + DP_CRDA1) << 8) & 0xFF00);
    } while (oldcrda == newcrda);
  }
  // Remote DMA enables bidirectional transfer via the data port.
  // RBCR decreases and RSAR increases as data is transferred.
  // Transfer ends when RBCR reaches 0.
  outb(ne->base + DP_RSAR0, addr & 0xFF);
  outb(ne->base + DP_RSAR1, (addr >> 8) & 0xFF);
  outb(ne->base + DP_RBCR0, size & 0xFF);
  outb(ne->base + DP_RBCR1, (size >> 8) & 0xFF);
  // Start the Remote DMA process.
  outb(ne->base + DP_CR, mode | CR_PS_P0 | CR_STA);
  return;
}

// Read 'size' bytes from RAM at 'addr' into dst.
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

// Begin data transmission.
// Send 'size' bytes starting from page 'page' in the local buffer.
void
ne_start_xmit(ne_t* ne, int page, int size)
{
  outb(ne->base + DP_TPSR, page);
  outb(ne->base + DP_TBCR0, size & 0xFF);
  outb(ne->base + DP_TBCR1, (size >> 8) & 0xFF);
  // [12,19] Setting TXP initiates transmission.
  // The flag resets internally when the transfer completes or fails.
  // TBCR and TPSR must be set beforehand.
  outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA | CR_TXP); // Start transmission!
  return;
}

// Write a 'size'-byte packet to the local buffer and then transmit it.
// 'size' must lie within [46, 1514].
int
ne_pio_write(ne_t* ne, uchar* packet, int size)
{
  // Ensure the head of the send queue is free.
  int q = ne->sendq_head % SENDQ_LEN;
  if (ne->sendq[q].filled || ne->sendq_head > ne->sendq_tail) {
    cprintf("%s: all transmitting buffers in NIC are busy.\n", ne->name);
    return 0;
  }

  // Write operation.
  ne_rdma_setup(ne, CR_DM_RW, ne->sendq[q].sendpage * DP_PAGESIZE, size);
  if (ne->is16bit)
    outsw(ne->base + NE_DATA, packet, size);
  else
    outsb(ne->base + NE_DATA, packet, size);
  // TODO: Should we check ISR_RDC here?

  ne->sendq[q].filled = TRUE;
  ne_start_xmit(ne, ne->sendq[q].sendpage, size);

  // TODO: Likely need to handle arithmetic overflow.
  ne->sendq_head++;
  
  return size;
}

// [11] Storage Format
typedef struct {
  uchar status;           // Receive Status
  uchar next;             // Next Packet Pointer
  uchar rbc0, rbc1;       // Receive Byte Count 0 (low)
} ne_recv_hdr;            // Receive Byte Count 1 (high)

// Read sequential data from the local buffer starting at page 'page'.
// 'bufsize' is the size of 'buf'; if insufficient, return the packet size.
// If 'buf' is NULL, return the packet size.
// If 'buf' is not NULL, return the number of bytes read.
// Return 0 when no packet exists, -1 on error.
int
ne_pio_read(ne_t* ne, uchar* buf, int bufsize)
{
  uint pktsize;
  ne_recv_hdr header;
  uint curr, bnry, page;
  // CURR: next page for NIC to write.
  // BNRY: page preceding the next one to read.
  // Thus, an overflow occurs if CURR catches BNRY.
  outb(ne->base + DP_CR, CR_PS_P1); // Need to switch because CURR resides on page 2.
  curr = inb(ne->base + DP_CURR);
  outb(ne->base + DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STA);
  bnry = inb(ne->base + DP_BNRY);
  page = bnry + 1;
  
  // Wrap to start if at the end.
  if (page == ne->recv_stoppage)
    page = ne->recv_startpage;

  // If we catch up to CURR, there is no packet (can CURR equal stoppage?).
  //if (bnry + 1 == curr || next == curr) {
  if (page == curr) {
    cprintf("%s: no packet to read\n", ne->name);
    return 0;
  }

  // Read the header (Storage Format) at the buffer start.
  ne_getblock(ne, page * DP_PAGESIZE, sizeof(header), &header);
  // Compute packet size.
  pktsize = (header.rbc0 | (header.rbc1 << 8)) - sizeof(header);
  
  // Check whether the length is within the Ethernet packet range.
  if (pktsize < ETH_MIN_SIZE || pktsize > ETH_MAX_SIZE) {
    cprintf("%s: Packet with strange length arrived: %d\n",
            ne->name, pktsize);
    return -1;
  }
  // Verify the status is valid.
  if ((header.status & RSR_PRX) == 0) {
    cprintf("%s: Bad status: %d\n", ne->name, header.status);
    return -1;
  }

  if (buf == 0 || pktsize > bufsize) {
    return pktsize;
  } else {
    // Read data, handling wrap-around from end to start.
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






