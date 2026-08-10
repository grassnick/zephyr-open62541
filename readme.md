# Example open62541 OPC UA application for Zephyr RTOS

## Preparation

### open62541

Used version is v1.5.4

There are some issues in the open62541 that need to be fixed in order for it to work properly:


``` patch

diff --git a/arch/zephyr/clock_zephyr.c b/arch/zephyr/clock_zephyr.c
index eeb688b6c..974cf14b0 100644
--- a/arch/zephyr/clock_zephyr.c
+++ b/arch/zephyr/clock_zephyr.c
@@ -39,7 +39,12 @@ UA_DateTime_localTimeUtcOffset(void) {
 
 UA_DateTime
 UA_DateTime_nowMonotonic(void) {
-    return k_uptime_get();
+    /* UA_DateTime counts 100ns ticks, k_uptime_get() counts milliseconds.
+     * Returning the latter directly makes the monotonic clock run 10000x too
+     * slow, so every timer (repeated callbacks, MonitoredItem sampling,
+     * subscription publishing) only comes due after ~10000x its interval.
+     * Convert via ticks->ns to avoid the 1ms quantisation of k_uptime_get(). */
+    return (UA_DateTime)(k_ticks_to_ns_floor64(k_uptime_ticks()) / 100);
 }
 
 #endif
diff --git a/arch/zephyr/eventloop_zephyr.h b/arch/zephyr/eventloop_zephyr.h
index 7bac77f69..eff2ede90 100644
--- a/arch/zephyr/eventloop_zephyr.h
+++ b/arch/zephyr/eventloop_zephyr.h
@@ -43,6 +43,12 @@
 #define UA_AGAIN EAGAIN /* the same as wouldblock on nearly every system */
 #define UA_INPROGRESS EINPROGRESS
 #define UA_WOULDBLOCK EWOULDBLOCK
+#define UA_NOBUFS ENOBUFS /* transient on Zephyr: the net-buf pool is drained */
+
+/* Backoff applied when a send hits ENOBUFS, and how many consecutive times to
+ * retry before giving up on the connection. */
+#define UA_ZEPHYR_SEND_NOBUFS_BACKOFF_MS 5
+#define UA_ZEPHYR_SEND_NOBUFS_RETRIES    100
 #define UA_POLLIN ZSOCK_POLLIN
 #define UA_POLLOUT ZSOCK_POLLOUT
 #define UA_SHUT_RDWR ZSOCK_SHUT_RDWR
@@ -75,7 +81,7 @@
 #define UA_gethostname zsock_gethostname
 
 typedef int UA_socket_fd;
-typedef struct zsock_fd_set UA_fd_set;
+typedef zsock_fd_set UA_fd_set;
 typedef struct zsock_addrinfo UA_addrinfo;
 typedef struct zsock_pollfd UA_pollfd;
 
diff --git a/arch/zephyr/eventloop_zephyr_tcp.c b/arch/zephyr/eventloop_zephyr_tcp.c
index 5ebf8b816..558252270 100644
--- a/arch/zephyr/eventloop_zephyr_tcp.c
+++ b/arch/zephyr/eventloop_zephyr_tcp.c
@@ -649,6 +649,7 @@ TCP_sendWithConnection(UA_ConnectionManager *cm, uintptr_t connectionId,
 
     /* Send the full buffer. This may require several calls to send */
     size_t nWritten = 0;
+    unsigned nobufsRetries = 0;
     do {
         ssize_t n = 0;
         do {
@@ -661,9 +662,22 @@ TCP_sendWithConnection(UA_ConnectionManager *cm, uintptr_t connectionId,
             if(n < 0) {
                 /* An error we cannot recover from? */
                 if(UA_ERRNO != UA_INTERRUPTED && UA_ERRNO != UA_WOULDBLOCK &&
-                   UA_ERRNO != UA_AGAIN)
+                   UA_ERRNO != UA_AGAIN && UA_ERRNO != UA_NOBUFS)
                     goto shutdown;
 
+                /* Zephyr reports a momentarily drained net-buf pool as ENOBUFS.
+                 * Unlike on POSIX this is transient, so it must not close the
+                 * connection. The socket stays writable while the pool is
+                 * empty, so polling would spin: back off instead and let the
+                 * network threads release buffers. Bounded, so a pool that
+                 * never recovers still shuts the connection down. */
+                if(UA_ERRNO == UA_NOBUFS) {
+                    if(++nobufsRetries > UA_ZEPHYR_SEND_NOBUFS_RETRIES)
+                        goto shutdown;
+                    k_msleep(UA_ZEPHYR_SEND_NOBUFS_BACKOFF_MS);
+                    continue;
+                }
+
                 /* Poll for the socket resources to become available and retry
                  * (blocking) */
                 int poll_ret;
@@ -675,6 +689,7 @@ TCP_sendWithConnection(UA_ConnectionManager *cm, uintptr_t connectionId,
             }
         } while(n < 0);
         nWritten += (size_t)n;
+        nobufsRetries = 0; /* progress made, allow the full budget again */
     } while(nWritten < buf->length);
 
     /* Clean up and return */
```


