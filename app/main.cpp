#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include "zephyr/kernel.h"
#include "zephyr/sys/mem_stats.h"

extern "C" void start_network();

// Provided by lib/libc/common when CONFIG_SYS_HEAP_RUNTIME_STATS=y; the header
// it lives in is not on the application include path.
extern "C" int malloc_runtime_stats_get(struct sys_memory_stats *stats);

// kB with one decimal, without pulling in floating point printf support.
#define KB_WHOLE(bytes) ((bytes) / 1024U)
#define KB_TENTH(bytes) (((bytes) % 1024U) * 10U / 1024U)
#define KB_FMT          "%u.%ukB"
#define KB_ARGS(bytes)  (unsigned)KB_WHOLE(bytes), (unsigned)KB_TENTH(bytes)

static void reportHeap(const char *stage) {
    struct sys_memory_stats stats {};
    if (malloc_runtime_stats_get(&stats) != 0) {
        return;
    }
    printf("[heap] %-18s allocated=" KB_FMT " free=" KB_FMT " peak=" KB_FMT "\n",
           stage, KB_ARGS(stats.allocated_bytes), KB_ARGS(stats.free_bytes),
           KB_ARGS(stats.max_allocated_bytes));
}

static char kLocale[]  = "en-US";
static char kVarName[] = "foobar";

static float sCurrentValue = 0.0f;

static UA_StatusCode writeCurrentValue(UA_Server *server) {
    UA_Variant value;
    UA_Variant_setScalar(&value, &sCurrentValue, &UA_TYPES[UA_TYPES_FLOAT]);
    return UA_Server_writeValue(server, UA_NODEID_STRING(1, kVarName), value);
}


static void updateVariable(UA_Server *server, void * /*data*/) {
    sCurrentValue += 1.0f;
    if (sCurrentValue > 1000.0f) {
        sCurrentValue = 0.0f;
    }

    // No %f: CONFIG_PICOLIBC_IO_FLOAT is off, so print the value as an int.
    UA_StatusCode wr = writeCurrentValue(server);
    if (wr != UA_STATUSCODE_GOOD) {
        printf("periodic write failed: %s (value=%d)\n", UA_StatusCode_name(wr),
               (int)sCurrentValue);
    }
}

int main(void) {

    start_network();

    puts("Hello world, this is dog!\n");

    for (auto i = 0; i < 10; i++) {
        k_sleep(K_MSEC(1000));
    }

    puts("Hello world, this is cat!\n");

    reportHeap("before server");

    UA_Server *server = UA_Server_new();
    if (!server) {
        return EXIT_FAILURE;
    }
    reportHeap("after Server_new");

    // NOTE: no UA_ServerConfig_setDefault() here. As of open62541 1.5,
    // UA_Server_new() already applies the default config internally; calling it
    // again on the live config re-runs endpoint and security-policy setup and
    // leaks the first set. The pre-1.2 tutorials still show the old idiom.

    // Add float variable node: ns=1,s=myVariable
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    float initValue             = 0.0f;
    UA_Variant_setScalar(&attr.value, &initValue, &UA_TYPES[UA_TYPES_FLOAT]);
    attr.dataType    = UA_TYPES[UA_TYPES_FLOAT].typeId;
    attr.valueRank   = UA_VALUERANK_SCALAR;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    attr.displayName = UA_LOCALIZEDTEXT(kLocale, kVarName);

    UA_NodeId nodeId        = UA_NODEID_STRING(1, kVarName);
    UA_QualifiedName browseN = UA_QUALIFIEDNAME(1, kVarName);
    UA_NodeId parentNodeId  = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentRefId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_NodeId typeDefId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);

    auto ret = UA_Server_addVariableNode(
        server, nodeId, parentNodeId, parentRefId, browseN, typeDefId, attr, nullptr, nullptr);
    if (ret != UA_STATUSCODE_GOOD)
    {
        printf("manual add node failed: %s\n", UA_StatusCode_name(ret));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }
    reportHeap("after addVariable");

    // Seed the value through the same path the periodic callback uses, so a
    // node that never gets a value fails here instead of leaving clients to
    // discover it as BadWaitingForInitialData.
    ret = writeCurrentValue(server);
    if (ret != UA_STATUSCODE_GOOD)
    {
        printf("initial value write failed: %s\n", UA_StatusCode_name(ret));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    // Increment variable by 1.0 every 100 ms; reset to 0 when exceeding 1000
    UA_UInt64 callbackId;
    ret = UA_Server_addRepeatedCallback(server, updateVariable, nullptr, 100, &callbackId);
    if (ret != UA_STATUSCODE_GOOD)
    {
        printf("repeated callback registration failed: %s\n", UA_StatusCode_name(ret));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    UA_Boolean running = true;
    UA_StatusCode retval = UA_Server_run(server, &running);

    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
