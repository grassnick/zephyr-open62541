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

## Build

Tested with the latest west tooling and zephyr main branch at the time: `https://github.com/zephyrproject-rtos/zephyr/commit/3984c8987aaf1226fed99186ccd8571d6daf21ce`

The following build command was used to build:

``` bash
west build -b ganymed_sk/sy120_gbm
```