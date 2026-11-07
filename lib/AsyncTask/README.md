# AsyncTask

`BackgroundTask` runs cooperative FreeRTOS tasks. `ScopedMutex` provides scoped mutex access.

The worker stops itself. If a running worker fails to stop, retain its files and memory. A timeout does not transfer resource ownership.

`getState()` reports `COMPLETE` after the worker publishes its exit generation, including after a stop timeout. Call `stop()` before reusing the task. `stop(0)` waits without a time limit.

See [background work](../../docs/architecture.md#background-work) for the shared ownership rules.
