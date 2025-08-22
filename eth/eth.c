#include "../types.h"
#include "../defs.h"
#include "../fs.h"
#include "../file.h"
#include "../traps.h"
#include "ne.h"
#include "eth.h"

// A single static instance of the network card driver state.
static ne_t ne;

/*
 * @brief Handles interrupts from the Ethernet card.
 *
 * This function is registered in the trap handler to be called when an
 * interrupt from the NE2000-compatible card is received.
 */
void ethintr() {
    ne_interrupt(&ne);
}

/*
 * @brief Handles device-specific I/O control requests for the Ethernet device.
 *
 * This function processes ioctl requests, which are a mechanism for applications
 * to communicate with the kernel to perform device-specific operations.
 *
 * @param ip The inode of the device, currently unused.
 * @param request The specific ioctl command.
 * @param p A pointer to data related to the request.
 * @return Returns 0 on success, or an error code on failure.
 */
int ethioctl(struct inode* ip, int request, void* p) {
    // Unused parameters are explicitly cast to void to prevent compiler warnings.
    (void)ip;
    (void)p;

    // A switch statement is used to handle different ioctl requests.
    switch (request) {
        case ETH_IPC_SETUP:
            // This request is not yet implemented because the IPC mechanism is
            // not yet available. Since this is critical functionality, panic.
            panic("ETH_IPC_SETUP is unimplemented due to missing IPC functionality");

        default:
            // For any unrecognized request, a message can be logged, and an
            // appropriate error can be returned.
            cprintf("%s: Received unrecognized ioctl request %d.\n", ne.name, request);
            return -1; // Or another appropriate error code.
    }

    return 0; // Success.
}

/*
 * @brief Reads data from the Ethernet device.
 *
 * This function is part of the file system's device switch table (devsw) and is
 * called when a user application reads from the Ethernet device's file.
 *
 * @param ip The inode of the device, currently unused.
 * @param p The buffer to write the read data to.
 * @param n The number of bytes to read.
 * @return The number of bytes read, or an error code.
 */
int ethread(struct inode* ip, char* p, int n) {
    (void)ip; // Unused
    return ne_pio_read(&ne, (uchar*)p, n);
}

/*
 * @brief Writes data to the Ethernet device.
 *
 * This function is part of the file system's device switch table (devsw) and is
 * called when a user application writes to the Ethernet device's file.
 *
 * @param ip The inode of the device, currently unused.
 * @param p The buffer containing the data to write.
 * @param n The number of bytes to write.
 * @return The number of bytes written, or an error code.
 */
int ethwrite(struct inode* ip, char* p, int n) {
    (void)ip; // Unused
    return ne_pio_write(&ne, (uchar*)p, n);
}

/*
 * @brief Initializes the Ethernet network card.
 *
 * This is the primary initialization function for the Ethernet device. It
 * probes for a NE2000-compatible card at various common I/O ports. If found,
 * it registers the device with the system and enables interrupts.
 */
void ethinit() {
    char name[] = "eth#";
    int ports[] = {0x300, 0xC100, 0x240, 0x280, 0x320, 0x340, 0x360};

    // Register the device's functions with the device switch table.
    devsw[ETHERNET].write = ethwrite;
    devsw[ETHERNET].read = ethread;
    devsw[ETHERNET].ioctl = ethioctl;

    // Loop through the list of common I/O ports to find a working card.
    for (int i = 0; (uint)i < NELEM(ports); ++i) {
        cprintf("Ethernet: Probing port 0x%x.\n", ports[i]);

        // Reset the device state structure for each probe attempt.
        memset(&ne, 0, sizeof(ne));

        // Create a unique name for the device, e.g., "eth0", "eth1", etc.
        name[3] = '0' + i;
        strncpy(ne.name, name, sizeof(ne.name) - 1);

        ne.irq = IRQ_ETH;
        ne.base = ports[i];

        // Attempt to probe and initialize the card.
        if (ne_probe(&ne)) {
            cprintf("Ethernet: Found card at port 0x%x, initializing...\n", ports[i]);
            ne_init(&ne);

            // Enable interrupts for the device.
            picenable(ne.irq);
            ioapicenable(ne.irq, 0);

            // Break the loop once a card is successfully found and initialized.
            break;
        }
    }
}