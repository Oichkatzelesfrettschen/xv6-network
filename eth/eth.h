
// Ethernet driver interface.
// Open "eth" and use read/write to transmit or receive frames.
// ioctl(fd, ETH_IPC_SETUP, ...) prepares an IPC channel; see ethtest.c.
// IPCをセットアップする。IPCの仕様次第なので、実装はまだできない。
#define ETH_IPC_SETUP     1



