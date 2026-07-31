--TEST--
Error handling mode does not leak into fiber
--FILE--
<?php

/* The wrapper runs inside DirectoryIterator's window, where a warning becomes an
 * UnexpectedValueException. A fiber started there is a separate flow of control
 * and gets the normal mode. */

class WindowedDirectory
{
    public $context;

    public function dir_opendir(string $path, int $options): bool
    {
        $fiber = new Fiber(function (): void {
            trigger_error("Warning A", E_USER_WARNING);
            Fiber::suspend();
            trigger_error("Warning B", E_USER_WARNING);
        });

        $fiber->start();
        $fiber->resume();

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

stream_wrapper_register("windowed", WindowedDirectory::class);

try {
    new DirectoryIterator("windowed://dir");
    echo "no exception\n";
} catch (Throwable $e) {
    echo $e::class, ": ", $e->getMessage(), "\n";
}

trigger_error("Warning C", E_USER_WARNING);

?>
--EXPECTF--
Warning: Warning A in %serror-handling-does-not-leak-into-fiber.php on line %d

Warning: Warning B in %serror-handling-does-not-leak-into-fiber.php on line %d
no exception

Warning: Warning C in %serror-handling-does-not-leak-into-fiber.php on line %d
