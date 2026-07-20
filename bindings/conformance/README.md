# Conformance scenario corpus

Language-neutral JSON scenarios that every EmbeddedMQ binding should replay against `libemq` and compare final queue stats.

## Layout

```
conformance/
  scenarios/          # one JSON file per scenario
  validate_corpus.py  # schema sanity checker (no C runtime required)
```

## Scenario format

Each file is a single JSON object:

| Field | Required | Description |
| ----- | -------- | ----------- |
| `name` | yes | Stable identifier (matches filename stem). |
| `description` | no | Human-readable summary. |
| `queue` | no | Default queue config for queue-oriented scenarios. |
| `topic` / `subscription` | no | Pub/sub scenarios (`pubsub.json`). |
| `ops` | yes | Ordered list of operations to execute. |
| `expected_stats` | yes | Final `emq_stats` counters after all ops. |

### Queue block

```json
"queue": {
  "name": "fifo",
  "opts": {
    "storage": "fast",
    "policy": "fifo",
    "capacity": 16,
    "producers": 1,
    "consumers": 1
  }
}
```

String enums mirror C public types: `storage` (`fast`, `durable`, …), `policy` (`fifo`, `work`, …), `delivery` (`at_most_once`, `at_least_once`), `backpressure` (`drop_new`, `block`, …).

### Operations

Every op has an `"op"` field. Common fields:

| Field | Ops | Meaning |
| ----- | --- | ------- |
| `payload` | push, publish | UTF-8 string body (bindings may encode as bytes). |
| `size` | push, publish | Byte length; may differ from string length for binary fixtures. |
| `timeout_ms` | pop, claim, sub_next | Blocking timeout (`0` = non-blocking where supported). |
| `expect_status` | any | Expected `emq_status` name: `ok`, `empty`, `full`, `timeout`, … |
| `expect_payload` | pop, sub_next | Expected message body after pop. |
| `expect_size` | peek, pop | Expected `message.size`. |
| `capture_id` | pop, claim | Store `message.id` as `$capture_id` for later ops. |
| `message_id` | ack, nack, release_claim | Literal id or `$capture_id` reference. |
| `delay_ms` | nack | Redelivery delay. |

Supported `op` values:

- Lifecycle: `runtime_create`, `runtime_destroy`, `queue_create`, `queue_close`
- Point-to-point: `push`, `pop`, `try_pop`, `peek`, `ack`, `nack`, `claim`, `release_claim`, `flush`
- Pub/sub: `subscribe`, `unsubscribe`, `publish`, `sub_next`

Bindings map each op to the corresponding C API call in `core/include/emq/emq.h`.

### Expected stats

Matches `emq_stats` from `emq_types.h`:

```json
"expected_stats": {
  "enqueued": 2,
  "dequeued": 2,
  "acked": 0,
  "nacked": 0,
  "expired": 0,
  "depth": 0,
  "bytes": 0,
  "redelivered": 0
}
```

Runners should call `emq_queue_stats()` (or equivalent) after the last op and assert equality.

## Validation

```bash
python validate_corpus.py scenarios/*.json
```

Checks JSON structure, required fields, known op names, and stat keys. Does **not** execute scenarios.

## Adding scenarios

1. Add `scenarios/<name>.json` with unique `name`.
2. Run `validate_corpus.py`.
3. Implement the op in each binding's conformance runner (future work).
