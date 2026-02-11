<?php

/** @generate-function-entries */

namespace Php\MemoryManager;

function getHeapId(): int {}

function createThread(callable $callback): int {}

function joinThread(int $thread_id): bool {}

function hasIncomingQueue(int $sender_heap_id): bool {}

function collectRemoteFrees(): void {}

function getMpscStats(): array {}
