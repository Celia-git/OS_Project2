# Pintos Project 1: Threads - Complete Testing Guide

## 📋 Test Categories & Commands

### 1. **Alarm Clock Tests** (tests/threads/alarms/)
Tests thread sleep/wakeup functionality with priority scheduling.

| Test Name | Command | Purpose |
|-----------|---------|---------|
| `alarm-single` | `pintos -- run alarm-single` | Single thread sleeps and wakes |
| `alarm-multiple` | `pintos -- run alarm-multiple` | Multiple threads sleep/wake |
| `alarm-priority` | `pintos -- run alarm-priority` | Priority preserved during sleep |
| `alarm-simultaneous` | `pintos -- run alarm-simultaneous` | Multiple threads wake at same tick |

### 2. **Priority Scheduling Tests** (tests/threads/priority/)
Tests priority scheduler + donation logic.

| Test Name | Command | Purpose |
|-----------|---------|---------|
| `priority-fifo` | `pintos -- run priority-fifo` | Higher priority preempts lower |
| `priority-donation` | `pintos -- run priority-donation` | **Nested donation chain (YOUR FAILING TEST)** |
| `priority-donate-lower` | `pintos -- run priority-donate-lower` | Donation from lower-priority donor |
| `priority-donate-one` | `pintos -- run priority-donate-one` | Single-level donation |
| `priority-donate-two` | `pintos -- run priority-donate-two` | Two-level donation |
| `priority-donate-chain` | `pintos -- run priority-donate-chain` | Multi-level donation chain |
| `priority-donate-sema` | `pintos -- run priority-donate-sema` | Donation via semaphores |
| `priority-mlfq` | `pintos -- run priority-mlfq` | MLFQ scheduling (bonus) |

## 🚀 Running Tests

### From `src/threads/` directory:
```bash
cd pintos/src/threads
make clean
make                    # Build kernel
pintos -- run <test>    # Run single test
```

### Run ALL tests at once:
```bash
cd pintos/src/threads/build
pintos --filesys-size=2 tests/threads/tests
```

### Run tests/threads/ directory tests:
```bash
cd pintos/tests/threads
make check-threads      # Run all threads tests
```

## 🔧 Build & Debug Workflow

```bash
# 1. Build
cd pintos/src/threads
make clean
make

# 2. Test specific failing case
pintos -- run priority-donation

# 3. Debug with GDB
pintos --gdb -- run priority-donation
# Then in another terminal: gdb kernel
# (gdb) target remote localhost:1234
# (gdb) continue
```

## ❌ Common Failure Symptoms & Fixes

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| **Page fault in `priority-donation`** | Priority donation chain corruption | Update `donate_priority_chain()` + `thread_update_priority()` |
| `alarm-priority` fails | Sleep list not priority-ordered | Check `wakeup_tick_less()` comparator |
| Tests pass but output interleaved | Priority scheduler bug | Ensure `thread_yield()` in `thread_unblock()` |
| `make -C tests check` fails | Missing `solutions/p1.patch` | Use `pintos -- run <test>` directly |

## 📊 Expected PASS Output
```
(priority-donation) begin
(priority-donation) Thread A acquires lock a.
(priority-donation) Thread C acquires lock c needing lock b.
(priority-donation) Thread B acquires lock b needing lock a.
(priority-donation) Thread C acquires lock b.
(priority-donation) Thread B acquires lock a.
(priority-donation) Thread A releases lock a.
(priority-donation) Thread B releases lock a.
(priority-donation) Thread C releases lock b.
(priority-donation) Thread A releases lock a.
(priority-donation) end
```

## ✅ Current Status Tracking
```
✅ alarm-single
✅ alarm-multiple  
✅ alarm-priority
✅ alarm-simultaneous
✅ priority-fifo
❌ priority-donation    ← **FOCUS HERE**
⏳ priority-donate-lower
⏳ priority-donate-one
⏳ priority-donate-two
⏳ priority-donate-chain
⏳ priority-donate-sema
⏳ priority-mlfq (bonus)
```

**Next step**: Fix `priority-donation` page fault using the `donate_priority_chain()` + `thread_update_priority()` corrections, then test all remaining donation tests.

[7](https://ycruan.github.io/files/162_project1_design/)
[8](https://courses.cs.vt.edu/cs4284/spring2024/pintosdocs/pintos_2.html)
[9](https://github.com/sanfordcheung/Pintos)
[10](https://www.cs.jhu.edu/~huang/cs318/fall21/project/debugtest.html)
[11](https://faculty.iiitd.ac.in/~piyus/pintos/doc/pintos_2.html)
