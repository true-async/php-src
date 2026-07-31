--TEST--
Error handling mode does not leak out of fiber
--FILE--
<?php

/* DirectoryIterator::__construct turns warnings into UnexpectedValueException for
 * the duration of the directory open. The open reaches a userland wrapper, so the
 * fiber can suspend while that mode is in force. */

class SuspendingDirectory
{
    public $context;

    public function dir_opendir(string $path, int $options): bool
    {
        Fiber::suspend();
        trigger_error("Warning A", E_USER_WARNING); // Still inside the window.
        return true;
    }

    public function dir_readdir(): string|false
    {
        return false;
    }

    public function dir_closedir(): bool
    {
        return true;
    }
}

stream_wrapper_register("suspending", SuspendingDirectory::class);

$fiber = new Fiber(function (): void {
    try {
        new DirectoryIterator("suspending://dir");
    } catch (Throwable $e) {
        echo $e::class, ": ", $e->getMessage(), "\n";
    }
});

$fiber->start();

trigger_error("Warning B", E_USER_WARNING); // Should stay a warning.

$fiber->resume();

?>
--EXPECTF--
Warning: Warning B in %serror-handling-does-not-leak-out-of-fiber.php on line %d
UnexpectedValueException: Warning A
