/* This is a generated file, edit async_scheduler_hook.stub.php instead.
 * Stub hash: 6ab2e71acad49ec4f00c5daacfd6803bb0f4741a */

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_Async_get_context, 0, 0, Async\\Context, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, coroutine, IS_OBJECT, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Scheduler_onLaunch, 0, 0, IS_OBJECT, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Scheduler_onShutdown, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Scheduler_onFiber, 0, 1, IS_OBJECT, 1)
	ZEND_ARG_OBJ_INFO(0, fiber, Fiber, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Scheduler_onEnqueue, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, coroutine, IS_OBJECT, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, error, Throwable, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Scheduler_onSuspend, 0, 2, IS_OBJECT, 1)
	ZEND_ARG_TYPE_INFO(0, fromMain, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, isBailout, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Scheduler_onDefer, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, task, IS_CALLABLE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_SchedulerHook_register, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, module, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, factory, IS_CALLABLE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_SchedulerHook_getModule, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()

#define arginfo_class_Async_SchedulerHook_defer arginfo_class_Async_Scheduler_onDefer

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Context_find, 0, 1, IS_MIXED, 0)
	ZEND_ARG_TYPE_MASK(0, key, MAY_BE_STRING|MAY_BE_OBJECT, NULL)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Context_has, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_MASK(0, key, MAY_BE_STRING|MAY_BE_OBJECT, NULL)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Async_Context_set, 0, 2, Async\\Context, 0)
	ZEND_ARG_TYPE_MASK(0, key, MAY_BE_STRING|MAY_BE_OBJECT, NULL)
	ZEND_ARG_TYPE_INFO(0, value, IS_MIXED, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Async_Context_unset arginfo_class_Async_Context_has

ZEND_FUNCTION(Async_get_context);
ZEND_METHOD(Async_SchedulerHook, register);
ZEND_METHOD(Async_SchedulerHook, getModule);
ZEND_METHOD(Async_SchedulerHook, defer);
ZEND_METHOD(Async_Context, find);
ZEND_METHOD(Async_Context, has);
ZEND_METHOD(Async_Context, set);
ZEND_METHOD(Async_Context, unset);

static const zend_function_entry ext_functions[] = {
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "get_context"), zif_Async_get_context, arginfo_Async_get_context, 0, NULL, NULL)
	ZEND_FE_END
};

static const zend_function_entry class_Async_Scheduler_methods[] = {
	ZEND_RAW_FENTRY("onLaunch", NULL, arginfo_class_Async_Scheduler_onLaunch, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("onShutdown", NULL, arginfo_class_Async_Scheduler_onShutdown, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("onFiber", NULL, arginfo_class_Async_Scheduler_onFiber, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("onEnqueue", NULL, arginfo_class_Async_Scheduler_onEnqueue, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("onSuspend", NULL, arginfo_class_Async_Scheduler_onSuspend, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("onDefer", NULL, arginfo_class_Async_Scheduler_onDefer, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_FE_END
};

static const zend_function_entry class_Async_SchedulerHook_methods[] = {
	ZEND_ME(Async_SchedulerHook, register, arginfo_class_Async_SchedulerHook_register, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Async_SchedulerHook, getModule, arginfo_class_Async_SchedulerHook_getModule, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Async_SchedulerHook, defer, arginfo_class_Async_SchedulerHook_defer, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};

static const zend_function_entry class_Async_Context_methods[] = {
	ZEND_ME(Async_Context, find, arginfo_class_Async_Context_find, ZEND_ACC_PUBLIC)
	ZEND_ME(Async_Context, has, arginfo_class_Async_Context_has, ZEND_ACC_PUBLIC)
	ZEND_ME(Async_Context, set, arginfo_class_Async_Context_set, ZEND_ACC_PUBLIC)
	ZEND_ME(Async_Context, unset, arginfo_class_Async_Context_unset, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_Async_Scheduler(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "Scheduler", class_Async_Scheduler_methods);
	class_entry = zend_register_internal_interface(&ce);

	return class_entry;
}

static zend_class_entry *register_class_Async_SchedulerHook(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "SchedulerHook", class_Async_SchedulerHook_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL);

	return class_entry;
}

static zend_class_entry *register_class_Async_Context(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "Context", class_Async_Context_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL|ZEND_ACC_NO_DYNAMIC_PROPERTIES|ZEND_ACC_NOT_SERIALIZABLE);

	return class_entry;
}
