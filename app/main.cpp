#include <open62541/server.h>
#include <open62541/server_config_default.h>

static char kLocale[]  = "en-US";
static char kVarName[] = "foobar";

static float sCurrentValue = 0.0f;

static void updateVariable(UA_Server *server, void * /*data*/) {
    sCurrentValue += 1.0f;
    if (sCurrentValue > 1000.0f) {
        sCurrentValue = 0.0f;
    }

    UA_Variant value;
    UA_Variant_setScalar(&value, &sCurrentValue, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, UA_NODEID_STRING(1, kVarName), value);
}

int main(void) {
    UA_Server *server = UA_Server_new();
    if (!server) {
        return EXIT_FAILURE;
    }
    UA_ServerConfig_setDefault(UA_Server_getConfig(server));

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
    UA_NodeId parentRefId   = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_NodeId typeDefId     = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);

    UA_Server_addVariableNode(server, nodeId, parentNodeId, parentRefId,
                              browseN, typeDefId, attr, nullptr, nullptr);

    // Increment variable by 1.0 every 100 ms; reset to 0 when exceeding 1000
    UA_UInt64 callbackId;
    UA_Server_addRepeatedCallback(server, updateVariable, nullptr, 100, &callbackId);

    UA_Boolean running = true;
    UA_StatusCode retval = UA_Server_run(server, &running);

    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
