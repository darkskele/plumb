# tools decisions

1. Python builds and runs the bench binaries and analyses what they wrote. It never records or rebuilds a measurement itself.
2. A results file is read by the schema it carries, so no struct is defined twice.
3. A figure for a case is the median across its repetitions, shown with their range. Repetitions are never merged.
4. Spread is the range of the repetition p50s over their median. A case over 5% is flagged.
5. Stamp share is the stamp cost over a batch at p50. Subtracting the stamp cost is an option, off by default.
6. Overview plots group cases by the decade of their p50, at most 8 a plot. A line per case through p50, p90 and p99, each point the median with a bar from the lowest repetition to the highest.
7. Drift is the furthest a tenth of a repetition's p50 sits from the whole repetition's. Over 5% is flagged and plotted.
8. Plots are PNG on a light background. A colour per case in fixed order. Repetitions share one colour, since their order means nothing.
9. Hardware counts are shown per band of a repetition's batches ranked fastest first, cut at its figures. A bucket across a cut gives each side its counts in proportion to its batches on that side, so every band holds exactly its share.
10. Stall is the cycles per instruction of the slowest 1% of batches over that of the fastest half, the median across repetitions. Over 1.5× is flagged.
11. `--subtract-stamp` also takes the empty batch's counts off each batch, the same harness cost.
12. A/B changes need three things together. Every repetition of one run past every repetition of the other, a change of at least 1 ns and 2% of the base, and over twice the wider range of repetitions. Each limit alone flags small cases.
13. Runs whose environments differ are compared anyway, the documented fields that differ listed.
14. Instructions per op and cycles per instruction are compared like the times, with no nanosecond limit.
15. One command builds, runs and reports, so neither a user nor a task has to script the steps.
16. The bench binaries come from a list the build directory holds, so the tools know no target by name.
17. A/B is a section of the report, not a command of its own.
18. A run is a folder of each bench binary's results, made in the build directory and never committed.
19. A profile is one case run once in process, so it takes as long as one repetition and no longer.
20. Call stacks come from LBR. DWARF stacks copy too much to stay fast, and frame pointers would change the timed code.
21. The op is found from the line table, not inline frames, which the compiler drops for the op's own lines.
22. Event counts are kept only for the op's samples. Counting the whole process would count the fixture and the harness.
23. A profile prints to the terminal only. It is for iterating and is never committed.
