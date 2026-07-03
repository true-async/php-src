<?php

/** @generate-class-entries */

namespace Async;

/**
 * Activation point for the concurrent mode.
 *
 * A scheduler is registered by handing register() a map of hook name
 * (the class constants below) to callable. There is exactly one scheduler
 * per process, so the class is used only through its static methods.
 */
final class SchedulerHook
{
    public const string LAUNCH = 'launch';
    public const string SHUTDOWN = 'shutdown';
    public const string INTERCEPT_FIBER = 'intercept_fiber';
    public const string ENQUEUE = 'enqueue_coroutine';
    public const string SUSPEND = 'suspend';
    public const string RESUME = 'resume';
    public const string CANCEL = 'cancel';
    public const string CONTEXT_FIND = 'context_find';
    public const string CONTEXT_SET = 'context_set';
    public const string CONTEXT_UNSET = 'context_unset';
    public const string GC_DESTRUCTORS = 'gc_destructors';
    public const string DEFER = 'defer';

    /**
     * Registers a scheduler and activates the concurrent mode.
     *
     * $hooks maps a hook constant to a callable; omitted hooks keep their
     * defaults. A scheduler is registered once per process: calling this
     * when a scheduler is already registered (by a C extension or by an
     * earlier PHP call) throws an Error.
     */
    public static function register(string $module, array $hooks): bool {}

    /** Returns the module name of the registered scheduler, or null when none. */
    public static function getModule(): ?string {}

    /**
     * Queues a callable on the scheduler's microtask queue (one-shot,
     * runs on the next tick). Forwards to the DEFER hook; the queue and
     * its draining belong to the scheduler.
     */
    public static function defer(callable $task): void {}

}
