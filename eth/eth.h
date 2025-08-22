/*
 * Ethernet driver interface for the Fiwix kernel.
 *
 * This header defines the interface for interacting with the Ethernet driver.
 * Users can open the "eth" device and use read/write operations to transmit or
 * receive Ethernet frames. The ETH_IPC_SETUP ioctl command prepares the device for
 * inter-process communication (IPC), with implementation details pending the
 * availability of an IPC mechanism. See ethtest.c for usage examples.
 */

// Prepare IPC; actual implementation depends on the IPC specification.
#define ETH_IPC_SETUP     1
