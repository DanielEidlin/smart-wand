# Superseded: gesture+silence takes on the loose mount (2026-08-23)

Not training data. Kept as the control half of a mount experiment.

These 20 takes were recorded with the board held by a SINGLE rubber band, which
was found to be letting the board pivot. Silence-while-gesturing is the only
condition that exposes mount noise -- every other batch has speech over the top
of it, 30+ dB louder -- and these takes carried in-band transients up to 1323
counts, within 5 dB of a spoken word.

Re-recorded after bedding the board more tightly. The comparison:

|                        | loose | tightened |
| ---------------------- | ----- | --------- |
| in-band max, median    | 476.7 |     129.8 |
| in-band max, worst     |1322.9 |     658.2 |
| raw peak, median       |  6855 |      3702 |
| dB below a spoken word |     5 |        11 |

Read that carefully: the operator ALSO swung more gently on the retake (raw peak
-5.4 dB), so the mount itself is worth about 6 dB of the 11.3 dB median
improvement, not all of it.

The residual transients are 45-125 ms sustained rather than sub-10 ms impulses,
which points at airflow over the mic port rather than mechanical contact -- but
they remain somewhat intermittent at a given swing force, so some mechanical
component survives. Both causes are partly rig artifacts: the finished wand
seals the board inside the 20 mm bore with epoxy and hot glue, so neither the
band nor this airflow path exists in the product. Worth re-recording this one
batch from the real wand once it is assembled.
