# tools decisions

1. Python analyses. It never records or rebuilds a measurement, it reads what the C++ wrote.
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
13. Runs whose environments differ are compared anyway, the differences listed.
14. Instructions per op and cycles per instruction are compared like the times, with no nanosecond limit.
