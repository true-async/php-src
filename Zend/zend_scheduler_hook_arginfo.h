/* This is a generated file, edit zend_scheduler_hook.stub.php instead.
 * Stub hash: 06651f905db71f840fb874392eb3edf6709a87c7 */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_SchedulerHook_register, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, module, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, hooks, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_SchedulerHook_getModule, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_SchedulerHook_defer, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, task, IS_CALLABLE, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(Async_SchedulerHook, register);
ZEND_METHOD(Async_SchedulerHook, getModule);
ZEND_METHOD(Async_SchedulerHook, defer);

static const zend_function_entry class_Async_SchedulerHook_methods[] = {
	ZEND_ME(Async_SchedulerHook, register, arginfo_class_Async_SchedulerHook_register, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Async_SchedulerHook, getModule, arginfo_class_Async_SchedulerHook_getModule, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Async_SchedulerHook, defer, arginfo_class_Async_SchedulerHook_defer, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_Async_SchedulerHook(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "SchedulerHook", class_Async_SchedulerHook_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL);

	zval const_LAUNCH_value;
	zend_string *const_LAUNCH_value_str = zend_string_init("launch", strlen("launch"), 1);
	ZVAL_STR(&const_LAUNCH_value, const_LAUNCH_value_str);
	zend_string *const_LAUNCH_name = zend_string_init_interned("LAUNCH", sizeof("LAUNCH") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_LAUNCH_name, &const_LAUNCH_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_LAUNCH_name, true);

	zval const_SHUTDOWN_value;
	zend_string *const_SHUTDOWN_value_str = zend_string_init("shutdown", strlen("shutdown"), 1);
	ZVAL_STR(&const_SHUTDOWN_value, const_SHUTDOWN_value_str);
	zend_string *const_SHUTDOWN_name = zend_string_init_interned("SHUTDOWN", sizeof("SHUTDOWN") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_SHUTDOWN_name, &const_SHUTDOWN_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_SHUTDOWN_name, true);

	zval const_INTERCEPT_FIBER_value;
	zend_string *const_INTERCEPT_FIBER_value_str = zend_string_init("intercept_fiber", strlen("intercept_fiber"), 1);
	ZVAL_STR(&const_INTERCEPT_FIBER_value, const_INTERCEPT_FIBER_value_str);
	zend_string *const_INTERCEPT_FIBER_name = zend_string_init_interned("INTERCEPT_FIBER", sizeof("INTERCEPT_FIBER") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_INTERCEPT_FIBER_name, &const_INTERCEPT_FIBER_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_INTERCEPT_FIBER_name, true);

	zval const_ENQUEUE_value;
	zend_string *const_ENQUEUE_value_str = zend_string_init("enqueue_coroutine", strlen("enqueue_coroutine"), 1);
	ZVAL_STR(&const_ENQUEUE_value, const_ENQUEUE_value_str);
	zend_string *const_ENQUEUE_name = zend_string_init_interned("ENQUEUE", sizeof("ENQUEUE") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_ENQUEUE_name, &const_ENQUEUE_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_ENQUEUE_name, true);

	zval const_SUSPEND_value;
	zend_string *const_SUSPEND_value_str = zend_string_init("suspend", strlen("suspend"), 1);
	ZVAL_STR(&const_SUSPEND_value, const_SUSPEND_value_str);
	zend_string *const_SUSPEND_name = zend_string_init_interned("SUSPEND", sizeof("SUSPEND") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_SUSPEND_name, &const_SUSPEND_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_SUSPEND_name, true);

	zval const_RESUME_value;
	zend_string *const_RESUME_value_str = zend_string_init("resume", strlen("resume"), 1);
	ZVAL_STR(&const_RESUME_value, const_RESUME_value_str);
	zend_string *const_RESUME_name = zend_string_init_interned("RESUME", sizeof("RESUME") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_RESUME_name, &const_RESUME_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_RESUME_name, true);

	zval const_CANCEL_value;
	zend_string *const_CANCEL_value_str = zend_string_init("cancel", strlen("cancel"), 1);
	ZVAL_STR(&const_CANCEL_value, const_CANCEL_value_str);
	zend_string *const_CANCEL_name = zend_string_init_interned("CANCEL", sizeof("CANCEL") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_CANCEL_name, &const_CANCEL_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_CANCEL_name, true);

	zval const_CONTEXT_FIND_value;
	zend_string *const_CONTEXT_FIND_value_str = zend_string_init("context_find", strlen("context_find"), 1);
	ZVAL_STR(&const_CONTEXT_FIND_value, const_CONTEXT_FIND_value_str);
	zend_string *const_CONTEXT_FIND_name = zend_string_init_interned("CONTEXT_FIND", sizeof("CONTEXT_FIND") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_CONTEXT_FIND_name, &const_CONTEXT_FIND_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_CONTEXT_FIND_name, true);

	zval const_CONTEXT_SET_value;
	zend_string *const_CONTEXT_SET_value_str = zend_string_init("context_set", strlen("context_set"), 1);
	ZVAL_STR(&const_CONTEXT_SET_value, const_CONTEXT_SET_value_str);
	zend_string *const_CONTEXT_SET_name = zend_string_init_interned("CONTEXT_SET", sizeof("CONTEXT_SET") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_CONTEXT_SET_name, &const_CONTEXT_SET_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_CONTEXT_SET_name, true);

	zval const_CONTEXT_UNSET_value;
	zend_string *const_CONTEXT_UNSET_value_str = zend_string_init("context_unset", strlen("context_unset"), 1);
	ZVAL_STR(&const_CONTEXT_UNSET_value, const_CONTEXT_UNSET_value_str);
	zend_string *const_CONTEXT_UNSET_name = zend_string_init_interned("CONTEXT_UNSET", sizeof("CONTEXT_UNSET") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_CONTEXT_UNSET_name, &const_CONTEXT_UNSET_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_CONTEXT_UNSET_name, true);

	zval const_GC_DESTRUCTORS_value;
	zend_string *const_GC_DESTRUCTORS_value_str = zend_string_init("gc_destructors", strlen("gc_destructors"), 1);
	ZVAL_STR(&const_GC_DESTRUCTORS_value, const_GC_DESTRUCTORS_value_str);
	zend_string *const_GC_DESTRUCTORS_name = zend_string_init_interned("GC_DESTRUCTORS", sizeof("GC_DESTRUCTORS") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_GC_DESTRUCTORS_name, &const_GC_DESTRUCTORS_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_GC_DESTRUCTORS_name, true);

	zval const_DEFER_value;
	zend_string *const_DEFER_value_str = zend_string_init("defer", strlen("defer"), 1);
	ZVAL_STR(&const_DEFER_value, const_DEFER_value_str);
	zend_string *const_DEFER_name = zend_string_init_interned("DEFER", sizeof("DEFER") - 1, true);
	zend_declare_typed_class_constant(class_entry, const_DEFER_name, &const_DEFER_value, ZEND_ACC_PUBLIC, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING));
	zend_string_release_ex(const_DEFER_name, true);

	return class_entry;
}
