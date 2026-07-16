PHP_ARG_ENABLE([async-scheduler-hook],
  [whether to enable the Async scheduler hook bridge],
  [AS_HELP_STRING([--enable-async-scheduler-hook],
    [Enable the Async scheduler hook bridge (Async\SchedulerHook, Async\Context)])],
  [no])

if test "$PHP_ASYNC_SCHEDULER_HOOK" != "no"; then
  PHP_NEW_EXTENSION([async_scheduler_hook], [async_scheduler_hook.c], [$ext_shared])
fi
