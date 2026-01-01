/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 896ef3fca544166c4476155a43dbb5428f04def6 */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_PHP_Thread___construct, 0, 0, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, bootstrap, IS_STRING, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_PHP_Thread_run, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, task, PHP\\Closure, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, args, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_PHP_Thread_join, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_PHP_Thread_kill arginfo_class_PHP_Thread_join

ZEND_METHOD(PHP_Thread, __construct);
ZEND_METHOD(PHP_Thread, run);
ZEND_METHOD(PHP_Thread, join);
ZEND_METHOD(PHP_Thread, kill);

static const zend_function_entry class_PHP_Thread_methods[] = {
	ZEND_ME(PHP_Thread, __construct, arginfo_class_PHP_Thread___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(PHP_Thread, run, arginfo_class_PHP_Thread_run, ZEND_ACC_PUBLIC)
	ZEND_ME(PHP_Thread, join, arginfo_class_PHP_Thread_join, ZEND_ACC_PUBLIC)
	ZEND_ME(PHP_Thread, kill, arginfo_class_PHP_Thread_kill, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_PHP_Thread(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "PHP", "Thread", class_PHP_Thread_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL);

	return class_entry;
}
