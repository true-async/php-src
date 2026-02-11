/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 486667d0ccc66c30a311a14354915eccd46a3a5d */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Php_MemoryManager_getHeapId, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Php_MemoryManager_createThread, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, callback, IS_CALLABLE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Php_MemoryManager_joinThread, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, thread_id, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Php_MemoryManager_hasIncomingQueue, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, sender_heap_id, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Php_MemoryManager_collectRemoteFrees, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Php_MemoryManager_getMpscStats, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_FUNCTION(Php_MemoryManager_getHeapId);
ZEND_FUNCTION(Php_MemoryManager_createThread);
ZEND_FUNCTION(Php_MemoryManager_joinThread);
ZEND_FUNCTION(Php_MemoryManager_hasIncomingQueue);
ZEND_FUNCTION(Php_MemoryManager_collectRemoteFrees);
ZEND_FUNCTION(Php_MemoryManager_getMpscStats);

static const zend_function_entry ext_functions[] = {
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Php\\MemoryManager", "getHeapId"), zif_Php_MemoryManager_getHeapId, arginfo_Php_MemoryManager_getHeapId, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Php\\MemoryManager", "createThread"), zif_Php_MemoryManager_createThread, arginfo_Php_MemoryManager_createThread, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Php\\MemoryManager", "joinThread"), zif_Php_MemoryManager_joinThread, arginfo_Php_MemoryManager_joinThread, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Php\\MemoryManager", "hasIncomingQueue"), zif_Php_MemoryManager_hasIncomingQueue, arginfo_Php_MemoryManager_hasIncomingQueue, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Php\\MemoryManager", "collectRemoteFrees"), zif_Php_MemoryManager_collectRemoteFrees, arginfo_Php_MemoryManager_collectRemoteFrees, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Php\\MemoryManager", "getMpscStats"), zif_Php_MemoryManager_getMpscStats, arginfo_Php_MemoryManager_getMpscStats, 0, NULL, NULL)
	ZEND_FE_END
};
