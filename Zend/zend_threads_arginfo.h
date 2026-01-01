/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: e89a8fe1a0ee170b408f3686777896e03820ec1f */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Thread___construct, 0, 0, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, bootstrap, IS_STRING, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Thread_run, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, task, Closure, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, args, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Thread_join, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Thread_kill arginfo_class_Thread_join

ZEND_METHOD(Thread, __construct);
ZEND_METHOD(Thread, run);
ZEND_METHOD(Thread, join);
ZEND_METHOD(Thread, kill);

static const zend_function_entry class_Thread_methods[] = {
	ZEND_ME(Thread, __construct, arginfo_class_Thread___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(Thread, run, arginfo_class_Thread_run, ZEND_ACC_PUBLIC)
	ZEND_ME(Thread, join, arginfo_class_Thread_join, ZEND_ACC_PUBLIC)
	ZEND_ME(Thread, kill, arginfo_class_Thread_kill, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_Thread(void)
{
	zend_class_entry ce, *class_entry;

	INIT_CLASS_ENTRY(ce, "Thread", class_Thread_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL);

	return class_entry;
}