### Ganymed linker scripts

For debugging we can use the 4mb RAM; need to modify the linker file in zephyr -- this is 
experimental so we dont want to push this to mainline zephyr.

```
---
Index: soc/sensry/ganymed/sy1xx/common/linker.ld
IDEA additional info:
Subsystem: com.intellij.openapi.diff.impl.patch.CharsetEP
<+>UTF-8
===================================================================
diff --git a/soc/sensry/ganymed/sy1xx/common/linker.ld b/soc/sensry/ganymed/sy1xx/common/linker.ld
--- a/soc/sensry/ganymed/sy1xx/common/linker.ld	(revision 599b67f7da25d4f4af66886a169f007e3ab866d4)
+++ b/soc/sensry/ganymed/sy1xx/common/linker.ld	(date 1779271775929)
@@ -30,10 +30,10 @@

## ROM AREA ##
#define ROM_BASE        0x1C010100
-#define ROM_SIZE        0x5Fa00
+#define ROM_SIZE        0x1FFF00

## RAM AREA ##
-#define RAM_BASE        0x1C070000
+#define RAM_BASE        0x1C210000
#define RAM_SIZE        0x200000

MEMORY
```


## Build

Tested with the latest west tooling and zephyr main branch at the time: `https://github.com/zephyrproject-rtos/zephyr/commit/3984c8987aaf1226fed99186ccd8571d6daf21ce`

The following build command was used to build:

``` bash
west build -b ganymed_sk/sy120_gbm
```

## Deploy

It is currently not possible to flash the build to ROM of the Ganymed Board.

Only debugger builds are supported.
These require a debugging bootloader installed on the target. Afterwards use the `loader.sh` script to load the image in the RAM.

``` bash
./loader.sh debug_sram zephyr
```

### Flash debug bootloader

Use https://github.com/sensry-de/ganymed-pypi with the debug flashing mode to prepare the board with the debug bootloader.


##

diff --git a/drivers/ethernet/eth_sensry_sy1xx_mac.c b/drivers/ethernet/eth_sensry_sy1xx_mac.c
index 8cc4d7f8318..417bc9b5af1 100644
--- a/drivers/ethernet/eth_sensry_sy1xx_mac.c
+++ b/drivers/ethernet/eth_sensry_sy1xx_mac.c
@@ -425,6 +425,23 @@ static int sy1xx_mac_low_level_receive(const struct device *dev, uint8_t *rx, ui
 	/* rx udma is ready */
 	bytes_transferred = SY1XX_UDMA_READ_REG(cfg->base_addr, SY1XX_UDMA_CFG_REG) & 0x0000ffff;
 	if (bytes_transferred) {
+		/*
+		 * The udma byte count covers the frame plus a trailer the MAC
+		 * appends (FCS and status): a 1514-byte frame is reported as
+		 * 1523. Short frames are unaffected because the stack stops at
+		 * the L3 length and ignores whatever follows, but a full-size
+		 * frame exceeds the RX buffer that net_pkt_alloc_with_buffer()
+		 * grants (NET_ETH_MTU + NET_ETH_MAX_HDR_SIZE) and would then be
+		 * rejected by net_pkt_write().
+		 *
+		 * Clamping is safe rather than truncating: that limit is the
+		 * largest untagged frame this interface can legitimately carry,
+		 * so only trailer bytes are ever dropped.
+		 */
+		if (bytes_transferred > NET_ETH_MTU + NET_ETH_MAX_HDR_SIZE) {
+			bytes_transferred = NET_ETH_MTU + NET_ETH_MAX_HDR_SIZE;
+		}
+
 		/* message received, copy data */
 		memcpy(rx, data->dma_buffers->rx, bytes_transferred);
 		*len = bytes_transferred;
diff --git a/soc/sensry/ganymed/sy1xx/common/linker.ld b/soc/sensry/ganymed/sy1xx/common/linker.ld
index fdf4975ed51..5a6532338c7 100644
--- a/soc/sensry/ganymed/sy1xx/common/linker.ld
+++ b/soc/sensry/ganymed/sy1xx/common/linker.ld
@@ -30,10 +30,10 @@
 
 ## ROM AREA ##
 #define ROM_BASE        0x1C010100
-#define ROM_SIZE        0x5Fa00
+#define ROM_SIZE        0x1FFF00
 
 ## RAM AREA ##
-#define RAM_BASE        0x1C070000
+#define RAM_BASE        0x1C210000
 #define RAM_SIZE        0x200000
 
 MEMORY
