`rusage_analyzer` attempts to find side-channel leaks in programs, with setuid
or fs capabilities, by statistically comparing the resource usage (rusage) of
two configurable commands on Linux.

Beware that false positives and negatives are not uncommon. Naturally other
processes and system load can influence the run-time behavior of the programs
under test, on an operating system with preemptive multi-tasking.

Usage
=====
```
Usage: ./rusage_analyzer [GLOBAL_OPTIONS] --scenario-a [OPTS] -- <COMMAND_A> --scenario-b [OPTS] -- <COMMAND_B>

Global Options:
  -n <N>                    Number of samples to collect (default: 100)
  -a <A>                    Significance level for the U-test (default: 0.05)

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
$ ./rusage_analyzer -n 500 --scenario-a -- /usr/bin/hello --scenario-b -- /usr/bin/hello 
[*] Collecting 100 samples for scenario: 'Scenario A'...
....................................................................................................
[*] Collection complete.

[*] Collecting 100 samples for scenario: 'Scenario B'...
....................................................................................................
[*] Collection complete.

---------------------------------------------------------------------------
Side-Channel Analysis Report
---------------------------------------------------------------------------
Metric          | p-value      | Median A     | Median B     | Significant 
                               | (Scenario A) | (Scenario B) | (alpha=0.025)
---------------------------------------------------------------------------
ru_nvcsw        | 3.32582e-03  | 1.00         | 1.00         | no          
elapsed_us      | 4.46960e-02  | 1104.50      | 1078.50      | no          
ru_nivcsw       | 3.02772e-01  | 0.00         | 0.00         | no          
ru_minflt       | 4.81542e-01  | 124.00       | 124.00       | no          
ru_utime        | 5.68257e-01  | 0.00         | 0.00         | no          
ru_stime        | 6.02679e-01  | 0.00         | 0.00         | no          
ru_maxrss       | 6.97970e-01  | 4208.00      | 4208.00      | no          
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
$ ./rusage_analyzer -n 500 --scenario-a -- ./demo/malloc 1 --scenario-b -- ./demo/malloc 4096

[*] Collecting 100 samples for scenario: 'Scenario A'...
....................................................................................................
[*] Collection complete.

[*] Collecting 100 samples for scenario: 'Scenario B'...
....................................................................................................
[*] Collection complete.

---------------------------------------------------------------------------
Side-Channel Analysis Report
---------------------------------------------------------------------------
Metric          | p-value      | Median A     | Median B     | Significant 
                |              | (Scenario A) | (Scenario B) | (alpha=0.025)
---------------------------------------------------------------------------
ru_minflt       | 0.00000e+00  | 98.00        | 99.00        | YES         
ru_nvcsw        | 1.84345e-01  | 0.00         | 1.00         | no          
ru_maxrss       | 2.46967e-01  | 1592.00      | 1592.00      | no          
ru_stime        | 3.51166e-01  | 0.00         | 0.00         | no          
elapsed_us      | 3.82614e-01  | 743.00       | 745.50       | no          
ru_nivcsw       | 6.17364e-01  | 0.00         | 0.00         | no          
ru_utime        | 7.50660e-01  | 0.00         | 0.00         | no          
ru_inblock      | 1.00000e+00  | 0.00         | 0.00         | no          
ru_majflt       | 1.00000e+00  | 0.00         | 0.00         | no          
ru_oublock      | 1.00000e+00  | 0.00         | 0.00         | no          
---------------------------------------------------------------------------

[+] Conclusion: The best predictor is 'ru_minflt'.
    You can likely distinguish between the two scenarios by observing this value.
```

References
==========

- [getrusage()](https://manpages.opensuse.org/Tumbleweed/man-pages/getrusage.2.en.html)
- [Mann-Whitney U test](https://en.wikipedia.org/wiki/Mann%E2%80%93Whitney_U_test)
- [CVE-2024-0149 (information leak)](https://security.opensuse.org/2025/03/26/nvidia-modprobe.html)
