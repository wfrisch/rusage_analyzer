`rusage_analyzer` attempts to find side-channel leaks in Linux programs with
setuid or fs capabilities, by statistically comparing the resource usage
(rusage) of two configurable commands.

Beware that false positives and negatives are not uncommon. Naturally other
processes will influence the run-time behavior of the programs under test.

[CVE-2024-0149: nvidia-modprobe: file existence test](https://security.opensuse.org/2025/03/26/nvidia-modprobe.html) serves an example for this problem.

Usage
=====
```
$ ./rusage_analyzer --help
Usage: ./rusage_analyzer [GLOBAL_OPTIONS] --scenario-a [OPTS] -- <COMMAND_A> --scenario-b [OPTS] -- <COMMAND_B>

Global Options:
  -n <N>                    Number of samples to collect (default: 200)
  -a <A>                    Significance level for the U-test (default: 0.025)

Scenario Options (must be placed after --scenario-a or --scenario-b):
  -l <STRING>               A descriptive label for the scenario in the report
  -e <KEY=VALUE>            Set an environment variable (can be used multiple times)

The '--' separator is mandatory before each command.

Example:
  ./rusage_analyzer -n 200 --scenario-a -l 'C Locale' -e LC_ALL=C -- /usr/bin/grep 'word' file.txt \
          --scenario-b -l 'UTF8 Locale' -e LC_ALL=en_US.UTF-8 -- /usr/bin/grep 'word' file.txt
```

Example: no difference (identical process)
------------------------------------------

```
$ ./rusage_analyzer --scenario-a -- ./demo/nothing --scenario-b -- ./demo/nothing
[*] Collecting 200 samples for each scenario, (interleave = 50)
[*] Collection complete.

---------------------------------------------------------------------------
Side-Channel Analysis Report
---------------------------------------------------------------------------
Metric          | p-value      | Median A     | Median B     | Significant
                |              | (Scenario A) | (Scenario B) | (alpha=0.025)
---------------------------------------------------------------------------
ru_nvcsw        | 2.02785e-02  | 0.00         | 0.00         | no
ru_utime        | 1.99003e-01  | 0.00         | 0.00         | no
ru_stime        | 4.33192e-01  | 0.00         | 0.00         | no
ru_nivcsw       | 4.58820e-01  | 0.00         | 0.00         | no
elapsed_us      | 6.77694e-01  | 766.50       | 758.50       | no
ru_minflt       | 8.98097e-01  | 98.00        | 98.00        | no
ru_maxrss       | 9.57918e-01  | 1420.00      | 1420.00      | no
ru_inblock      | 1.00000e+00  | 0.00         | 0.00         | no
ru_majflt       | 1.00000e+00  | 0.00         | 0.00         | no
ru_oublock      | 1.00000e+00  | 0.00         | 0.00         | no
---------------------------------------------------------------------------

[-] Conclusion: No statistically significant predictor found with the current data.
    Try increasing SAMPLE_COUNT or check if the scenarios have different resource footprints.
```

Example: silent process with malloc() of variable size
------------------------------------------------------

This detects [CVE-2024-0149](https://security.opensuse.org/2025/03/26/nvidia-modprobe.html).

```
$ ./rusage_analyzer --scenario-a -- ./demo/ru_minflt 1 --scenario-b -- ./demo/ru_minflt 4096
[*] Collecting 200 samples for each scenario, (interleave = 50)
[*] Collection complete.

---------------------------------------------------------------------------
Side-Channel Analysis Report
---------------------------------------------------------------------------
Metric          | p-value      | Median A     | Median B     | Significant
                |              | (Scenario A) | (Scenario B) | (alpha=0.025)
---------------------------------------------------------------------------
ru_maxrss       | 0.00000e+00  | 1592.00      | 1724.00      | YES
ru_minflt       | 0.00000e+00  | 100.00       | 102.00       | YES
elapsed_us      | 7.40756e-02  | 748.50       | 763.50       | no
ru_nvcsw        | 1.93878e-01  | 0.00         | 1.00         | no
ru_stime        | 2.95454e-01  | 0.00         | 0.00         | no
ru_nivcsw       | 4.12513e-01  | 0.00         | 0.00         | no
ru_utime        | 6.68532e-01  | 0.00         | 0.00         | no
ru_inblock      | 1.00000e+00  | 0.00         | 0.00         | no
ru_majflt       | 1.00000e+00  | 0.00         | 0.00         | no
ru_oublock      | 1.00000e+00  | 0.00         | 0.00         | no
---------------------------------------------------------------------------

[+] Conclusion: The best predictor is 'ru_maxrss'.
    You can likely distinguish between the two scenarios by observing this value.
```


Example: disk write
-------------------

```
$ ./rusage_analyzer --scenario-a -- ./demo/ru_oublock 0 --scenario-b -- ./demo/ru_oublock 1
[*] Collecting 200 samples for each scenario, (interleave = 50)
[*] Collection complete.

---------------------------------------------------------------------------
Side-Channel Analysis Report
---------------------------------------------------------------------------
Metric          | p-value      | Median A     | Median B     | Significant
                |              | (Scenario A) | (Scenario B) | (alpha=0.025)
---------------------------------------------------------------------------
ru_oublock      | 0.00000e+00  | 0.00         | 8.00         | YES
elapsed_us      | 1.61972e-03  | 846.00       | 887.50       | YES
ru_stime        | 1.28065e-02  | 0.00         | 0.00         | YES
ru_utime        | 1.43706e-01  | 0.00         | 0.00         | no
ru_minflt       | 1.46458e-01  | 104.00       | 103.00       | no
ru_maxrss       | 5.07679e-01  | 1596.00      | 1596.00      | no
ru_nivcsw       | 6.56230e-01  | 0.00         | 0.00         | no
ru_nvcsw        | 9.99590e-01  | 0.00         | 0.00         | no
ru_inblock      | 1.00000e+00  | 0.00         | 0.00         | no
ru_majflt       | 1.00000e+00  | 0.00         | 0.00         | no
---------------------------------------------------------------------------

[+] Conclusion: The best predictor is 'ru_oublock'.
    You can likely distinguish between the two scenarios by observing this value.
```

References
==========

- [getrusage()](https://manpages.opensuse.org/Tumbleweed/man-pages/getrusage.2.en.html)
- [Mann-Whitney U test](https://en.wikipedia.org/wiki/Mann%E2%80%93Whitney_U_test)
- [CVE-2024-0149 (information leak)](https://security.opensuse.org/2025/03/26/nvidia-modprobe.html)
