################
 Events
################

The TrueAsync API revolves around the concept of **events**. An event represents any asynchronous action that can produce a result or error in the future. All specific event types share a common base structure ``zend_async_event_t`` which provides memory management, state flags and a standard lifecycle.

**********************
Creating an Event
**********************

To create an event you typically call one of the ``ZEND_ASYNC_NEW_*`` macros which delegate to the underlying implementation. Each macro returns a structure that contains ``zend_async_event_t`` as its first member.

.. code:: c

   zend_async_timer_event_t *timer = ZEND_ASYNC_NEW_TIMER_EVENT(1000, false);
   timer->base.start(&timer->base);

The ``base`` field exposes the common functions ``start``, ``stop`` and ``dispose`` as well as reference counting helpers.

*****************
Event Flags
*****************

Each event has a ``flags`` field that controls its state and behaviour. The most
important flags are:

``ZEND_ASYNC_EVENT_F_CLOSED``
    The event has been closed and may no longer be started or stopped.

``ZEND_ASYNC_EVENT_F_RESULT_USED``
    Indicates that the result will be consumed by an awaiting context. When this
    flag is set the awaiting code keeps the result alive until it is processed
    in the exception handler.

``ZEND_ASYNC_EVENT_F_EXC_CAUGHT``
    Marks that an error produced by the event was caught in a callback and will
    not be rethrown automatically.

``ZEND_ASYNC_EVENT_F_ZVAL_RESULT``
    Signals that the callback result is a ``zval`` pointer. The TrueAsync core
    will properly increment the reference count before inserting the value into
    userland arrays.

``ZEND_ASYNC_EVENT_F_ZEND_OBJ``
    Specifies that the structure also acts as a Zend object implementing
    ``Awaitable``.

``ZEND_ASYNC_EVENT_F_NO_FREE_MEMORY``
    The dispose handler must not free the memory of the event itself. This is
    used when the event is embedded into another structure.

``ZEND_ASYNC_EVENT_F_EXCEPTION_HANDLED``
    Set by the callback once it has fully processed an exception. If this flag
    is not set the exception will be rethrown after callbacks finish.

Convenience macros such as ``ZEND_ASYNC_EVENT_SET_CLOSED`` and
``ZEND_ASYNC_EVENT_IS_EXCEPTION_HANDLED`` are provided to manipulate these
flags.

******************
Event Callbacks
******************

The core maintains a dynamic vector of callbacks for each event. Implementations
provide the ``add_callback`` and ``del_callback`` methods which internally use
``zend_async_callbacks_push`` and ``zend_async_callbacks_remove``. When the
backend operation completes ``zend_async_callbacks_notify`` iterates over all
registered callbacks and passes the result or exception:

.. code:: c

   static void on_poll_event(uv_poll_t *handle, int status, int events) {
       async_poll_event_t *poll = handle->data;
       zend_object *exception = NULL;

       if (status < 0) {
           exception = async_new_exception(
               async_ce_input_output_exception,
               "Input output error: %s",
               uv_strerror(status)
           );
       }

       poll->event.triggered_events = events;

       zend_async_callbacks_notify(&poll->event.base, NULL, exception);

       if (exception != NULL) {
           zend_object_release(exception);
       }
   }

*************************
Event Coroutines
*************************

Coroutines themselves are implemented as events using the ``zend_coroutine_t`` structure. When a coroutine yields, its waker waits on multiple events and resumes the coroutine once any of them triggers.

.. code:: c

   // Spawn a coroutine that waits for a timer
   zend_coroutine_t *co = ZEND_ASYNC_SPAWN(NULL);
   zend_async_resume_when(co, &timer->base, false, zend_async_waker_callback_resolve, NULL);
   zend_async_enqueue_coroutine(co);

When the coroutine finishes execution the event triggers again to deliver the
result or exception. The coroutine implementation marks the callback result as a
``zval`` value using ``ZEND_ASYNC_EVENT_SET_ZVAL_RESULT``. Callback handlers may
also set ``ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED`` to indicate that the thrown
exception has been processed and should not be rethrown by the runtime.

****************************
Extending Events
****************************

Custom event types embed ``zend_async_event_t`` at the beginning of their
structure and may allocate additional memory beyond the end of the struct.
The ``extra_size`` argument in ``ZEND_ASYNC_NEW_*_EX`` controls how much extra
space is reserved, and ``extra_offset`` records where that region begins.

