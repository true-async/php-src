<?php

/* A provider whose every operation throws, as one would for a cancelled coroutine */
Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        throw new RuntimeException("cancelled " . $op::class);
    }
});
