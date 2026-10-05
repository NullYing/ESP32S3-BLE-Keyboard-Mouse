Run the real mouse accumulator C implementation with host ESP timer / BLE stubs:

```sh
sh tests/mouse_accumulator/run.sh
```

Checks button transitions and failed-notify retry order, X/Y/wheel saturation
residual drainage, disconnected motion suppression and current button
synchronization, temporary readiness failures, idle queue drainage, overflow
release recovery, stale snapshots rejected across reconnection, and
producer / clear / reentrant-send interleavings during
notification. The notification hooks deterministically execute modifications
between snapshot and commit. AddressSanitizer and UndefinedBehaviorSanitizer
are enabled; no ESP-IDF SDK or connected hardware is required.