.. code:: c

   // Allocate extra space for user data
   zend_async_poll_event_t *poll = ZEND_ASYNC_NEW_POLL_EVENT_EX(fd, false, sizeof(my_data_t));
   my_data_t *data = (my_data_t *)((char*)poll + poll->base.extra_offset);

The libuv backend defines event wrappers that embed libuv handles. A timer
event, for example, extends ``zend_async_timer_event_t`` as follows:

.. code:: c

   typedef struct {
       zend_async_timer_event_t event;
       uv_timer_t uv_handle;
   } async_timer_event_t;

   // Initialize callbacks for the event
   event->event.base.add_callback = libuv_add_callback;
   event->event.base.del_callback = libuv_remove_callback;
   event->event.base.start = libuv_timer_start;
   event->event.base.stop = libuv_timer_stop;
   event->event.base.dispose = libuv_timer_dispose;

Every extended event defines its own ``start``, ``stop`` and ``dispose``
functions.  The dispose handler must release all resources associated with
the event and is called when the reference count reaches ``1``.  It is
common to stop the event first and then close the underlying libuv handle so
that memory gets freed in the ``uv_close`` callback.

.. code:: c

   static void libuv_timer_dispose(zend_async_event_t *event)
   {
       if (ZEND_ASYNC_EVENT_REF(event) > 1) {
           ZEND_ASYNC_EVENT_DEL_REF(event);
           return;
       }

       if (event->loop_ref_count > 0) {
           event->loop_ref_count = 1;
           event->stop(event);
       }

       zend_async_callbacks_free(event);

       async_timer_event_t *timer = (async_timer_event_t *)event;
       uv_close((uv_handle_t *)&timer->uv_handle, libuv_close_handle_cb);
   }

If ``ZEND_ASYNC_EVENT_F_NO_FREE_MEMORY`` is set the dispose handler must not
free the event memory itself because the structure is embedded in another
object (e.g. ``async_coroutine_t``).  The libuv close callback will only free
the libuv handle in this case.

***********************
Custom Event Callbacks
***********************

Callbacks can also be extended to store additional state.  The await logic in
``php-async`` defines a callback that inherits from
``zend_coroutine_event_callback_t`` and keeps a reference to the awaiting
context:

.. code:: c

   typedef struct {
       zend_coroutine_event_callback_t callback;
       async_await_context_t *await_context;
       zval key;
       zend_async_event_callback_dispose_fn prev_dispose;
   } async_await_callback_t;

   async_await_callback_t *cb = ecalloc(1, sizeof(async_await_callback_t));
   cb->callback.base.callback = async_waiting_callback;
   cb->await_context = ctx;
   zend_async_resume_when(co, awaitable, false, NULL, &cb->callback);

***********************
Events as Zend Objects
***********************

If ``ZEND_ASYNC_EVENT_F_ZEND_OBJ`` is set, the event also acts as a Zend object implementing ``Awaitable``. The ``zend_object_offset`` field stores the location of the ``zend_object`` within the structure. Reference counting macros automatically use either the internal counter or ``GC_REFCOUNT`` depending on this flag.

This allows events to be exposed to userland seamlessly while keeping the internal lifecycle consistent.

The ``php-async`` extension provides ``Async\\Timeout`` objects that embed a
timer event. The object factory allocates the event, marks it as a Zend object
and sets up the handlers::

   static zend_object *async_timeout_create(const zend_ulong ms, const bool is_periodic)
   {
       zend_async_event_t *event = (zend_async_event_t *) ZEND_ASYNC_NEW_TIMER_EVENT_EX(
           ms, is_periodic, sizeof(async_timeout_ext_t) + zend_object_properties_size(async_ce_timeout)
       );

       async_timeout_ext_t *timeout = ASYNC_TIMEOUT_FROM_EVENT(event);
       event->before_notify = timeout_before_notify_handler;

       zend_object_std_init(&timeout->std, async_ce_timeout);
       object_properties_init(&timeout->std, async_ce_timeout);

       ZEND_ASYNC_EVENT_SET_ZEND_OBJ(event);
       ZEND_ASYNC_EVENT_SET_NO_FREE_MEMORY(event);
       ZEND_ASYNC_EVENT_SET_ZEND_OBJ_OFFSET(
           event,
           ((uint32_t)((event)->extra_offset + XtOffsetOf(async_timeout_ext_t, std)))
       );

       if (async_timeout_handlers.offset == 0) {
           async_timeout_handlers.offset = (int) event->zend_object_offset;
       }

       timeout->std.handlers = &async_timeout_handlers;
       return &timeout->std;
   }

