# Example open62541 OPC UA application for Zephyr RTOS

## Preparation

There seems to be a mismatch in the open62541 headers, this small patch fixes it:

``` patch
diff --git a/arch/zephyr/eventloop_zephyr.h b/arch/zephyr/eventloop_zephyr.h
index 7bac77f69..aac893fe3 100644
--- a/arch/zephyr/eventloop_zephyr.h
+++ b/arch/zephyr/eventloop_zephyr.h
@@ -75,7 +75,7 @@
 #define UA_gethostname zsock_gethostname
 
 typedef int UA_socket_fd;
-typedef struct zsock_fd_set UA_fd_set;
+typedef zsock_fd_set UA_fd_set;
 typedef struct zsock_addrinfo UA_addrinfo;
 typedef struct zsock_pollfd UA_pollfd;

```

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
