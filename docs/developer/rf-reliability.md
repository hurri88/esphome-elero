# RF delivery and STOP reliability

This implementation follows the [read-only review at its immutable revision](https://github.com/pfriedrich84/esphome-elero/blob/1ca6cf755cea05f619c1d10efbcc2d1db280d955/docs/developer/reviews/2026-09-08-rf-reliability-astra-gpt-6.md).

## Ownership and causal STOP confirmation

- Core 0 owns SPI, packet capture, receive sequence/epoch allocation and local TX completions.
- `ProfileDeliveryCoordinator` owns ordering, counters and bounded packet repetitions/retries.
- `EleroCover` owns motor-state interpretation and STOP verification. A UI IDLE estimate is not proof of physical standstill.

`RxMetadata` accompanies a status before it crosses the result queue: monotonic 64-bit capture sequence, radio receive epoch/time, source, forwarding addresses, destinations, channel, counter, type and hop. No 64-bit atomic operations run in the GPIO ISR. Before reporting successful local TX, Core 0 captures already buffered packets and fences the receive timeline. A buffered prefix must retain its original epoch when completed later. This intentionally conservative fence can exclude a very early response; the bounded CHECK path can observe a later state. Entity-dispatch time never creates freshness.

The first successful STOP transmission carries that cutoff through the transaction completion and coordinator outcome. Only a later receive sequence/epoch for the configured motor may affect verification. Packet counters are not assumed to echo the transmitted counter, and a spontaneous fresh state observation is not called a protocol ACK.

## Common STOP entry and outcomes

`EleroCover::request_stop()` is the common entry for ESPHome calls, position-triggered STOP, semantically mapped custom buttons, web intents and already-admitted group STOPs. It records the position/trigger, blocks movement, requests priority delivery and handles rejection. Group admission remains in the existing group/coordinator path; its member preparation invokes the same entry without duplicating an already admitted packet.

- `stop_queued`: admitted, not yet locally transmitted.
- `stop_verifying`: local TX succeeded; standstill is not established.
- `stop_confirmed`: fresh STOPPED, TOP, BOTTOM, INTERMEDIATE, TILT, TOP_TILT or BOTTOM_TILT observation for the correct motor.
- `stop_motor_blocking` / `stop_motor_overheated`: explicit terminal motor fault, not successful user STOP.
- `stop_failed`: bounded local delivery/verification failed.

UNKNOWN, TIMEOUT and unknown raw values do not confirm STOP. Fresh movement may request the existing one additional burst, only after the initial burst finishes. Missing feedback uses one CHECK per burst, at the existing two-second interval. Repeated user requests during the same verification do not replenish its budget. Physical overshoot is not inferred from RX/dispatch delay.

Only STOP bypasses a lane's three-second failure cooldown; the cooldown is not erased for OPEN/CLOSE/CHECK. A selected STOP burst is never truncated by another lane's STOP. Native group movement is ineligible while any referenced member verifies STOP, including movement admitted before the block was established.

## Validation boundary

The host entity tests compile the complete production cover/group method bodies and production declarations, substituting only framework include paths and ESPHome/queue boundaries. Causal timelines, lane/coordinator logic and state transitions are production code. These tests do not validate RF propagation, CC1101 electrical timing or motor mechanics. Register access and FIFO/CCA behavior require separate radio tests and hardware validation.
