<?php

/** @generate-class-entries */

namespace PHP {
    final class Thread
    {
        public function __construct(?string $bootstrap = null) {}

        public function run(Closure $task, array $args = []): void {}

        public function join(): void {}

        public function kill(): void {}
    }
}
