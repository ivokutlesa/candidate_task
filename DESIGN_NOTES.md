# Candidate design notes

Replace this text with 5–15 sentences (bullet points are fine).

## What I changed

## What I did not finish

## Design trade-off

## Ring buffer concurrency (B1.6)

The ring buffer uses separate atomic read and write counters for its single consumer and single producer. The producer marks bytes as ready only after writing them, and the consumer marks slots as free only after reading them. This prevents reading unfinished data or overwriting unread data. 
On a real Cortex-M, I would verify that the chosen atomic types are lock-free. If an atomic type is not lock-free, its implementation might internally use a lock or library routine. That is dangerous inside an interrupt because it can leat to a deadlock. 
i.e. 
1. Main program acquires hidden lock
2. Interrupt occurs
3. Interrupt tries to acquire the same lock
4. Deadlock: main cannot resume until the interrupt finishes
`volatile` alone is not sufficient because it only forces memory access, it does not make shared data safely synchronized.

## Testing on a real MCU

## Limits of this simulation for a real safety system
