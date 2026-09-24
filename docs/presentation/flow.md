# M2 presentation flow — target 8:00

Keep the webcam visible in the blank right rail from the first slide to the last. Do not cover slide text. The companion clip is illustrative; every performance claim comes from the named CSVs.

## 1. Title — 0:00–0:20

“This presentation makes one narrow M2 claim. I measure how far the OpenMP version of the independent M2 continuous simulation engine scales on one cluster node, then show the serial work that stops it scaling further.”

## 2. Demo — 0:20–0:45

“First, here is the engine running. Blue prey flee red hunters. This ten-second clip shows 256 agents so their motion is legible. It is a demonstration, not benchmark evidence. The benchmark uses 100,000 agents.”

## 3. Scope — 0:45–1:15

“The product is a deterministic, fixed-step simulation engine. It simulates in two dimensions; rendering supplies the 2.5D presentation and is excluded from timing. Although the engine has several scenes and kernels, this presentation tests one continuous predator–prey workload: 100,000 agents, ten steps, radius 80, and three seeds. That scope is deliberate. I cannot honestly generalise one kernel’s result to every scene.”

## 4. Simulation loop and complexity — 1:15–1:55

"At each fixed step, the engine rebuilds the uniform grid if cached neighbours are no longer valid, otherwise reuses CSR neighbours. It applies steering rules, integrates the next state, then publishes it. After the final step, it checksums the state. Rendering is outside the timed loop. Naive discovery is theta n squared. Grid-based rebuild work is theta n plus m, where m is local candidate work; density and radius still control m."

## 5. OpenMP change — 1:55–2:30

“The one optimisation under examination is intentionally limited. Grid and CSR rebuilding remain serial. The L7 reuse loop is OpenMP static work-sharing: every thread owns a disjoint entity range, reads immutable state and CSR, and writes only its own next state. Metrics are thread-local and the final commit remains serial. These ownership rules keep the checksum meaningful.”

## 6. Measurement gate — 2:30–3:05

“Each tuple has one warm-up and five timed samples for each of seeds 31, 32 and 33. I use the median nanoseconds per entity-update, preserve seed-level rows, and reject any checksum mismatch. The run used one allocated Rangpur vcpu-5 node: eight virtual AMD EPYC 7542 CPUs, one NUMA node, CPUs zero to seven, GCC 13.3.1. The precise build settings and provenance sit next to the raw result.”

## 7. Baseline versus final — 3:05–3:40

"The first comparison exposes a gap. M1 L0 takes 358.239 nanoseconds per entity-update. The independent M2 L7 serial executable takes 94.945; M2 OpenMP at one thread takes 95.185 and at eight threads takes 52.131. The 6.863-times end-to-end ratio crosses codebases. It cannot measure M2 optimisation alone. I have prepared a same-codebase M2 L0 run and will not claim its ratio until that checksum-gated measurement exists."

## 8. Strong scaling result — 3:40–4:25

“For the actual OpenMP comparison, p1 to p8 improves by 1.826 times: from 95.185 to 52.131 nanoseconds per entity-update. The intermediate speed-ups are 1.410 at two threads and 1.773 at four. Eight-thread efficiency is 22.8 percent. The useful initial gain, followed by an early plateau, is the result—not something to hide with only the best number.”

## 9. Fixed-density scaling — 4:25–5:00

“I then repeat the same checksum-gated measurement at 100, 200, 400 and 800 thousand agents, increasing world area with agent count so density remains fixed. Eight-thread speed-up stays close to 1.8 times: 1.831, 1.885, 1.855 and 1.836. However, per-entity time rises as N grows at both one and eight threads. The weak-scaling endpoint—100K on one thread to 800K on eight—is 5.520-times normalized elapsed time, or 18.1 percent efficiency. That increase is measured; I do not assign a hardware cause because perf counters were unavailable.”

## 10. Why it plateaus — 5:00–5:50

“A separate instrumented phase build explains the plateau. At eight threads, CSR rebuilding is about 33.5 milliseconds, or 75.8 percent of diagnostic time, while the reuse/publish phase is about 10.6 milliseconds. Within the serial rebuild, grid/count traversal is 14.0 milliseconds, the second fill traversal is 10.8, and residual prefix/allocation work is 8.7. The diagnostic is not comparable to release timing; it explains phase share, not the headline performance number.”

## 11. Next decision — 5:50–6:30

“That evidence gives one next candidate: retain accepted pairs in a bounded scratch list to avoid the second fill traversal, while preserving order, checksums and a safe fallback. No validated candidate A/B result is available here, so I do not claim a gain. CUDA, MPI and manual AVX have not been measured for this workload; they would need their own correctness and timing evidence.”

## 12. Limits — 6:30–7:10

“This establishes repeated, checksum-verified strong scaling for this one workload on one node. It does not establish all scenes, density regimes, multi-node scaling, GPU acceleration or a candidate speed-up. Hardware-counter attribution is also unavailable because perf stat could not count events on this allocation. Those are boundaries on the claim, not omissions hidden in the conclusion.”

## 13. Conclusion and disclosure — 7:10–7:55

"Static OpenMP ownership preserved each seed's checksum and yielded 1.826-times at eight threads. The surprising result is that at eight threads, serial CSR rebuilding occupies 75.8 percent of the instrumented step. Adding more workers to the reuse loop alone offers little. The next test is the bounded scratch candidate against the original M2 binary; I will reject it if memory or one-thread cost outweighs its benefit. Generative AI assisted implementation, data checks and slide drafting. I will independently review the final evidence and defend every claim in the interview."

## Delivery checks

- Record the slides and your face together; keep the webcam in the right rail and never over text
- Keep the recording under 10 minutes; this script leaves roughly 2:40 of margin for pauses and a short clip
- Use the provided H.264 demo clip only as visual context; retain the raw CSV paths in the submitted source
- Add CSR candidate numbers only after the A/B result is available and its checksum gate passes
- Be ready to open the raw profile CSV, phase CSV and source at the in-person interview
