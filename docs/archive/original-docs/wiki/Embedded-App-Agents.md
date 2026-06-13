# Embedded App Agents

OSAI is designed for applications that embed local CPU-only AI agents.

The agent is not just a chatbot beside the application. It is a controlled app-local worker that can understand the repository, receive human requests, generate code changes, rebuild, test, sync with Git, and help the running application evolve quickly and safely.

## Target Loop

```text
human request
  -> app-local CPU AI agent
  -> source-code understanding
  -> patch generation
  -> rebuild / test
  -> Git sync
  -> hot reload or redeploy
  -> running app becomes smarter
```

OSAI exists to make this loop fast, deterministic, and safe.

## Why This Needs OS Support

An embedded app agent touches more than model inference. It uses:

```text
CPU inference
source-code indexing
symbol search
file I/O
compiler toolchains
build caches
test runners
Git worktrees
network APIs
hot reload / deployment logic
rollback state
```

On a general-purpose OS, these activities compete with one another through generic scheduling, page cache pressure, background daemons, shared thread pools, generic TCP stacks, filesystem contention, and unpredictable memory placement.

OSAI makes these resources explicit.

## Agent Cell

Each app agent should run inside an **AI cell**.

An AI cell owns:

```text
fixed CPU cores
fixed memory arena
fixed model-weight mapping
fixed KV/cache arena
fixed NIC RX/TX queues
fixed source-code index
fixed build/test sandbox
fixed Git workspace
fixed telemetry counters
```

This lets the OS enforce a predictable performance contract instead of treating the agent as just another process.

## Source-Code Knowledge

An OSAI app agent should maintain a repository-local source index.

Index inputs:

- Git tree;
- language metadata;
- build files;
- dependency manifests;
- symbols;
- tests;
- API routes;
- configuration files;
- recent diffs;
- app-specific documentation.

The source index should be memory-isolated from the model runtime so indexing work cannot randomly steal model bandwidth or pollute hot inference caches.

## Patch Generation

The agent should never blindly mutate production code.

Safe patch flow:

```text
request
  -> context selection
  -> patch proposal
  -> diff review
  -> isolated build
  -> isolated tests
  -> optional human approval
  -> commit
  -> hot reload or deploy
  -> rollback point
```

OSAI should make the safe path the default path.

## Build and Test Isolation

Build and test work should not run on model-serving cores unless explicitly configured.

Recommended resource split:

```text
network cores: low-latency request/response
inference cores: CPU model runtime
source-index cores: repository analysis
build/test cores: compiler and test runner
housekeeping cores: SSH, logging, package management, Git sync
```

This keeps compile spikes, linker memory pressure, and test runners from damaging inference latency.

## Git Integration

OSAI should treat Git as an operating-system workflow, not just a user-space afterthought.

Useful Git features:

- per-agent worktrees;
- controlled patch staging;
- generated diff review;
- signed commits;
- optional human approval before push;
- branch policy integration;
- rollback to last known-good commit;
- status telemetry for dirty worktrees;
- Git sync isolated from inference cores.

## Service Manifest Example

```toml
[service]
name = "smart-app-agent"
binary = "/apps/smart-app-agent/bin/agent"

[model]
name = "local-coder-small"
mode = "cpu-only"
weights = "/models/local-coder-small/model.gguf"
shared_weights = true
context_tokens = 32768
quantization = "int4"

[cores]
network = "2"
inference = "3-7"
source_index = "8"
build_test = "9-15"
migration = "forbidden"
preemption = "forbidden_on_hot_cores"

[memory]
model_arena = "shared"
kv_arena = "private"
hugepages = true
prefault = true
swap = false
numa = "local-only"
page_faults_after_ready = "fatal"

[network]
stack = "osai-lowlatency"
ports = [8080]
rx_queues = "owned"
tx_queues = "owned"
latency_profile = "min"

[git]
repository = "/srv/apps/smart-app"
mode = "worktree"
allow_commit = true
allow_push = false
require_tests = true
rollback = true
```

## Safety Model

An app agent should not automatically receive unlimited authority over the application or host.

OSAI should use explicit permissions for:

- reading source code;
- writing source code;
- running builds;
- running tests;
- creating commits;
- pushing to remotes;
- restarting services;
- opening network ports;
- modifying deployment state.

The agent should be fast, but it must remain bounded by capabilities.

## Product Thesis

The market opportunity is not only faster inference. The bigger opportunity is making normal applications self-improving:

```text
small CPU AI model + source-code knowledge + safe patch/build/test/Git loop = smart application
```

OSAI is the operating-system layer for that loop.