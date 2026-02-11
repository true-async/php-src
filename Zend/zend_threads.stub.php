<?php

/** @generate-class-entries */

final class Thread
{
    public function __construct(?string $bootstrap = null) {}

    public function run(Closure $task, array $args = []): void {}

    public function join(): void {}

    public function kill(): void {}

    public static function isSupported(): bool {}
}
