<?php

/** @generate-class-entries */

namespace Async;

/**
 * A scheduler plugs into the concurrent mode by returning an instance of this
 * interface from the factory handed to Async\SchedulerHook::register(). The on*
 * methods are event callbacks the engine invokes; the engine performs the
 * context switches.
 *
 * The engine learns the current coroutine in exactly one way: the return value
 * of onLaunch()/onSuspend(). It never chooses one on its own.
 */
interface Scheduler
{
    /**
     * The scheduler starts: return the coroutine the top-level script runs in.
     * The main flow is a coroutine from its first opcode, not a plain flow that
     * becomes one at its first yield, so the scheduler defines it up front (it is
     * the scheduler's own object; the engine only records it as the main and the
     * current coroutine). Returning anything but a coroutine object is an Error.
     */
    public function onLaunch(): object;

    /** A graceful shutdown has been requested. */
    public function onShutdown(): void;

    /** A foreign Fiber is starting: return a coroutine to adopt it, or null. */
    public function onFiber(\Fiber $fiber): ?object;

    /**
     * Make a coroutine runnable (enqueue and resume are the same operation). A
     * non-null $error is raised at the coroutine's suspension point, which is how
     * cancellation and IO/timeout failures reach waiting code. Returns true when
     * the coroutine is queued and will run; false when it was not accepted
     * (shutdown): a quiet rejection, not an error.
     */
    public function onEnqueue(object $coroutine, ?\Throwable $error = null): bool;

    /**
     * The current flow yields; pick who runs next and switch to them. Return the
     * coroutine that is now current (or null); the engine records it.
     *
     * $fromMain marks the end-of-main handover: the main coroutine has REALLY
     * finished, and whatever runs after the drain (shutdown functions,
     * destructors) is a different flow. The scheduler must return a fresh main
     * coroutine — the engine records it as the new main and the current one;
     * returning the finished main (or a non-object) is an Error. Each handover
     * replaces the main again. $isBailout marks an abnormal end of the main
     * flow: the last chance to release resources.
     */
    public function onSuspend(bool $fromMain, bool $isBailout): ?object;

    /** Store a one-shot microtask on the scheduler's queue. */
    public function onDefer(callable $task): void;
}

/**
 * Activation point for the concurrent mode.
 *
 * A scheduler is registered by handing register() a factory that returns an
 * Async\Scheduler. There is exactly one scheduler per process, so the class is
 * used only through its static methods.
 */
final class SchedulerHook
{
    /**
     * Registers a scheduler and activates the concurrent mode.
     *
     * The factory receives the mandate and returns the scheduler, constructed
     * already holding its capabilities:
     *
     *     fn (callable $bindEntry,        // bindEntry(object $coroutine, callable $entry): void
     *         callable $switchTo,         // switchTo(object $coroutine, mixed $value = null, ?Throwable $error = null): mixed
     *         callable $currentCoroutine, // currentCoroutine(): ?object
     *     ): Async\Scheduler
     *
     * Everything is keyed by the scheduler's own coroutine objects; the
     * execution context behind a coroutine is engine-internal. $bindEntry
     * gives one of the scheduler's coroutines its body; $switchTo transfers
     * control into a coroutine symmetrically — the main coroutine, an adopted
     * fiber (the engine runs its body itself) and the scheduler's own
     * coroutines all switch the same way. $value is delivered as the return
     * value of the switchTo() call the target is suspended in; a non-null
     * $error is thrown from it instead (passing both is a ValueError); a first
     * entry ignores $value, and a first entry with $error finishes the
     * coroutine without starting the body. $currentCoroutine reads the
     * coroutine the engine records as current.
     *
     * The capabilities are real closures over engine internals that exist in
     * no function table: only the factory ever receives them. For a PHP
     * scheduler the factory call is the launch moment: the engine's own
     * launch point has already passed by the time userland code runs.
     *
     * A scheduler is registered once per process: calling this when a
     * scheduler is already registered (by a C extension or by an earlier PHP
     * call) throws an Error. Any other failure is an Error too.
     */
    public static function register(string $module, callable $factory): void {}

    /** Returns the module name of the registered scheduler, or null when none. */
    public static function getModule(): ?string {}

    /**
     * Queues a callable on the scheduler's microtask queue (one-shot,
     * runs on the next tick). Forwards to the scheduler's onDefer() hook.
     */
    public static function defer(callable $task): void {}
}
