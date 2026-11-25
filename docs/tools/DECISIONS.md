# tools decisions

1. Python analyses. It never records or rebuilds a measurement, it reads what the C++ wrote.
2. A results file is read by the schema it carries, so no struct is defined twice.
3. A figure for a case is the median across its repetitions, shown with their range. Repetitions are never merged.
4. Spread is the range of the repetition p50s over their median. A case over 5% is flagged.
5. Stamp share is the stamp cost over a batch at p50. Subtracting the stamp cost is an option, off by default.
6. Overview plots group cases by the decade of their p50, at most 8 a plot. A line per case through p50, p90 and p99, each point the median with a bar from the lowest repetition to the highest.
7. Drift is the furthest a tenth of a repetition's p50 sits from the whole repetition's. Over 5% is flagged and plotted.
8. Plots are PNG on a light background. A colour per case in fixed order. Repetitions share one colour, since their order means nothing.
